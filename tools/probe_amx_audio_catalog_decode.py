#!/usr/bin/env python3
"""Smoke-decode all exact original AMX cue blocks with the owned XMA codec.

The probe verifies each original STR and AMX hash, sends unchanged compressed
packets, never sends EOF, and writes only aggregate hashes/frame counts. This
does not prove original guest call flow or mixer behavior.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from extract_resource import select_payload
from generate_amx_audio_catalog import scan_payload


ROOT = Path(__file__).resolve().parents[1]
ORIGINALS = ROOT / "Simpsons Game, The (USA)"
CATALOG = ROOT / "analysis/amx_audio_catalog.json"
SOURCE = ROOT / "tools/probe_resident_catalog_decode.cpp"
INSTALL = ROOT / "build/audio-codec/install"
OUT = ROOT / "build/amx-catalog-decode"
DLLS = ("avcodec-simpsonsxma-62.dll", "avutil-simpsonsxma-60.dll", "libwinpthread-1.dll")


def need(value: bool, reason: str) -> None:
    if not value:
        raise RuntimeError(reason)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_manifest(catalog: dict, path: Path) -> list[dict]:
    records = []
    with path.open("wb") as output:
        output.write(b"RDEC0001" + struct.pack("<I", catalog["cue_count"]))
        for payload_info in catalog["payloads"]:
            alias = payload_info["occurrences"][0]
            container = ORIGINALS / alias["container_path"]
            payload, provenance = select_payload(
                container, alias["entry_index"], alias["name"], ORIGINALS)
            need(provenance["source"]["sha256"] == alias["container_sha256"] and
                 len(payload) == payload_info["payload_bytes"] and
                 digest(payload) == payload_info["payload_sha256"],
                 f"Original AMX source changed: {alias['container_path']}")
            parsed = scan_payload(payload)
            need(parsed["cue_count"] == payload_info["cue_count"] and
                 parsed["block_count"] == payload_info["block_count"],
                 f"AMX cue structure changed: {alias['container_path']}")
            for cue in payload_info["cues"]:
                at, end = cue["header_offset"], cue["end_offset"]
                need(payload[at:at + 8].hex() == cue["header_hex"] and
                     digest(payload[at:end]) == cue["cue_sha256"] and
                     len(cue["blocks"]) == 1,
                     f"Original AMX cue identity differs: {alias['container_path']}:{at}")
                block = cue["blocks"][0]
                start, length = block["offset"], block["bytes"]
                raw = payload[start:start + length]
                need(len(raw) == length and digest(raw) == block["sha256"],
                     f"Original AMX block changed: {alias['container_path']}:{start}")
                size, frames, layer = struct.unpack_from(">III", raw)
                need(size == length and frames == block["frames"] and
                     layer >> 2 == length - 8 and layer & 3 == block["selector"] and
                     block["payload_bytes"] == length - 12 and
                     block["restored_ff"] == (-block["payload_bytes"]) % 2048,
                     f"Original AMX block framing changed: {alias['container_path']}:{start}")
                packets = raw[12:] + b"\xff" * block["restored_ff"]
                need(packets and len(packets) % 2048 == 0 and len(packets) <= 16 * 1024 * 1024,
                     f"AMX packet buffer exceeds probe bound: {alias['container_path']}:{start}")
                index = len(records)
                output.write(struct.pack("<5I", index, cue["channels"], block["codec_rate"],
                                         len(packets) // 2048, int(cue["loop"])))
                output.write(packets)
                records.append({
                    "payload_sha256": payload_info["payload_sha256"],
                    "name": payload_info["name"],
                    "canonical_container_path": alias["container_path"],
                    "cue_ordinal": cue["ordinal"],
                    "cue_offset": at,
                    "cue_loop": cue["loop"],
                    "block_sha256": block["sha256"],
                    "block_bytes": length,
                    "channels": cue["channels"],
                    "playback_rate": cue["playback_rate"],
                    "codec_rate": block["codec_rate"],
                    "declared_frames": frames,
                    "restored_ff": block["restored_ff"],
                    "packet_count": len(packets) // 2048,
                })
    need(len(records) == catalog["cue_count"], "AMX manifest cue count changed")
    return records


def main() -> None:
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    need(catalog["schema_version"] == 1 and catalog["cue_count"] == 254,
         "AMX catalog version/count changed")
    OUT.mkdir(parents=True, exist_ok=True)
    verified = subprocess.run(
        [sys.executable, "-B", str(ROOT / "tools/build_native_audio_codec.py"), "--verify"],
        cwd=ROOT, capture_output=True, text=True, timeout=90)
    need(verified.returncode == 0, f"Owned codec verification failed: {verified.stderr[-1000:]}")
    spec = importlib.util.spec_from_file_location("amx_probe_toolchain", ROOT / "tests/test_host_fp.py")
    need(spec is not None and spec.loader is not None, "Host compiler helper unavailable")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    compiler, environment = helper.toolchain()
    with tempfile.TemporaryDirectory(prefix="simpsons-amx-decode-") as temporary:
        work = Path(temporary)
        manifest = work / "manifest.bin"
        records = make_manifest(catalog, manifest)
        executable = work / "probe.exe"
        command = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/fp:strict",
                   "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", f"/I{ROOT}", f"/I{INSTALL/'include'}",
                   str(SOURCE), str(ROOT / "audio/native_xma_codec.cpp"), f"/Fo{work.as_posix()}/",
                   f"/Fe{executable}", "/link", f"/LIBPATH:{INSTALL/'lib'}",
                   "avcodec-simpsonsxma.lib", "avutil-simpsonsxma.lib", "/INCREMENTAL:NO"]
        built = subprocess.run(command, cwd=work, env=environment,
                               capture_output=True, text=True, timeout=120)
        (OUT / "compile.log").write_text(built.stdout + built.stderr, encoding="utf-8")
        need(built.returncode == 0, f"AMX probe compile failed: {(built.stdout+built.stderr)[-2000:]}")
        for name in DLLS:
            shutil.copyfile(INSTALL / "bin" / name, work / name)
        environment["PATH"] = str(work) + os.pathsep + environment.get("PATH", "")
        decoded = subprocess.run([str(executable), str(manifest)], cwd=work, env=environment,
                                 capture_output=True, text=True, timeout=600)
        (OUT / "native.log").write_text(decoded.stdout + decoded.stderr, encoding="utf-8")
        need(decoded.returncode == 0, f"AMX native probe failed: {decoded.stderr[-1000:]}")

    lines = decoded.stdout.splitlines()
    need(lines[0] == "id\tstatus\traw_frames\traw_hash\trestart_frames\trestart_hash\treason" and
         len(lines) == len(records) + 1, "AMX native probe returned incomplete rows")
    failures = []
    for expected, (line, record) in enumerate(zip(lines[1:], records)):
        fields = line.split("\t")
        need(len(fields) == 7 and int(fields[0]) == expected,
             "AMX native probe row identity changed")
        record["native_status"] = fields[1]
        record["raw_frames"] = int(fields[2])
        record["raw_hash_fnv64"] = fields[3]
        record["restart_frames"] = int(fields[4])
        record["restart_hash_fnv64"] = fields[5]
        record["reason"] = fields[6]
        record["required_frames"] = record["declared_frames"] + 384
        record["surplus_frames"] = record["raw_frames"] - record["required_frames"]
        record["conservative_ring_frames"] = (record["declared_frames"] + 384 + 4096 + 511) & ~511
        record["qualified"] = (fields[1] == "OK" and record["surplus_frames"] >= 0 and
                                (not record["cue_loop"] or
                                 record["restart_frames"] == record["raw_frames"] and
                                 record["restart_hash_fnv64"] == record["raw_hash_fnv64"]))
        if not record["qualified"]:
            failures.append(record)
    report = {
        "probe_version": 1,
        "scope": "all_254_original_AMX_payload_cues",
        "catalog_sha256": digest(CATALOG.read_bytes()),
        "probe_source_sha256": digest(SOURCE.read_bytes()),
        "codec_source_sha256": digest((ROOT / "audio/native_xma_codec.cpp").read_bytes()),
        "codec_dll_sha256": {name: digest((INSTALL / "bin" / name).read_bytes()) for name in DLLS},
        "raw_eof_sent": False,
        "pcm_files_written": False,
        "block_count": len(records),
        "qualified_count": len(records) - len(failures),
        "failed_count": len(failures),
        "records": records,
    }
    report_path = OUT / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for record in failures[:20]:
        print(f"FAIL {record['name']}:{record['cue_offset']} "
              f"raw={record['raw_frames']} required={record['required_frames']} "
              f"reason={record['reason']}")
    if len(failures) > 20:
        print(f"... {len(failures)-20} further failures in report")
    print(f"{len(records)-len(failures)}/{len(records)} AMX blocks passed; report {report_path}")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        print(f"AMX decode probe failed: {error}", file=sys.stderr)
        raise SystemExit(1)
