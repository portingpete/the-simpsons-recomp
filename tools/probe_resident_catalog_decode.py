#!/usr/bin/env python3
"""Smoke-decode exact catalogued SBK blocks with the owned native XMA codec.

Reads original containers and checks catalog hashes before handing packet bytes
to a temporary native harness. No PCM, changed game asset, or game process is
created. ``--all`` extends the same procedure to every distinct catalog block.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
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
from generate_resident_audio_catalog import bank_envelope

ROOT = Path(__file__).resolve().parents[1]
INSTALL = ROOT / "build/audio-codec/install"
OUT = ROOT / "build/resident-catalog-decode"
SOURCE = ROOT / "tools/probe_resident_catalog_decode.cpp"
CATALOG = ROOT / "analysis/resident_audio_catalog.json"
ORIGINALS = ROOT / "Simpsons Game, The (USA)"
DLLS = ("avcodec-simpsonsxma-62.dll", "avutil-simpsonsxma-60.dll", "libwinpthread-1.dll")

# Exact original container, cue offset. These include previously uncertified
# nonloop sounds at every observed playback rate, stereo, restored FF, both
# resident loop shapes, and the largest nonloop cue in the catalog.
REPRESENTATIVES = (
    ("bargainbin/bargainbin.str", 8920),
    ("bargainbin/bargainbin/challenge_mode/challenge_mode_design.str", 985405),
    ("cheater/cheater.str", 270909),
    ("brt/brt_global.str", 4348913),
    ("dayofthedolphins/dayofthedolphins_global.str", 3638409),
    ("eighty_bites/eighty_bites/story_mode/story_mode_design.str", 376764),
    ("bigsuperhappy/bigsuperhappy.str", 426974),
    ("colossaldonut/colossaldonut/zone01.str", 242088),
    ("loc/loc_global.str", 1830094),
    ("bargainbin/bargainbin_global.str", 1793096),
    ("dayspringfieldstoodstill/dayspringfieldstoodstill/zone02.str", 1037936),
)


def need(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def choose(catalog: dict, all_blocks: bool, max_blocks: int | None) -> list[tuple[dict, dict, dict]]:
    banks = catalog["banks"]
    selected = []
    if all_blocks:
        seen = set()
        for bank in banks:
            for cue in bank["cues"]:
                for block in cue["blocks"]:
                    key = (block["sha256"], cue["channels"], block["codec_rate"], cue["loop"])
                    if key not in seen:
                        seen.add(key)
                        selected.append((bank, cue, block))
                    if max_blocks is not None and len(selected) >= max_blocks:
                        return selected
    else:
        by_path = {bank["container_path"]: bank for bank in banks}
        for path, offset in REPRESENTATIVES:
            bank = by_path[path]
            cue = next(c for c in bank["cues"] if c["header_offset"] == offset)
            selected.extend((bank, cue, block) for block in cue["blocks"])
    need(bool(selected), "No catalog blocks selected")
    return selected


def manifest(selected: list[tuple[dict, dict, dict]], manifest_path: Path) -> list[dict]:
    by_bank: dict[str, list[tuple[int, dict, dict, dict]]] = defaultdict(list)
    for index, (bank, cue, block) in enumerate(selected):
        by_bank[bank["container_path"]].append((index, bank, cue, block))
    records: list[dict | None] = [None] * len(selected)
    # Extract one bank at a time. A manifest can be processed by one codec
    # process even for thousands of blocks, and duplicate blocks are skipped.
    packets: list[bytes | None] = [None] * len(selected)
    for path, items in by_bank.items():
        bank = items[0][1]
        original = ORIGINALS / path
        payload, provenance = select_payload(original, bank["entry_index"], bank["name"], ORIGINALS)
        need(provenance["source"]["sha256"] == bank["container_sha256"] and
             digest(payload) == bank["payload_sha256"] and len(payload) == bank["payload_bytes"],
             f"Original resident bank identity differs: {path}")
        envelope = bank_envelope(payload)
        need(all(envelope[key] == bank[key] for key in ("metadata_bytes", "audio_offset", "audio_bytes")),
             f"Original resident bank envelope differs: {path}")
        for index, _, cue, block in items:
            header_at = cue["header_offset"]
            need(payload[header_at:header_at + 8].hex() == cue["header_hex"],
                 f"Original resident cue header differs: {path}:{header_at}")
            at, length = block["offset"], block["bytes"]
            original_block = payload[at:at + length]
            need(len(original_block) == length and digest(original_block) == block["sha256"],
                 f"Original resident block differs: {path}:{at}")
            size, declared, layer = struct.unpack_from(">III", original_block)
            need(size == length and declared == block["frames"] and
                 layer >> 2 == length - 8 and layer & 3 == block["selector"] and
                 block["payload_bytes"] == length - 12 and
                 block["restored_ff"] == (-block["payload_bytes"]) % 2048,
                 f"Resident block framing differs: {path}:{at}")
            packet_bytes = original_block[12:] + b"\xff" * block["restored_ff"]
            need(len(packet_bytes) and len(packet_bytes) % 2048 == 0 and
                 len(packet_bytes) <= 16 * 1024 * 1024,
                 f"Resident packet buffer exceeds bound: {path}:{at}")
            packets[index] = packet_bytes
            records[index] = {
                "container_path": path, "bank": bank["name"],
                "bank_sha256": bank["payload_sha256"],
                "cue_offset": header_at, "cue_loop": cue["loop"],
                "cue_loop_start": cue["loop_start_sample"],
                "block_ordinal": block["ordinal"], "block_offset": at,
                "block_sha256": block["sha256"], "channels": cue["channels"],
                "playback_rate": cue["playback_rate"], "codec_rate": block["codec_rate"],
                "declared_frames": block["frames"], "restored_ff": block["restored_ff"],
                "packet_count": len(packet_bytes) // 2048,
            }
    with manifest_path.open("wb") as output:
        output.write(b"RDEC0001" + struct.pack("<I", len(selected)))
        for index, row in enumerate(records):
            need(row is not None and packets[index] is not None, "Incomplete probe manifest")
            output.write(struct.pack("<5I", index, row["channels"], row["codec_rate"],
                                     row["packet_count"], int(row["cue_loop"])))
            output.write(packets[index])
    return records


def run_native(binary: Path, input_path: Path, env: dict[str, str]) -> str:
    result = subprocess.run([str(binary), str(input_path)], cwd=binary.parent, env=env,
                            capture_output=True, text=True, timeout=600)
    (OUT / "native.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    need(result.returncode == 0, f"Native decoder probe failed: {result.stderr[-1000:]}")
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="Probe all distinct catalog blocks")
    parser.add_argument("--max-blocks", type=int, help="Limit --all to the first N distinct blocks")
    args = parser.parse_args()
    need(args.max_blocks is None or args.all and args.max_blocks > 0,
         "--max-blocks requires --all and a positive count")
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    selected = choose(catalog, args.all, args.max_blocks)
    OUT.mkdir(parents=True, exist_ok=True)
    verified = subprocess.run([sys.executable, "-B", str(ROOT / "tools/build_native_audio_codec.py"), "--verify"],
                              cwd=ROOT, capture_output=True, text=True, timeout=90)
    need(verified.returncode == 0, f"Owned codec verification failed: {verified.stderr[-1000:]}")
    spec = importlib.util.spec_from_file_location("resident_probe_toolchain", ROOT / "tests/test_host_fp.py")
    need(spec is not None and spec.loader is not None, "Host compiler helper unavailable")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    compiler, env = helper.toolchain()
    with tempfile.TemporaryDirectory(prefix="simpsons-resident-decode-") as temporary:
        work = Path(temporary)
        input_path = work / "manifest.bin"
        records = manifest(selected, input_path)
        executable = work / "probe.exe"
        command = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/fp:strict",
                   "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", f"/I{ROOT}", f"/I{INSTALL/'include'}",
                   str(SOURCE), str(ROOT / "audio/native_xma_codec.cpp"), f"/Fo{work.as_posix()}/",
                   f"/Fe{executable}", "/link", f"/LIBPATH:{INSTALL/'lib'}",
                   "avcodec-simpsonsxma.lib", "avutil-simpsonsxma.lib", "/INCREMENTAL:NO"]
        built = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True, timeout=120)
        (OUT / "compile.log").write_text(built.stdout + built.stderr, encoding="utf-8")
        need(built.returncode == 0, f"Native probe compile failed: {(built.stdout+built.stderr)[-2000:]}")
        for name in DLLS:
            shutil.copyfile(INSTALL / "bin" / name, work / name)
        env["PATH"] = str(work) + os.pathsep + env.get("PATH", "")
        output = run_native(executable, input_path, env)
    lines = output.splitlines()
    need(lines[0] == "id\tstatus\traw_frames\traw_hash\trestart_frames\trestart_hash\treason" and
         len(lines) == len(records) + 1, "Native decoder returned incomplete rows")
    failures = []
    for expected, (line, record) in enumerate(zip(lines[1:], records)):
        fields = line.split("\t")
        need(len(fields) == 7 and int(fields[0]) == expected, "Native decoder row identity changed")
        record["native_status"] = fields[1]
        record["raw_frames"] = int(fields[2])
        record["raw_hash_fnv64"] = fields[3]
        record["restart_frames"] = int(fields[4])
        record["restart_hash_fnv64"] = fields[5]
        record["reason"] = fields[6]
        record["required_frames"] = record["declared_frames"] + 384
        record["surplus_frames"] = record["raw_frames"] - record["required_frames"]
        record["qualified"] = (fields[1] == "OK" and record["surplus_frames"] >= 0 and
                                (not record["cue_loop"] or
                                 record["restart_frames"] == record["raw_frames"] and
                                 record["restart_hash_fnv64"] == record["raw_hash_fnv64"]))
        if not record["qualified"]:
            failures.append(record)
    report = {
        "probe_version": 1, "scope": "all_unique_blocks" if args.all else "representative_blocks",
        "catalog_sha256": digest(CATALOG.read_bytes()),
        "probe_source_sha256": digest(SOURCE.read_bytes()),
        "codec_source_sha256": digest((ROOT / "audio/native_xma_codec.cpp").read_bytes()),
        "codec_dll_sha256": {name: digest((INSTALL / "bin" / name).read_bytes()) for name in DLLS},
        "raw_eof_sent": False, "pcm_files_written": False,
        "block_count": len(records), "qualified_count": len(records) - len(failures),
        "failed_count": len(failures), "records": records,
    }
    report_path = OUT / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    shown = failures[:20] if args.all else records
    for record in shown:
        print(f"{record['bank']}:{record['cue_offset']} block{record['block_ordinal']} "
              f"rate={record['playback_rate']}/{record['codec_rate']} "
              f"raw={record['raw_frames']} required={record['required_frames']} "
              f"surplus={record['surplus_frames']} {'PASS' if record['qualified'] else 'FAIL'}")
    if args.all and len(failures) > len(shown):
        print(f"... {len(failures)-len(shown)} further failures in report")
    print(f"{len(records)-len(failures)}/{len(records)} blocks passed; report {report_path}")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        print(f"Resident decode probe failed: {error}", file=sys.stderr)
        raise SystemExit(1)
