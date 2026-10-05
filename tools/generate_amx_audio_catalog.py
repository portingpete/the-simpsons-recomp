#!/usr/bin/env python3
"""Inventory EA-XMA cues embedded in every original AMX resource.

An AMX payload can be copied into several STR resources. The catalog keeps one
cue list per exact payload SHA-256 and records every containing STR placement.
The structural checks do not establish that an XMA decoder can play each cue.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from extract_resource import select_payload
from generate_resident_audio_catalog import cue_at


# SIMAMX01 is little-endian. Offsets in cue/block rows address the decoded
# original AMX payload. String offsets address the trailing UTF-8 name blob.
BIN_MAGIC = b"SIMAMX01"
BIN_VERSION = 2
BIN_HEADER = struct.Struct("<8sIIIIQQQQQ")
BIN_PAYLOAD = struct.Struct("<QIIIIIII32s32s")
BIN_CUE = struct.Struct("<11I8s32s")
BIN_BLOCK = struct.Struct("<9I32s")


def binary_catalog(payloads: list[dict]) -> bytes:
    payload_rows = bytearray()
    cue_rows = bytearray()
    block_rows = bytearray()
    names = bytearray()
    for payload_index, payload in enumerate(payloads):
        name = payload["name"].encode("utf-8")
        name_offset = len(names)
        names.extend(name)
        first_cue = len(cue_rows) // BIN_CUE.size
        for ordinal, cue in enumerate(payload["cues"]):
            cue_index = len(cue_rows) // BIN_CUE.size
            first_block = len(block_rows) // BIN_BLOCK.size
            for block in cue["blocks"]:
                block_rows.extend(BIN_BLOCK.pack(
                    cue_index, block["ordinal"], block["offset"], block["bytes"],
                    block["frames"], block["selector"], block["codec_rate"],
                    block["restored_ff"], block["payload_bytes"],
                    bytes.fromhex(block["sha256"])))
            cue_rows.extend(BIN_CUE.pack(
                payload_index, ordinal, cue["header_offset"], cue["end_offset"],
                cue["frames"], cue["playback_rate"], cue["channels"],
                cue["loop_start_sample"], int(cue["loop"]), first_block,
                len(cue["blocks"]), bytes.fromhex(cue["header_hex"]),
                bytes.fromhex(cue["cue_sha256"])))
        payload_rows.extend(BIN_PAYLOAD.pack(
            name_offset, len(name), payload["payload_bytes"],
            payload["metadata_bytes"], payload["prefix_bytes"],
            payload["audio_end_offset"], first_cue, len(payload["cues"]),
            bytes.fromhex(payload["payload_sha256"]),
            bytes.fromhex(payload["audio_sha256"])))
    counts = (len(payload_rows) // BIN_PAYLOAD.size,
              len(cue_rows) // BIN_CUE.size,
              len(block_rows) // BIN_BLOCK.size)
    need(all(value <= 0xFFFFFFFF for value in counts), "AMX binary count overflow")
    payload_offset = BIN_HEADER.size
    cue_offset = payload_offset + len(payload_rows)
    block_offset = cue_offset + len(cue_rows)
    string_offset = block_offset + len(block_rows)
    header = BIN_HEADER.pack(BIN_MAGIC, BIN_VERSION, *counts, payload_offset,
                             cue_offset, block_offset, string_offset, len(names))
    return b"".join((header, payload_rows, cue_rows, block_rows, names))


def need(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError(reason)


def scan_payload(data: bytes) -> dict:
    """Validate one AMX envelope and its contiguous EA-XMA cue run."""
    need(len(data) >= 64, "AMX payload is shorter than its envelope")
    version, body_bytes, metadata_bytes, repeated_body_bytes = struct.unpack_from(">4I", data)
    need(version == 9 and body_bytes == repeated_body_bytes and
         metadata_bytes + body_bytes + 64 == len(data),
         "AMX envelope length/version differs from original resources")

    cues = []
    for offset, value in enumerate(data):
        if value != 3:
            continue
        candidate = cue_at(data, offset)
        if candidate is not None:
            cues.append(candidate)
    need(cues and cues[0]["header_offset"] >= metadata_bytes + 64,
         "AMX has no bounded audio cue run")
    need(all(previous["end_offset"] == following["header_offset"]
             for previous, following in zip(cues, cues[1:])),
         "AMX EA-XMA candidates have an overlap or unexplained gap")
    for ordinal, cue in enumerate(cues):
        cue["ordinal"] = ordinal
        cue["cue_sha256"] = hashlib.sha256(
            data[cue["header_offset"]:cue["end_offset"]]).hexdigest()

    return {
        "version": version,
        "body_bytes": body_bytes,
        "metadata_bytes": metadata_bytes,
        "prefix_bytes": cues[0]["header_offset"],
        "audio_end_offset": cues[-1]["end_offset"],
        "trailing_bytes": len(data) - cues[-1]["end_offset"],
        "cue_count": len(cues),
        "block_count": sum(len(cue["blocks"]) for cue in cues),
        "cues": cues,
    }


def report_occurrences(report: dict) -> list[dict]:
    need(report.get("schema_version") == 1 and isinstance(report.get("files"), list),
         "Unsupported original asset inspection report")
    occurrences = []
    for file in report["files"]:
        if file.get("extension") != ".str":
            continue
        for entry in file["inspection"].get("entries", []):
            for chunk in entry.get("chunks", []):
                if chunk.get("type_name") != "AMX":
                    continue
                occurrences.append({
                    "container_path": file["path"],
                    "container_sha256": file["sha256"],
                    "entry_index": entry["index"],
                    "name": chunk["name"],
                    "source_path": chunk.get("source_path", ""),
                    "payload_decoded_offset": chunk["payload_decoded_offset"],
                    "payload_bytes": chunk["payload_size"],
                    "payload_sha256": chunk["payload_sha256"],
                })
    occurrences.sort(key=lambda row: (
        row["container_path"].casefold(), row["container_path"],
        row["entry_index"], row["name"].casefold(), row["name"]))
    keys = {(row["container_path"], row["entry_index"], row["name"])
            for row in occurrences}
    need(len(keys) == len(occurrences), "Duplicate AMX resource occurrence")
    return occurrences


def generate(root: Path, report_path: Path, output: Path,
             binary: Path | None = None) -> dict:
    root = Path(root)
    report = json.loads(Path(report_path).read_text(encoding="utf-8"))
    occurrences = report_occurrences(report)
    need(occurrences, "Asset report contains no AMX resources")
    payloads: dict[str, dict] = {}
    for occurrence in occurrences:
        source = root / occurrence["container_path"]
        data, provenance = select_payload(
            source, occurrence["entry_index"], occurrence["name"], root)
        need(provenance["source"]["sha256"] == occurrence["container_sha256"] and
             provenance["resource"]["type_name"] == "AMX" and
             provenance["resource"]["payload_decoded_offset"] ==
             occurrence["payload_decoded_offset"] and
             len(data) == occurrence["payload_bytes"] and
             hashlib.sha256(data).hexdigest() == occurrence["payload_sha256"],
             "AMX original source/report provenance differs")
        digest = occurrence["payload_sha256"]
        if digest not in payloads:
            parsed = scan_payload(data)
            payloads[digest] = {
                "name": occurrence["name"],
                "payload_sha256": digest,
                "audio_sha256": hashlib.sha256(
                    data[parsed["prefix_bytes"]:parsed["audio_end_offset"]]).hexdigest(),
                "payload_bytes": len(data),
                **parsed,
                "occurrences": [],
            }
        else:
            need(payloads[digest]["payload_bytes"] == len(data) and
                 payloads[digest]["name"] == occurrence["name"],
                 "AMX identical payload has inconsistent resource identity")
        payloads[digest]["occurrences"].append({
            key: occurrence[key] for key in (
                "container_path", "container_sha256", "entry_index", "name",
                "source_path", "payload_decoded_offset")})

    ordered = sorted(payloads.values(), key=lambda row: (
        row["name"].casefold(), row["name"], row["payload_sha256"]))
    signatures = Counter(
        tuple(block["sha256"] for block in cue["blocks"])
        for payload in ordered for cue in payload["cues"])
    result = {
        "schema_version": 1,
        "format": "original_AMX_EAAC_structural_inventory",
        "validation": "exact_STR_provenance_envelope_contiguous_cues_and_block_framing_not_XMA_decode",
        "payload_count": len(ordered),
        "resource_occurrence_count": len(occurrences),
        "cue_count": sum(payload["cue_count"] for payload in ordered),
        "cue_occurrence_count": sum(payload["cue_count"] * len(payload["occurrences"])
                                    for payload in ordered),
        "block_count": sum(payload["block_count"] for payload in ordered),
        "unique_block_sequence_count": len(signatures),
        "loop_cue_count": sum(cue["loop"] for payload in ordered for cue in payload["cues"]),
        "payloads": ordered,
    }
    output = Path(output)
    binary = Path(binary) if binary is not None else output.with_suffix(".bin")
    need(output.absolute() != binary.absolute(), "AMX JSON and binary output paths overlap")
    packed = binary_catalog(ordered)
    binary.parent.mkdir(parents=True, exist_ok=True)
    temporary_binary = binary.with_name(binary.name + ".tmp")
    temporary_binary.write_bytes(packed)
    temporary_binary.replace(binary)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    temporary.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n",
                         encoding="utf-8")
    temporary.replace(output)
    return result


def main() -> None:
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=project / "Simpsons Game, The (USA)")
    parser.add_argument("--assets-report", type=Path, default=project / "analysis/assets.json")
    parser.add_argument("--output", type=Path, default=project / "analysis/amx_audio_catalog.json")
    parser.add_argument("--binary", type=Path, default=None)
    args = parser.parse_args()
    result = generate(args.root, args.assets_report, args.output, args.binary)
    print(f"AMX: {result['payload_count']} unique payloads, "
          f"{result['resource_occurrence_count']} STR placements, "
          f"{result['cue_count']} payload cues, "
          f"{result['cue_occurrence_count']} placed cues")


if __name__ == "__main__":
    main()
