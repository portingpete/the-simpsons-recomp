#!/usr/bin/env python3
"""Generate an isolated relocated original SNU using the unchanged catalog producer.

This qualifies the native catalog/decoder path, not an original-console file
open, sample seek, guest reader setup, audio device, or gameplay route.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ORIGINAL_PATH = "audiostreams/cb_xxx_0/d_chcb_xxx_0006700.exa.snu"
ORIGINAL_SHA = "1034445fb02bdb156570b4d4ae82157ee99816986ae98f8fa56b685ad2172bba"
RELOCATED_OFFSET = 0x20000000


def need(ok, reason):
    if not ok:
        raise ValueError(reason)


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def identify(path):
    return {"path": str(path.resolve(strict=True)), "bytes": path.stat().st_size,
            "sha256": digest(path)}


def exclusive_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, sort_keys=True, indent=2)
        output.write("\n")


def sparse(output):
    # Only the newly created owned fixture handle is affected.
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    call = kernel.DeviceIoControl
    call.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
                     ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
                     ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    call.restype = ctypes.c_int
    returned = ctypes.c_uint32()
    need(call(msvcrt.get_osfhandle(output.fileno()), 0x900C4, None, 0,
              None, 0, ctypes.byref(returned), None),
         "Could not mark isolated SNU fixture sparse")


def prepare(project, base):
    tools = project / "tools"
    sys.path.insert(0, str(tools))
    from generate_audio_catalog import generate, read_catalog
    from inspect_assets import inspect_snu

    original = project / "Simpsons Game, The (USA)" / ORIGINAL_PATH
    raw = original.read_bytes()
    need(hashlib.sha256(raw).hexdigest() == ORIGINAL_SHA,
         "Authenticated original dialogue fixture changed")
    info = inspect_snu(raw)
    start = info["audio"]["audio_offset"]
    need(len(raw) == 24528 and start == 3472 and
         info["audio"]["audio_size"] == 21056 and
         info["audio"]["block_count"] == 14 and
         info["header"]["samples"] == 67328 and
         info["header"]["channels"] == 1 and not info["header"]["loop"],
         "Original dialogue framing changed")
    authority = {"original": identify(original),
                 "catalog_producer": identify(tools / "generate_audio_catalog.py"),
                 "inspector": identify(tools / "inspect_assets.py"),
                 "fixture_tool": identify(Path(__file__)),
                 "relocated_offset": RELOCATED_OFFSET}
    receipt = base / "fixture-provenance-v1.json"
    if base.exists():
        need(receipt.is_file(), "Preserve incomplete fixture preparation; use a new output directory")
        saved = json.loads(receipt.read_text(encoding="utf-8"))
        need(saved["authority"] == authority, "Fixture producer/source authority changed")
        for item in saved["files"]:
            need(identify(Path(item["path"])) == item, "Preserved fixture identity changed")
        return saved
    base.mkdir(parents=True, exist_ok=False)
    files = []
    for label in ("original", "relocated"):
        root = base / label / "source"
        source = root / "audiostreams/dialogue.exa.snu"
        source.parent.mkdir(parents=True, exist_ok=False)
        if label == "original":
            with source.open("xb") as output:
                output.write(raw)
        else:
            prefix = bytearray(raw[:start])
            struct.pack_into(">I", prefix, 8, RELOCATED_OFFSET)
            with source.open("xb") as output:
                sparse(output)
                output.write(prefix)
                output.seek(RELOCATED_OFFSET)
                output.write(raw[start:])
        binary = base / label / "catalog.bin"
        summary = base / label / "catalog-summary.json"
        # This really reads the complete file, inspects its framing, hashes it,
        # and serializes every row with the unmodified production generator.
        result = generate(root, None, binary, summary)
        parsed = read_catalog(binary)
        need((result["source_count"], result["stream_count"],
              result["block_count"], result["layer_count"]) == (1, 1, 14, 14),
             "Generated original dialogue inventory changed")
        source_row, stream_row = parsed.sources[0], parsed.streams[0]
        expected_start = start if label == "original" else RELOCATED_OFFSET
        need(source_row["file_bytes"] == expected_start + len(raw) - start and
             stream_row["header_offset"] == 16 and
             stream_row["audio_offset"] == expected_start and
             stream_row["audio_bytes"] == len(raw) - start and
             source_row["sha256"].hex() == digest(source),
             "Generated source extent/identity differs from actual fixture")
        files.extend(identify(p) for p in (source, binary, summary))

    low = read_catalog(base / "original/catalog.bin")
    high = read_catalog(base / "relocated/catalog.bin")
    shift = RELOCATED_OFFSET - start
    for table, fields in (("streams", ("audio_offset",)),
                          ("blocks", ("raw_offset",)),
                          ("layers", ("header_offset", "payload_offset"))):
        for a, b in zip(getattr(low, table), getattr(high, table), strict=True):
            for field in a:
                expected = a[field] + shift if field in fields else a[field]
                need(b[field] == expected, f"Relocation changed encoded {table}.{field}")
    # Fresh independent corruption cases; their extents have no memory budget
    # implication because the small catalog only describes a source file.
    negative = base / "negative"
    negative.mkdir(exist_ok=False)
    # Keep unrelated negatives inside the existing source-size policy so its
    # early rejection cannot hide the actual structural check being tested.
    data = (base / "original/catalog.bin").read_bytes()
    source_at, stream_at = struct.unpack_from("<QQ", data, 28)
    variants = {
        "zero_source_extent": (source_at + 24, "<Q", 0),
        "source_shorter_than_audio": (source_at + 24, "<Q", len(raw) - 1),
        "audio_offset_wrap": (stream_at + 16, "<Q", (1 << 64) - 1),
        "header_outside_source": (stream_at + 8, "<Q", len(raw)),
        "source_kind_invalid": (source_at + 20, "<I", 3),
    }
    for name, (at, layout, value) in variants.items():
        mutated = bytearray(data)
        struct.pack_into(layout, mutated, at, value)
        path = negative / f"{name}.bin"
        with path.open("xb") as output:
            output.write(mutated)
        files.append(identify(path))
    saved = {"schema_version": 1, "authority": authority, "files": files,
             "qualification": "actual_generator_original_payload_relocation_not_original_game_setup",
             "source": {"original_audio_offset": start,
                        "relocated_audio_offset": RELOCATED_OFFSET,
                        "audio_bytes": 21056, "blocks": 14, "frames": 67328},
             "cases": {"positive": ["original", "relocated", "relocated_reopen"],
                       "structural_corruption": list(variants)}}
    exclusive_json(receipt, saved)
    return saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--test", type=Path)
    args = parser.parse_args()
    project = (args.project or Path(__file__).resolve().parents[1]).resolve(strict=True)
    base = (args.output or project / "build/audio-catalog-extent-fixture/v1").absolute()
    need(not base.resolve().is_relative_to(project / "Simpsons Game, The (USA)"),
         "Fixture must stay outside original assets")
    result = prepare(project, base)
    print(json.dumps({"fixture": str(base), "receipt": identify(base / "fixture-provenance-v1.json"),
                      "qualification": result["qualification"]}), flush=True)
    if args.test:
        command = [str(args.test.resolve(strict=True)), str(base)]
        print("EXTENT_NATIVE_COMMAND " + json.dumps(command), flush=True)
        return subprocess.run(command, check=False, timeout=60).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
