#!/usr/bin/env python3
"""Catalogue every original streamed EA-XMA source without changing asset bytes.

The binary format is intentionally simple to read from the native runtime:

  header  <8sIIIIIQQQQQQ>        76 bytes, magic SIMAUD01, version 1
  source  <QIIIIQ32s>           64 bytes, one SNU or MUS file
  stream  <IIQQQIIIIIIII8s>     72 bytes, one SNU or MUS substream
  block   <IIQIIIHHI32s32s>   100 bytes, one ordered EAAC block
  layer   <IIQIQII>            36 bytes, one ordered XMA layer

All integers are little endian. Tables are contiguous in that order, followed
by a blob of concatenated UTF-8 source paths. Source paths are relative to the
original asset root, with forward slashes. The row field order is documented
in FIELD_NAMES below. Offsets in source paths address the string blob; all
other offsets address original asset files. The normalized block digest hashes
the original block with its first byte masked by 0x7f, as the original reader
does before native admission. No audio is decoded or rewritten here.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
from collections import Counter, defaultdict
from collections.abc import Sequence

from inspect_assets import inspect_mus, inspect_snu, inventory


MAGIC = b"SIMAUD01"
VERSION = 1
HEADER = struct.Struct("<8sIIIIIQQQQQQ")
SOURCE = struct.Struct("<QIIIIQ32s")
STREAM = struct.Struct("<IIQQQIIIIIIII8s")
BLOCK = struct.Struct("<IIQIIIHHI32s32s")
LAYER = struct.Struct("<IIQIQII")
FIELD_NAMES = {
    "source": ("path_offset", "path_bytes", "first_stream", "stream_count",
               "kind", "file_bytes", "sha256"),
    "stream": ("source_index", "ordinal", "header_offset", "audio_offset",
               "audio_bytes", "first_block", "block_count", "sample_rate",
               "channels", "frames", "loop_start_sample", "loop_offset_relative",
               "flags", "header"),
    "block": ("stream_index", "ordinal", "raw_offset", "raw_bytes", "frames",
              "first_layer", "layer_count", "segment_flag", "terminal_padding",
              "raw_sha256", "normalized_sha256"),
    "layer": ("block_index", "ordinal", "header_offset", "layer_bytes",
              "payload_offset", "payload_bytes", "restored_ff"),
}


def _need(value: bool, message: str) -> None:
    if not value:
        raise ValueError(message)


def _sha(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def _source_entries(root: Path):
    paths = [p for p in inventory(root) if p.suffix.lower() in (".snu", ".mus")]
    return sorted(paths, key=lambda p: (
        p.relative_to(root).as_posix().casefold(), p.relative_to(root).as_posix()))


def _report_data(report: Path | None):
    if report is None:
        return None, {}
    data = json.loads(Path(report).read_text(encoding="utf-8"))
    _need(data.get("schema_version") == 1 and isinstance(data.get("files"), list),
          "Unsupported asset inspection report")
    files = {item["path"]: item for item in data["files"]}
    _need(len(files) == len(data["files"]), "Duplicate asset report paths")
    return data, files


def _media_summary(report_data):
    if report_data is None:
        return {"status": "asset_report_not_supplied", "sbk_resources": [],
                "movie_audio": [], "other_resource_type_occurrences": {}}
    banks = []
    movies = []
    for item in report_data["files"]:
        result = item["inspection"]
        if item["extension"] == ".vp6":
            _need(result.get("format") == "EA_chunked_VP6", "Rejected VP6 in asset report")
            movies.append({
                "path": item["path"], "source_sha256": item["sha256"],
                "file_bytes": item["size"], "codec": result["audio_codec"],
                "channels": result["audio_channels"],
                "audio_packets": result["chunk_counts"]["SCDl"],
                "audio_samples": result["audio_sample_sum"],
            })
        for entry in result.get("entries", []):
            for chunk in entry.get("chunks", []):
                if chunk.get("type_name") == "SBK":
                    banks.append({
                        "container_path": item["path"], "container_sha256": item["sha256"],
                        "entry_index": entry["index"], "name": chunk["name"],
                        "payload_bytes": chunk["payload_size"],
                        "payload_sha256": chunk["payload_sha256"],
                        "payload_decoded_offset": chunk["payload_decoded_offset"],
                    })
    banks.sort(key=lambda b: (b["container_path"], b["entry_index"], b["name"]))
    movies.sort(key=lambda m: m["path"])
    types = report_data["summary"]["str"]["resource_type_occurrences"]
    return {
        "status": "inventory_only_codec_validation_separate",
        "sbk_resource_count": len(banks), "sbk_resources": banks,
        "movie_count": len(movies), "movie_audio_packets": sum(m["audio_packets"] for m in movies),
        "movie_audio": movies,
        "other_resource_type_occurrences": {
            name: types.get(name, 0) for name in ("AUC", "BNK", "MIX", "MSX")},
    }


def _append_stream(source_index: int, ordinal: int, data: bytes, header_offset: int,
                   audio_offset: int, audio_bytes: int, header_info: dict,
                   stream_rows: bytearray, block_rows: bytearray, layer_rows: bytearray,
                   stream_summaries: list, path: str) -> tuple[int, int]:
    stream_index = len(stream_rows) // STREAM.size
    first_block = len(block_rows) // BLOCK.size
    end = audio_offset + audio_bytes
    _need(0 <= header_offset <= len(data) - 8 and
          0 <= audio_offset < end <= len(data), "Invalid source stream extents")
    header = data[header_offset:header_offset + 8]
    channels = header_info["channels"]
    at = audio_offset
    block_count = 0
    frames = 0
    segment_ends = 0
    while at < end:
        word = int.from_bytes(data[at:at + 4], "big")
        flag = word >> 24
        size = word & 0xFFFFFF
        _need(flag in (0, 0x80) and 8 <= size <= end - at,
              f"Invalid EAAC block: {path} stream {ordinal} offset {at}")
        sample_count = int.from_bytes(data[at + 4:at + 8], "big")
        layer_count = (channels + 1) // 2
        first_layer = len(layer_rows) // LAYER.size
        layer_at = at + 8
        for layer_ordinal in range(layer_count):
            _need(layer_at + 8 <= at + size, "Truncated EAAC layer")
            layer_word = int.from_bytes(data[layer_at:layer_at + 4], "big")
            layer_bytes = layer_word >> 2
            _need(layer_word & 3 == 3 and layer_bytes >= 8 and
                  layer_at + layer_bytes <= at + size and
                  data[layer_at + 4:layer_at + 8] == b"\x08\0\0\0",
                  "Invalid EA-XMA layer")
            payload_offset = layer_at + 4
            payload_bytes = layer_bytes - 4
            restored_ff = (-payload_bytes) % 2048
            layer_rows.extend(LAYER.pack(first_block + block_count, layer_ordinal,
                                         layer_at, layer_bytes, payload_offset,
                                         payload_bytes, restored_ff))
            layer_at += layer_bytes
        padding = at + size - layer_at
        _need((padding == 0 or flag == 0x80 and padding <= 63) and
              not any(data[layer_at:at + size]), "Invalid EAAC trailing padding")
        raw = data[at:at + size]
        normalized = bytes([raw[0] & 0x7F]) + raw[1:]
        block_rows.extend(BLOCK.pack(stream_index, block_count, at, size,
                                     sample_count, first_layer, layer_count,
                                     flag, padding, _sha(raw), _sha(normalized)))
        at += size
        block_count += 1
        frames += sample_count
        segment_ends += flag == 0x80
    _need(at == end and frames == header_info["samples"] and
          segment_ends == (2 if header_info["loop"] else 1),
          "Stream block accounting differs from original header")
    loop_start = header_info.get("loop_start_sample", 0)
    loop_offset = header_info.get("loop_offset_relative", 0)
    stream_rows.extend(STREAM.pack(source_index, ordinal, header_offset,
                                   audio_offset, audio_bytes, first_block,
                                   block_count, header_info["sample_rate"], channels,
                                   frames, loop_start, loop_offset,
                                   int(header_info["loop"]), header))
    stream_summaries.append({
        "path": path, "ordinal": ordinal, "header_hex": header.hex(),
        "header_offset": header_offset, "audio_offset": audio_offset,
        "audio_bytes": audio_bytes, "sample_rate": header_info["sample_rate"],
        "channels": channels, "samples": frames, "loop": header_info["loop"],
        "loop_start_sample": loop_start, "loop_offset_relative": loop_offset,
        "blocks": block_count, "first_block": first_block,
    })
    return stream_index, block_count


def generate(root: Path, report: Path | None, binary: Path,
             summary: Path) -> dict:
    """Validate/capture the complete original streamed corpus and write both outputs.

    `report` may be None for synthetic fixtures. If supplied, every source's
    full-file SHA256 must match the separately generated asset inspection.
    """
    root = Path(root).resolve(strict=True)
    binary = Path(binary).absolute()
    summary = Path(summary).absolute()
    _need(root.is_dir(), "Audio asset root is not a directory")
    _need(not binary.resolve().is_relative_to(root) and
          not summary.resolve().is_relative_to(root) and binary != summary,
          "Catalog output must stay outside original asset root")
    report_data, report_files = _report_data(report)
    source_rows = bytearray()
    stream_rows = bytearray()
    block_rows = bytearray()
    layer_rows = bytearray()
    strings = bytearray()
    source_summaries = []
    stream_summaries = []
    paths = _source_entries(root)
    for path in paths:
        relative = path.relative_to(root).as_posix()
        data = path.read_bytes()
        source_hash = _sha(data)
        if report_data is not None:
            report_item = report_files.get(relative)
            _need(report_item is not None and report_item["sha256"] == source_hash.hex() and
                  report_item["size"] == len(data),
                  f"Audio asset/report identity differs: {relative}")
        kind = 1 if path.suffix.lower() == ".snu" else 2
        source_index = len(source_rows) // SOURCE.size
        first_stream = len(stream_rows) // STREAM.size
        if kind == 1:
            info = inspect_snu(data)
            _append_stream(source_index, 0, data, 16,
                           info["audio"]["audio_offset"], info["audio"]["audio_size"],
                           info["header"], stream_rows, block_rows, layer_rows,
                           stream_summaries, relative)
        else:
            info = inspect_mus(data)
            for stream in info["streams"]:
                _append_stream(source_index, stream["index"], data,
                               stream["header"]["header_offset"],
                               stream["audio"]["audio_offset"],
                               stream["audio"]["audio_size"], stream["header"],
                               stream_rows, block_rows, layer_rows,
                               stream_summaries, relative)
        path_bytes = relative.encode("utf-8")
        _need(len(path_bytes) <= 0xFFFFFFFF, "Audio source path too long")
        source_rows.extend(SOURCE.pack(len(strings), len(path_bytes), first_stream,
                                       len(stream_rows) // STREAM.size - first_stream,
                                       kind, len(data), source_hash))
        strings.extend(path_bytes)
        source_summaries.append({"path": relative, "kind": "SNU" if kind == 1 else "MUS",
                                 "file_bytes": len(data), "source_sha256": source_hash.hex(),
                                 "first_stream": first_stream,
                                 "stream_count": len(stream_rows) // STREAM.size - first_stream})
    counts = (len(source_rows) // SOURCE.size, len(stream_rows) // STREAM.size,
              len(block_rows) // BLOCK.size, len(layer_rows) // LAYER.size)
    _need(all(n <= 0xFFFFFFFF for n in counts), "Audio catalog count overflow")
    source_offset = HEADER.size
    stream_offset = source_offset + len(source_rows)
    block_offset = stream_offset + len(stream_rows)
    layer_offset = block_offset + len(block_rows)
    string_offset = layer_offset + len(layer_rows)
    header = HEADER.pack(MAGIC, VERSION, *counts, source_offset, stream_offset,
                         block_offset, layer_offset, string_offset, len(strings))
    catalog = b"".join((header, source_rows, stream_rows, block_rows, layer_rows, strings))
    digest = _sha(catalog).hex()
    media = _media_summary(report_data)
    channels = Counter(s["channels"] for s in stream_summaries)
    loops = sum(s["loop"] for s in stream_summaries)
    by_header = defaultdict(list)
    by_first = defaultdict(list)
    for index, stream in enumerate(stream_summaries):
        header_hex = stream["header_hex"]
        first = stream["first_block"]
        first_hash = BLOCK.unpack_from(block_rows, first * BLOCK.size)[-1].hex()
        by_header[header_hex].append(index)
        by_first[(header_hex, first_hash)].append(index)
    header_aliases = [{"header_hex": header, "stream_indices": indices}
                      for header, indices in sorted(by_header.items()) if len(indices) > 1]
    first_aliases = [{"header_hex": header, "first_block_sha256": digest,
                      "stream_indices": indices}
                     for (header, digest), indices in sorted(by_first.items())
                     if len(indices) > 1]
    report_output = {
        "schema_version": 1,
        "binary_format": "SIMAUD01 little-endian v1",
        "binary_sha256": digest,
        "binary_bytes": len(catalog),
        "source_count": counts[0], "stream_count": counts[1],
        "block_count": counts[2], "layer_count": counts[3],
        "snu_count": sum(s["kind"] == "SNU" for s in source_summaries),
        "mus_count": sum(s["kind"] == "MUS" for s in source_summaries),
        "channels": {str(k): channels[k] for k in sorted(channels)},
        "loop_streams": loops,
        "collision_inventory": {
            "header_only_group_count": len(header_aliases),
            "header_only_stream_count": sum(len(g["stream_indices"]) for g in header_aliases),
            "header_and_first_block_group_count": len(first_aliases),
            "header_and_first_block_stream_count": sum(len(g["stream_indices"]) for g in first_aliases),
            "header_only_groups": header_aliases,
            "header_and_first_block_groups": first_aliases,
        },
        "source_provenance": source_summaries,
        "stream_inventory": stream_summaries,
        "embedded_and_movie_audio": media,
        "qualification": "container_structure_and_exact_block_identity; XMA bitstreams not decoded",
    }
    _atomic_write(binary, catalog)
    encoded = (json.dumps(report_output, ensure_ascii=True, sort_keys=True,
                          separators=(",", ":")) + "\n").encode("utf-8")
    _atomic_write(summary, encoded)
    return report_output


class _Table(Sequence):
    def __init__(self, catalog: "Catalog", name: str, layout: struct.Struct,
                 offset: int, count: int):
        self.catalog, self.name, self.layout = catalog, name, layout
        self.offset, self.count = offset, count

    def __len__(self):
        return self.count

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(self.count))]
        if index < 0:
            index += self.count
        if not 0 <= index < self.count:
            raise IndexError(index)
        values = self.layout.unpack_from(self.catalog.data,
                                         self.offset + index * self.layout.size)
        row = dict(zip(FIELD_NAMES[self.name], values))
        if self.name == "source":
            start = self.catalog.string_offset + row["path_offset"]
            end = start + row["path_bytes"]
            _need(end <= len(self.catalog.data), "Source path outside string table")
            row["path"] = self.catalog.data[start:end].decode("utf-8")
        return row


class Catalog:
    def __init__(self, data: bytes):
        self.data = data
        _need(len(data) >= HEADER.size, "Truncated audio catalog header")
        (magic, version, sources, streams, blocks, layers, source_offset,
         stream_offset, block_offset, layer_offset, string_offset,
         string_bytes) = HEADER.unpack_from(data)
        _need(magic == MAGIC and version == VERSION, "Unsupported audio catalog format")
        _need(source_offset == HEADER.size and
              stream_offset == source_offset + sources * SOURCE.size and
              block_offset == stream_offset + streams * STREAM.size and
              layer_offset == block_offset + blocks * BLOCK.size and
              string_offset == layer_offset + layers * LAYER.size and
              len(data) == string_offset + string_bytes,
              "Audio catalog table extents are invalid")
        self.string_offset = string_offset
        self.sources = _Table(self, "source", SOURCE, source_offset, sources)
        self.streams = _Table(self, "stream", STREAM, stream_offset, streams)
        self.blocks = _Table(self, "block", BLOCK, block_offset, blocks)
        self.layers = _Table(self, "layer", LAYER, layer_offset, layers)


def read_catalog(path: Path) -> Catalog:
    return Catalog(Path(path).read_bytes())


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    project = Path(__file__).resolve().parents[1]
    parser.add_argument("--root", type=Path, default=project / "Simpsons Game, The (USA)")
    parser.add_argument("--assets-report", type=Path, default=project / "analysis/assets.json")
    parser.add_argument("--output", type=Path, default=project / "analysis/audio_catalog.bin")
    parser.add_argument("--summary", type=Path, default=project / "analysis/audio_catalog_summary.json")
    args = parser.parse_args(argv)
    result = generate(args.root, args.assets_report, args.output, args.summary)
    print(f"Catalogued {result['stream_count']} EA-XMA streams, "
          f"{result['block_count']} blocks, {result['layer_count']} layers; "
          f"{result['embedded_and_movie_audio']['sbk_resource_count']} SBK resources and "
          f"{result['embedded_and_movie_audio']['movie_count']} movies inventoried")
    return 0


if __name__ == "__main__":
    sys.exit(main())
