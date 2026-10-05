#!/usr/bin/env python3
"""Inventory every embedded SBK resident XMA cue in the original STR assets.

This validates bank envelopes, decoded resource identity, nonoverlapping cue
extents, header/sample accounting and block framing. It records exact source
hashes for later decoder qualification; it does not claim to decode every cue.

The SIMRES01 binary is little-endian, with tightly contiguous tables followed
by a UTF-8 string blob. Layouts and field order are BIN_* / BIN_FIELD_NAMES
below. Path and name offsets are relative to the string blob; cue/block offsets
are absolute within the original decoded SBK payload.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

from extract_resource import select_payload


MAGIC = 0x6B6E6273  # "knbs"
VERSION = 203
CODEC_RATES = (24000, 32000, 44100, 48000)
# The resident header's playback clock is independent of the XMA layer clock.
# Low-rate cues (for example 12/16 kHz) still carry 24-48 kHz XMA packets.
MIN_PLAYBACK_RATE = 8000
MAX_PLAYBACK_RATE = 48000
BIN_MAGIC = b"SIMRES01"
BIN_VERSION = 1
BIN_HEADER = struct.Struct("<8sIIIIQQQQQ")
BIN_BANK = struct.Struct("<QIQIQQQQIII32s32s")
BIN_CUE = struct.Struct("<IIQIIIIIII8sQ")
BIN_BLOCK = struct.Struct("<IIQIIIIII32s")
BIN_FIELD_NAMES = {
    "bank": ("path_offset", "path_bytes", "name_offset", "name_bytes",
             "payload_bytes", "metadata_bytes", "audio_offset", "audio_bytes",
             "first_cue", "cue_count", "entry_index", "payload_sha256",
             "container_sha256"),
    "cue": ("bank_index", "ordinal", "header_offset", "first_block",
            "block_count", "frames", "playback_rate", "channels",
            "loop_start_sample", "flags", "header", "end_offset"),
    "block": ("cue_index", "ordinal", "offset", "bytes", "frames",
              "selector", "codec_rate", "restored_ff", "payload_bytes",
              "sha256"),
}


def need(value: bool, reason: str) -> None:
    if not value:
        raise ValueError(reason)


def be32(data: bytes, at: int) -> int:
    return struct.unpack_from(">I", data, at)[0]


def bank_envelope(data: bytes) -> dict:
    need(len(data) >= 24, "Truncated SBK envelope")
    magic, version, metadata_bytes, audio_bytes, audio_offset, repeat = struct.unpack_from(">6I", data)
    need(magic == MAGIC and version == VERSION and audio_bytes == repeat and
         metadata_bytes + 24 <= audio_offset and
         audio_offset + audio_bytes == len(data), "Unrecognized SBK envelope/split")
    return {"metadata_bytes": metadata_bytes, "audio_offset": audio_offset,
            "audio_bytes": audio_bytes}


def cue_at(data: bytes, offset: int):
    """Return one structurally sound cue candidate or None at an arbitrary byte."""
    if offset + 20 > len(data) or data[offset] != 3:
        return None
    first, second = struct.unpack_from(">II", data, offset)
    channels = ((first >> 18) & 63) + 1
    playback_rate = first & 0x3FFFF
    total_frames = second & 0x1FFFFFFF
    looping = bool((second >> 29) & 1)
    if (first >> 28 or (first >> 24) & 15 != 3 or second >> 30 or
            channels not in (1, 2) or
            not MIN_PLAYBACK_RATE <= playback_rate <= MAX_PLAYBACK_RATE or
            not 0 < total_frames <= 4_000_000):
        return None
    loop_start = be32(data, offset + 8) if looping else 0
    if looping and loop_start >= total_frames:
        return None
    at = offset + 8 + 4 * looping
    expected_frames = ([loop_start, total_frames - loop_start]
                       if looping and loop_start else [total_frames])
    blocks = []
    for ordinal, frames in enumerate(expected_frames):
        if at + 12 > len(data):
            return None
        size, declared, layer = struct.unpack_from(">III", data, at)
        if not (12 < size <= min(16 * 1024 * 1024, len(data) - at) and
                declared == frames and layer >> 2 == size - 8):
            return None
        selector = layer & 3
        payload_bytes = size - 12
        blocks.append({
            "ordinal": ordinal, "offset": at, "bytes": size,
            "frames": declared, "selector": selector,
            "codec_rate": CODEC_RATES[selector],
            "payload_bytes": payload_bytes,
            "restored_ff": (-payload_bytes) % 2048,
            "sha256": hashlib.sha256(data[at:at + size]).hexdigest(),
        })
        at += size
    return {
        "header_offset": offset, "header_hex": data[offset:offset + 8].hex(),
        "channels": channels, "playback_rate": playback_rate,
        "frames": total_frames, "loop": looping, "loop_start_sample": loop_start,
        "end_offset": at, "blocks": blocks,
    }


def discover_cues(data: bytes, audio_offset: int) -> list[dict]:
    cues = []
    at = audio_offset
    while True:
        at = data.find(b"\x03", at)
        if at < 0:
            break
        cue = cue_at(data, at)
        if cue is not None:
            if cues:
                need(cues[-1]["end_offset"] == at,
                     "Gap or overlap between resident cue candidates")
            cues.append(cue)
            at = cue["end_offset"]
        else:
            at += 1
    return cues


def binary_catalog(banks: list[dict]) -> bytes:
    bank_rows = bytearray()
    cue_rows = bytearray()
    block_rows = bytearray()
    strings = bytearray()
    for bank_index, bank in enumerate(banks):
        first_cue = len(cue_rows) // BIN_CUE.size
        path = bank["container_path"].encode("utf-8")
        name = bank["name"].encode("utf-8")
        path_offset = len(strings)
        strings.extend(path)
        name_offset = len(strings)
        strings.extend(name)
        for ordinal, cue in enumerate(bank["cues"]):
            cue_index = len(cue_rows) // BIN_CUE.size
            first_block = len(block_rows) // BIN_BLOCK.size
            for block in cue["blocks"]:
                block_rows.extend(BIN_BLOCK.pack(
                    cue_index, block["ordinal"], block["offset"], block["bytes"],
                    block["frames"], block["selector"], block["codec_rate"],
                    block["restored_ff"], block["payload_bytes"],
                    bytes.fromhex(block["sha256"])))
            cue_rows.extend(BIN_CUE.pack(
                bank_index, ordinal, cue["header_offset"], first_block,
                len(cue["blocks"]), cue["frames"], cue["playback_rate"],
                cue["channels"], cue["loop_start_sample"], int(cue["loop"]),
                bytes.fromhex(cue["header_hex"]), cue["end_offset"]))
        bank_rows.extend(BIN_BANK.pack(
            path_offset, len(path), name_offset, len(name), bank["payload_bytes"],
            bank["metadata_bytes"], bank["audio_offset"], bank["audio_bytes"],
            first_cue, len(bank["cues"]), bank["entry_index"],
            bytes.fromhex(bank["payload_sha256"]),
            bytes.fromhex(bank["container_sha256"])))
    counts = (len(bank_rows) // BIN_BANK.size, len(cue_rows) // BIN_CUE.size,
              len(block_rows) // BIN_BLOCK.size)
    need(all(value <= 0xFFFFFFFF for value in counts), "Resident catalog count overflow")
    bank_offset = BIN_HEADER.size
    cue_offset = bank_offset + len(bank_rows)
    block_offset = cue_offset + len(cue_rows)
    string_offset = block_offset + len(block_rows)
    header = BIN_HEADER.pack(BIN_MAGIC, BIN_VERSION, *counts, bank_offset,
                             cue_offset, block_offset, string_offset, len(strings))
    return b"".join((header, bank_rows, cue_rows, block_rows, strings))


def generate(root: Path, assets_report: Path, output: Path,
             binary: Path | None = None) -> dict:
    root = Path(root).resolve(strict=True)
    output = Path(output).absolute()
    binary = Path(binary).absolute() if binary is not None else output.with_suffix(".bin")
    need(root.is_dir() and not output.resolve().is_relative_to(root),
         "Resident catalog output must stay outside asset root")
    need(not binary.resolve().is_relative_to(root) and binary != output,
         "Resident binary output must stay outside asset root")
    report = json.loads(Path(assets_report).read_text(encoding="utf-8"))
    need(report.get("schema_version") == 1 and report.get("options", {}).get("decode_str") is True,
         "Resident catalog needs the decoded STR asset inspection report")
    banks = []
    for file in report["files"]:
        for entry in file["inspection"].get("entries", []):
            for chunk in entry.get("chunks", []):
                if chunk.get("type_name") != "SBK":
                    continue
                source = root / file["path"]
                data, provenance = select_payload(source, entry["index"], chunk["name"], root)
                need(provenance["source"]["sha256"] == file["sha256"] and
                     provenance["resource"]["payload_sha256"] == chunk["payload_sha256"] and
                     len(data) == chunk["payload_size"],
                     "SBK resource differs from independent asset report")
                envelope = bank_envelope(data)
                cues = discover_cues(data, envelope["audio_offset"])
                need(cues and cues[-1]["end_offset"] == len(data),
                     "Resident cue sequence does not cover bank audio through EOF")
                banks.append({
                    "container_path": file["path"],
                    "container_sha256": file["sha256"],
                    "entry_index": entry["index"],
                    "name": chunk["name"],
                    "payload_sha256": chunk["payload_sha256"],
                    "payload_bytes": len(data),
                    **envelope,
                    "cue_start_offset": cues[0]["header_offset"],
                    "audio_prefix_bytes": cues[0]["header_offset"] - envelope["audio_offset"],
                    "cue_count": len(cues), "cues": cues,
                    "status": "complete_contiguous_structural_cue_section",
                })
    banks.sort(key=lambda b: (b["container_path"], b["entry_index"], b["name"]))
    need(len(banks) == report["summary"]["str"]["resource_type_occurrences"]["SBK"],
         "SBK inventory count differs from decoded asset report")
    cue_count = sum(b["cue_count"] for b in banks)
    block_count = sum(len(c["blocks"]) for b in banks for c in b["cues"])
    packed = binary_catalog(banks)
    rates = Counter(c["playback_rate"] for b in banks for c in b["cues"])
    result = {
        "schema_version": 1,
        "qualification": "validated_SBK_envelope_resource_hash_and_structural_cue_blocks; XMA bitstreams not decoded",
        "bank_count": len(banks), "banks_with_cues": sum(bool(b["cues"]) for b in banks),
        "unique_payload_count": len({b["payload_sha256"] for b in banks}),
        "cue_count": cue_count, "block_count": block_count,
        "mono_cues": sum(c["channels"] == 1 for b in banks for c in b["cues"]),
        "stereo_cues": sum(c["channels"] == 2 for b in banks for c in b["cues"]),
        "loop_cues": sum(c["loop"] for b in banks for c in b["cues"]),
        "playback_rates": {str(k): rates[k] for k in sorted(rates)},
        "binary_format": "SIMRES01 little-endian v1",
        "binary_sha256": hashlib.sha256(packed).hexdigest(),
        "binary_bytes": len(packed),
        "structural_coverage": "Every cue in each bank is contiguous from its first cue to SBK EOF.",
        "banks": banks,
    }
    binary.parent.mkdir(parents=True, exist_ok=True)
    temporary_binary = binary.with_name(binary.name + ".tmp")
    temporary_binary.write_bytes(packed)
    temporary_binary.replace(binary)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    temporary.write_text(json.dumps(result, ensure_ascii=True, sort_keys=True,
                                   separators=(",", ":")) + "\n", encoding="utf-8")
    temporary.replace(output)
    return result


def main(argv=None) -> int:
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=project / "Simpsons Game, The (USA)")
    parser.add_argument("--assets-report", type=Path, default=project / "analysis/assets.json")
    parser.add_argument("--output", type=Path, default=project / "analysis/resident_audio_catalog.json")
    parser.add_argument("--binary", type=Path, default=project / "analysis/resident_audio_catalog.bin")
    args = parser.parse_args(argv)
    result = generate(args.root, args.assets_report, args.output, args.binary)
    print(f"Catalogued {result['cue_count']} resident cues in "
          f"{result['banks_with_cues']}/{result['bank_count']} SBK banks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
