#!/usr/bin/env python3
"""Audit the complete original audio census against native format/storage bounds.

No game, compiler, decoder, or audio device is started. The optional original
stream scan checks every original file and ordered block hash; it writes no
extracted audio. This is an asset coverage audit, not proof that every original
caller, seek, cancellation, or worker scheduling path is implemented.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from generate_audio_catalog import read_catalog

ROOT = Path(__file__).resolve().parents[1]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def need(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def resident_report(catalog: dict, key: str) -> dict:
    violations = []
    channels, rates, codec_rates = Counter(), Counter(), Counter()
    max_bytes = max_frames = max_ring = 0
    cues = blocks = loops = 0
    for owner in catalog[key]:
        label = owner.get("container_path", owner.get("name", owner.get("payload_sha256", "AMX")))
        if key == "banks" and owner["payload_bytes"] > 16 * 1024 * 1024:
            violations.append({"source": label, "reason": "named SBK loader exceeds 16 MiB"})
        for cue in owner["cues"]:
            cues += 1
            loops += bool(cue["loop"])
            channels[cue["channels"]] += 1
            rates[cue["playback_rate"]] += 1
            offset = cue["header_offset"] + (12 if cue["loop"] else 8)
            required_blocks = 2 if cue["loop"] and cue["loop_start_sample"] else 1
            if cue["channels"] not in (1, 2) or not 8000 <= cue["playback_rate"] <= 48000:
                violations.append({"source": label, "cue": cue["header_offset"], "reason": "resident channel/playback rate"})
            if len(cue["blocks"]) != required_blocks or key == "payloads" and required_blocks != 1:
                violations.append({"source": label, "cue": cue["header_offset"], "reason": "resident intro/loop block shape"})
            frame_sum = 0
            for block in cue["blocks"]:
                blocks += 1
                ring = (block["frames"] + 384 + 4096 + 511) & ~511
                max_bytes = max(max_bytes, block["bytes"])
                max_frames = max(max_frames, block["frames"])
                max_ring = max(max_ring, ring)
                codec_rates[block["codec_rate"]] += 1
                if not (0 < block["bytes"] <= 1024 * 1024 and 0 < block["frames"] and
                        ring <= 4194304 and block["offset"] == offset and
                        block["codec_rate"] in (24000, 32000, 44100, 48000) and
                        block["restored_ff"] == (-block["payload_bytes"]) % 2048):
                    violations.append({"source": label, "cue": cue["header_offset"], "block": block["offset"],
                                       "reason": "resident owned block/ring/codec bound"})
                offset += block["bytes"]
                frame_sum += block["frames"]
            if offset != cue["end_offset"] or frame_sum != cue["frames"]:
                violations.append({"source": label, "cue": cue["header_offset"], "reason": "resident extent/sample sum"})
    return {"owners": len(catalog[key]), "cues": cues, "blocks": blocks, "loop_cues": loops,
            "channels": dict(channels), "playback_rates": dict(rates), "codec_rates": dict(codec_rates),
            "max_block_bytes": max_bytes, "max_block_frames": max_frames,
            "max_conservative_ring_frames": max_ring, "violations": violations}


def probe_report(path: Path, catalog: Path) -> dict:
    if not path.exists():
        return {"present": False}
    report = json.loads(path.read_text(encoding="utf-8"))
    current = json.loads(catalog.read_text(encoding="utf-8"))
    owner_key = "banks" if "banks" in current else "payloads"
    expected = {(block["sha256"], cue["channels"], block["codec_rate"], block["frames"],
                 block["restored_ff"], bool(cue["loop"]))
                for owner in current[owner_key] for cue in owner["cues"] for block in cue["blocks"]}
    decoded = {(row["block_sha256"], row["channels"], row["codec_rate"], row["declared_frames"],
                row["restored_ff"], bool(row["cue_loop"]))
               for row in report["records"] if row["qualified"]}
    codec_path = ROOT / "audio/native_xma_codec.cpp"
    dlls = report.get("codec_dll_sha256", {})
    return {"present": True, "report_sha256": sha(path.read_bytes()), "scope": report["scope"],
            "qualified_count": report["qualified_count"], "failed_count": report["failed_count"],
            "catalog_identity_current": report["catalog_sha256"] == sha(catalog.read_bytes()),
            "encoded_block_keys_expected": len(expected),
            "encoded_block_keys_covered": len(expected & decoded),
            "encoded_block_corpus_identity_current": expected == decoded,
            "codec_source_identity_current": report["codec_source_sha256"] == sha(codec_path.read_bytes()),
            "codec_dll_identities_current": bool(dlls) and all(
                (ROOT / "build/audio-codec/install/bin" / name).exists() and
                sha((ROOT / "build/audio-codec/install/bin" / name).read_bytes()) == digest
                for name, digest in dlls.items()),
            "raw_eof_sent": report["raw_eof_sent"], "pcm_files_written": report["pcm_files_written"]}


def stream_decode_expectations(catalog) -> dict:
    """Reconstruct exact chains and coverage from the current binary inventory."""
    groups = {}
    for index, stream in enumerate(catalog.streams):
        digest = hashlib.sha256(stream["header"])
        digest.update(struct.pack("<5I", stream["channels"], stream["sample_rate"], stream["flags"],
                                  stream["loop_start_sample"], stream["block_count"]))
        packets = [0] * ((stream["channels"] + 1) // 2)
        for at in range(stream["first_block"], stream["first_block"] + stream["block_count"]):
            block = catalog.blocks[at]
            need(block["stream_index"] == index and block["ordinal"] == at - stream["first_block"] and
                 block["layer_count"] == len(packets), "Stream decoder catalog sequence/layers differ")
            digest.update(block["normalized_sha256"])
            digest.update(struct.pack("<I", block["segment_flag"]))
            for layer_index in range(len(packets)):
                layer = catalog.layers[block["first_layer"] + layer_index]
                need(layer["block_index"] == at and layer["ordinal"] == layer_index and
                     layer["restored_ff"] == (-layer["payload_bytes"]) % 2048,
                     "Stream decoder catalog packet provenance differs")
                packets[layer_index] += (layer["payload_bytes"] + layer["restored_ff"]) // 2048
        key = digest.hexdigest()
        if key in groups:
            need(groups[key]["packets"] == packets, "Equivalent stream packet counts differ")
            groups[key]["catalog_stream_aliases"].append(index)
            continue
        source = catalog.sources[stream["source_index"]]
        groups[key] = {"stream_index": index, "catalog_stream_aliases": [index],
                       "ordered_chain_sha256": key, "source_path": source["path"],
                       "source_sha256": source["sha256"].hex(), "ordinal": stream["ordinal"],
                       "header_hex": stream["header"].hex(), "channels": stream["channels"],
                       "sample_rate": stream["sample_rate"], "loop": bool(stream["flags"]),
                       "declared_frames": stream["frames"], "block_count": stream["block_count"],
                       "packets": packets, "source_index": stream["source_index"]}
    rows = list(groups.values())
    native_sources = {row["source_index"] for row in rows}
    alias_indices = [index for row in rows for index in row["catalog_stream_aliases"][1:]]
    alias_sources = {catalog.streams[index]["source_index"] for index in alias_indices}
    return {"rows": rows, "catalog_sha256": sha(catalog.data), "streams": len(catalog.streams),
            "sources": len(catalog.sources), "blocks": len(catalog.blocks),
            "unique_blocks": sum(row["block_count"] for row in rows),
            "alias_streams": len(alias_indices),
            "alias_blocks": sum(catalog.streams[index]["block_count"] for index in alias_indices),
            "native_sources": len(native_sources), "additional_alias_sources": len(alias_sources - native_sources)}


def stream_decode_report(path: Path | None, catalog, expected: dict | None = None) -> dict:
    """Require complete, current pure-decoder evidence; never infer live scheduling."""
    if path is None:
        return {"present": False, "one_pass_decode_proven": False}
    need(path.is_file(), "Explicit streamed decoder report is missing")
    need(path.stat().st_size <= 32 * 1024 * 1024, "Streamed decoder report exceeds bound")
    data = path.read_bytes()
    report = json.loads(data)
    expected = expected if expected is not None else stream_decode_expectations(catalog)
    need(report.get("probe_version") == 1 and report.get("scope") == "all_unique_stream_chains" and
         report.get("complete") is True and report.get("timed_out") is False,
         "Streamed decoder report is partial or has an unsupported scope")
    need(report.get("catalog_sha256") == expected["catalog_sha256"], "Streamed decoder input catalog identity is stale")
    source_fields = {
        "probe_source_sha256": "tools/probe_stream_catalog_decode.cpp",
        "driver_source_sha256": "tools/probe_stream_catalog_decode.py",
        "codec_source_sha256": "audio/native_xma_codec.cpp",
        "parser_source_sha256": "audio/ea_xma_block.cpp",
    }
    for field, relative in source_fields.items():
        need(report.get(field) == sha((ROOT / relative).read_bytes()), f"Streamed decoder provenance is stale: {relative}")
    dlls = ("avcodec-simpsonsxma-62.dll", "avutil-simpsonsxma-60.dll", "libwinpthread-1.dll")
    need(set(report.get("codec_dll_sha256", {})) == set(dlls), "Streamed decoder DLL provenance is incomplete")
    for name in dlls:
        need(report["codec_dll_sha256"][name] == sha((ROOT / "build/audio-codec/install/bin" / name).read_bytes()),
             f"Streamed decoder DLL provenance is stale: {name}")
    need(all(report.get(field) is False for field in ("raw_eof_sent", "pcm_files_written", "audio_endpoint_opened")),
         "Streamed decoder probe is outside the pure raw decode contract")
    rows = report.get("records")
    need(isinstance(rows, list) and len(rows) == len(expected["rows"]), "Streamed decoder chain coverage is incomplete")
    counters = {"catalog_stream_count": expected["streams"], "catalog_unique_chains": len(rows),
                "selected_unique_chains": len(rows), "completed_unique_chains": len(rows),
                "qualified_count": len(rows), "failed_count": 0,
                "completed_stream_instances": expected["streams"], "completed_blocks": expected["unique_blocks"]}
    for field, value in counters.items():
        need(report.get(field) == value, f"Streamed decoder coverage count differs: {field}")
    alias = report.get("alias_verification", {})
    alias_counts = {"verified_alias_streams": expected["alias_streams"], "verified_alias_blocks": expected["alias_blocks"],
                    "additional_full_source_hashes": expected["additional_alias_sources"],
                    "native_representative_source_count": expected["native_sources"], "covered_source_count": expected["sources"]}
    for field, value in alias_counts.items():
        need(alias.get(field) == value, f"Streamed decoder actual duplicate source coverage differs: {field}")
    need(alias.get("native_codec_repeated_for_aliases") is False and
         expected["unique_blocks"] + expected["alias_blocks"] == expected["blocks"],
         "Streamed decoder source/block partition differs")
    native_path = path.with_name("native.tsv")
    need(native_path.is_file() and native_path.stat().st_size <= 16 * 1024 * 1024,
         "Streamed decoder native output is missing or exceeds bound")
    native_bytes = native_path.read_bytes()
    lines = native_bytes.decode("utf-8").splitlines()
    header = "id\tstatus\tblocks\tpackets\traw_frames\traw_hashes\tminimum_segment_surplus\tmaximum_block_frames\tmilliseconds\treason"
    need(len(lines) == len(rows) + 1 and lines[0] == header, "Streamed decoder native output is incomplete")
    for index, (row, reference, line) in enumerate(zip(rows, expected["rows"], lines[1:])):
        for field, value in reference.items():
            if field != "source_index":
                need(row.get(field) == value, f"Streamed decoder ordered input identity differs: row {index} {field}")
        need(row.get("native_status") == "OK" and row.get("decoded_blocks") == reference["block_count"] and
             row.get("reason") == "", f"Streamed decoder failed or omitted blocks: row {index}")
        frames, hashes = row.get("raw_frames", []), row.get("raw_hashes_fnv64", [])
        minimum = reference["declared_frames"] + (768 if reference["loop"] else 384)
        need(len(frames) == len(reference["packets"]) and len(hashes) == len(frames) and
             all(type(count) is int and count >= minimum and count % 512 == 0 for count in frames) and
             len(set(frames)) == 1,
             f"Streamed decoder raw layer frames cannot cover the original extent: row {index}")
        need(all(isinstance(value, str) and value.isdecimal() and str(int(value)) == value and
                 0 <= int(value) <= 0xffffffffffffffff for value in hashes),
             f"Streamed decoder raw output hash is invalid: row {index}")
        surplus, maximum = row.get("minimum_segment_surplus"), row.get("maximum_block_raw_frames")
        need(type(surplus) is int and 0 <= surplus <= min(frames) - reference["declared_frames"] and
             type(maximum) is int and 0 < maximum <= min(frames) and maximum % 512 == 0,
             f"Streamed decoder segment availability is invalid: row {index}")
        fields = line.split("\t")
        need(len(fields) == 10 and fields == [str(index), "OK", str(row["decoded_blocks"]),
             ",".join(map(str, row["packets"])), ",".join(map(str, frames)), ",".join(hashes),
             str(surplus), str(maximum), str(row["milliseconds"]), ""],
             f"Streamed decoder report differs from actual native output: row {index}")
    return {"present": True, "one_pass_decode_proven": True, "report_path": str(path.resolve()),
            "report_sha256": sha(data), "native_output_sha256": sha(native_bytes),
            "catalog_identity_current": True, "probe_parser_codec_provenance_current": True,
            "unique_chains_decoded": len(rows), "streams_covered": expected["streams"],
            "sources_covered": expected["sources"], "unique_chain_blocks_decoded": expected["unique_blocks"],
            "duplicate_blocks_reverified": expected["alias_blocks"], "blocks_covered": expected["blocks"],
            "failed_count": 0, "raw_eof_sent": False, "audio_endpoint_opened": False,
            "scope": "Original blocks through the actual runtime parser and persistent real layer codecs; introduction once and fresh loop body once",
            "unproven": ["live quota/ring scheduling", "seek", "restart/loop replay", "cancel/reader lifetime", "speaker/output routing"]}


def audit(verify_original_streams: bool, stream_report: Path | None = None) -> dict:
    analysis = ROOT / "analysis"
    stream_binary = analysis / "audio_catalog.bin"
    summary = json.loads((analysis / "audio_catalog_summary.json").read_text(encoding="utf-8"))
    catalog = read_catalog(stream_binary)
    need(sha(stream_binary.read_bytes()) == summary["binary_sha256"], "Stream catalog summary identity differs")
    streams, sources = list(catalog.streams), list(catalog.sources)
    channels = Counter(s["channels"] for s in streams)
    violations = []
    max_bytes = max_frames = max_compressed = 0
    for index, stream in enumerate(streams):
        if stream["sample_rate"] != 48000 or stream["channels"] not in (1, 2, 4, 6):
            violations.append({"stream": index, "reason": "stream channel/playback rate"})
        if stream["flags"] and stream["loop_start_sample"] != 1:
            violations.append({"stream": index, "reason": "stream one-sample intro"})
    for block in catalog.blocks:
        compressed = 0
        for j in range(block["layer_count"]):
            layer = catalog.layers[block["first_layer"] + j]
            compressed += layer["payload_bytes"] + layer["restored_ff"]
        max_bytes = max(max_bytes, block["raw_bytes"])
        max_frames = max(max_frames, block["frames"])
        max_compressed = max(max_compressed, compressed)
        if not (8 <= block["raw_bytes"] <= 1024 * 1024 and 0 < block["frames"] <= 65536 and
                compressed <= 1024 * 1024 and block["terminal_padding"] <= 63):
            violations.append({"stream": block["stream_index"], "block": block["ordinal"],
                               "reason": "stream reader/parser/packet bound"})
    verified_files = verified_blocks = verified_bytes = 0
    if verify_original_streams:
        originals = ROOT / "Simpsons Game, The (USA)"
        for source in sources:
            path = originals / source["path"]
            data = path.read_bytes()
            need(len(data) == source["file_bytes"] and hashlib.sha256(data).digest() == source["sha256"],
                 f"Original streamed source identity differs: {source['path']}")
            verified_files += 1
            verified_bytes += len(data)
            for stream in streams[source["first_stream"]:source["first_stream"] + source["stream_count"]]:
                header = stream["header_offset"]
                need(data[header:header + 8] == stream["header"], "Original EAAC stream header differs")
                for block in catalog.blocks[stream["first_block"]:stream["first_block"] + stream["block_count"]]:
                    start, length = block["raw_offset"], block["raw_bytes"]
                    raw = data[start:start + length]
                    need(hashlib.sha256(raw).digest() == block["raw_sha256"], "Original EAAC ordered block differs")
                    need(hashlib.sha256(bytes((raw[0] & 0x7F,)) + raw[1:]).digest() == block["normalized_sha256"],
                         "Original EAAC reader normalization differs")
                    verified_blocks += 1
    resident_path, amx_path = analysis / "resident_audio_catalog.json", analysis / "amx_audio_catalog.json"
    resident = json.loads(resident_path.read_text(encoding="utf-8"))
    amx = json.loads(amx_path.read_text(encoding="utf-8"))
    resident_result, amx_result = resident_report(resident, "banks"), resident_report(amx, "payloads")
    image = (analysis / "simpsons.pe").read_bytes()
    factory_words = {f"{a:08X}": image[a - 0x82000000:a - 0x82000000 + 4].hex()
                     for a in (0x823305C8, 0x82330604, 0x82330608, 0x8233060C,
                               0x82330614, 0x82330618, 0x82330620, 0x82330628, 0x821DD434)}
    need(struct.unpack(">f", image[0x1DD434:0x1DD438])[0] == -1000.0,
         "Original reader factory scale constant changed")
    decoded_streams = stream_decode_report(stream_report, catalog)
    return {"schema_version": 1, "scope": "original asset census and native format/storage bounds; no gameplay/caller proof",
            "streamed": {"sources": len(sources), "streams": len(streams), "blocks": len(catalog.blocks),
                         "layers": len(catalog.layers), "channels": dict(channels),
                         "loop_streams": sum(bool(s["flags"]) for s in streams),
                         "max_block_bytes": max_bytes, "max_block_frames": max_frames,
                         "max_compressed_packet_bytes_per_block": max_compressed, "violations": violations,
                         "original_files_verified": verified_files, "original_blocks_verified": verified_blocks,
                         "original_source_bytes_verified": verified_bytes,
                         "decoder_census": ("PROVEN one-pass original streamed decode; live quota/ring/seek/restart/cancel scheduling remains unproven"
                                            if decoded_streams["one_pass_decode_proven"] else
                                            "NOT proven by this structural/hash audit; supply an explicit current streamed decode report"),
                         "decode_evidence": decoded_streams},
            "resident": resident_result, "amx": amx_result,
            "existing_resident_decode": probe_report(ROOT / "build/resident-catalog-decode/report.json", resident_path),
            "existing_amx_decode": probe_report(ROOT / "build/amx-catalog-decode/report.json", amx_path),
            "original_factory_words": factory_words,
            "factory_admission": {
                "original_entry": "82330540",
                "implementation": "runtime/engine_audio_reader.cpp",
                "regression_source": "tests/test_audio_reader_producer.cpp",
                "regression_source_sha256": sha((ROOT / "tests/test_audio_reader_producer.cpp").read_bytes()),
                "contract": "Original caller/stack/voice/allocator ownership and exact two-float-multiply, fctidz and 16-byte ring expression; signed divw and guest extent bounds retained",
                "structural_ring_bounds": {"minimum_bytes": 0, "maximum_aligned_signed_bytes": 0x7FFFFFF0,
                                           "alignment_bytes": 16,
                                           "qualification": "Bounds are further narrowed by actual single-precision products; not every aligned size is numerically reachable"},
                "qualified_fixture_rings": ["47E00", "55280", "4BC80", "1D4C0", "84D0", "2710", "EA60", "810", "800", "80", "10", "0"],
                "independent_test_indices": list(range(14)),
                "small_ring_request_contract": "Fresh request reserves 16 bytes and requires the rounded 2 KiB watermark. Whole original requests on 128/16/2048-byte rings defer and are cancelled by original reset; 2064 bytes reaches the initial boundary and returns an actual claim.",
                "reader_group_count_contract": "No original global 64-group quota; fixture constructs, uses/resets and retires 65 simultaneous original groups. Checked original allocations and distinct live extents bound the native registry.",
                "empty_ring_contract": "Positive sub-byte/denormal duration constructs an empty original reader; empty claim/reset and actual original retirement are exercised. Zero duration bypasses group creation. Nonempty payload claims still require actual contained ring storage.",
                "qualification": "Source-derived admission replaces the earlier two-size whitelist; asset framing alone does not prove every live caller"},
            "unproven_live_paths": [
                "Optional original 82342748 seek/config command metadata is outside the encoded-audio catalogs and guarded zero in native producer",
                "Full stream cancellation, stop/restart, seek and worker scheduling cannot be inferred from asset framing"],
            "source_sha256": {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p.read_bytes()) for p in (
                ROOT / "runtime/engine_audio_owners.cpp", ROOT / "runtime/engine_audio_reader.cpp",
                ROOT / "audio/audio_catalog.cpp", ROOT / "audio/ea_xma_block.cpp",
                ROOT / "audio/native_xma_codec.cpp", ROOT / "audio/xma_source.cpp")}}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-original-streams", action="store_true")
    parser.add_argument("--stream-decode-report", type=Path, help="Validate an explicit complete current pure-decoder report and sibling native.tsv")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = audit(args.verify_original_streams, args.stream_decode_report)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for name in ("streamed", "resident", "amx"):
        item = report[name]
        print(f"{name}: {item.get('streams', item.get('cues'))} sources/cues, {item['blocks']} blocks, "
              f"{len(item['violations'])} native format/storage violations; "
              f"max block {item['max_block_bytes']} bytes / {item['max_block_frames']} frames")
    stream = report["streamed"]
    print(f"Original stream hashes checked: {stream['original_files_verified']} files / "
          f"{stream['original_blocks_verified']} blocks / {stream['original_source_bytes_verified']} bytes")
    if stream["decode_evidence"]["one_pass_decode_proven"]:
        evidence = stream["decode_evidence"]
        print(f"Pure streamed decode proven: {evidence['unique_chains_decoded']} unique chains / "
              f"{evidence['streams_covered']} streams / {evidence['blocks_covered']} blocks; live scheduling unproven")
    if args.output:
        print(f"Report: {args.output}")
    if any(report[name]["violations"] for name in ("streamed", "resident", "amx")):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
