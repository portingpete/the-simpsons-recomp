#!/usr/bin/env python3
"""Decode exact shipped streamed block chains with the current owned native codec.

No game process, audio endpoint, PCM file, EOF, or changed asset. Each real mono/
stereo layer preserves codec state across blocks. The original one-sample loop
introduction and body use separate contexts (the runtime's existing fresh path).
This does not test loop replay, seeking, cancellation, reader claims or ring/quota
scheduling. --all covers every unique ordered chain and records duplicate aliases.
"""
from __future__ import annotations
import argparse
from collections import Counter
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
import time

sys.dont_write_bytecode = True
from generate_audio_catalog import read_catalog

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / "Simpsons Game, The (USA)"
INSTALL = ROOT / "build/audio-codec/install"
SOURCE = ROOT / "tools/probe_stream_catalog_decode.cpp"
CATALOG = ROOT / "analysis/audio_catalog.bin"
DLLS = ("avcodec-simpsonsxma-62.dll", "avutil-simpsonsxma-60.dll", "libwinpthread-1.dll")
HEADER = "id\tstatus\tblocks\tpackets\traw_frames\traw_hashes\tminimum_segment_surplus\tmaximum_block_frames\tmilliseconds\treason"

def need(ok, reason):
    if not ok:
        raise RuntimeError(reason)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def chains(catalog):
    groups = {}
    for index, stream in enumerate(catalog.streams):
        digest = hashlib.sha256()
        # Playback/intro distinctions are retained even when compressed bytes match.
        digest.update(stream["header"])
        digest.update(struct.pack("<5I", stream["channels"], stream["sample_rate"], stream["flags"],
                                  stream["loop_start_sample"], stream["block_count"]))
        for at in range(stream["first_block"], stream["first_block"] + stream["block_count"]):
            block = catalog.blocks[at]
            need(block["stream_index"] == index and block["ordinal"] == at - stream["first_block"],
                 "Catalog block sequence differs")
            digest.update(block["normalized_sha256"])
            digest.update(struct.pack("<I", block["segment_flag"]))
        key = digest.hexdigest()
        groups.setdefault(key, []).append(index)
    return groups

def select(catalog, groups, all_streams, maximum):
    unique = [(key, indices) for key, indices in groups.items()]
    if not all_streams:
        selected = {}
        for item in unique:
            stream = catalog.streams[item[1][0]]
            profile = (stream["channels"], stream["flags"])
            selected.setdefault(profile, item)
        # Include the longest authored stream of each channel profile for timing.
        for channels in (1, 2, 4, 6):
            candidates = [item for item in unique if catalog.streams[item[1][0]]["channels"] == channels]
            longest = max(candidates, key=lambda item: catalog.streams[item[1][0]]["frames"])
            selected[(channels, "longest")] = longest
        unique = list({item[0]: item for item in selected.values()}.values())
    if maximum:
        unique = unique[:maximum]
    need(bool(unique), "No streams selected")
    return unique

def manifest(catalog, selected, output):
    records = []
    with output.open("wb") as file:
        file.write(b"SDEC0001" + struct.pack("<I", len(selected)))
        for index, (key, aliases) in enumerate(selected):
            stream = catalog.streams[aliases[0]]
            source = catalog.sources[stream["source_index"]]
            path = (ORIGINAL / source["path"]).resolve(strict=True)
            need(path.is_file() and path.is_relative_to(ORIGINAL.resolve()), "Original path escapes owned assets")
            path_bytes = str(path).encode("utf-8")
            file.write(struct.pack("<5I2Q", index, stream["channels"], stream["sample_rate"], stream["flags"],
                                   stream["block_count"], stream["header_offset"], source["file_bytes"]))
            file.write(stream["header"] + source["sha256"] + struct.pack("<I", len(path_bytes)) + path_bytes)
            for b in range(stream["first_block"], stream["first_block"] + stream["block_count"]):
                block = catalog.blocks[b]
                payload, ff = [0]*3, [0]*3
                need(block["layer_count"] == (stream["channels"] + 1)//2, "Catalog channel/layer count differs")
                for i in range(block["layer_count"]):
                    layer = catalog.layers[block["first_layer"] + i]
                    need(layer["block_index"] == b and layer["ordinal"] == i and
                         layer["restored_ff"] == (-layer["payload_bytes"]) % 2048, "Catalog layer provenance differs")
                    payload[i], ff[i] = layer["payload_bytes"], layer["restored_ff"]
                file.write(struct.pack("<Q4I", block["raw_offset"], block["raw_bytes"], block["frames"],
                                       block["segment_flag"], block["terminal_padding"]))
                file.write(block["raw_sha256"] + block["normalized_sha256"] + struct.pack("<6I", *payload, *ff))
            records.append({"stream_index": aliases[0], "catalog_stream_aliases": aliases,
                            "ordered_chain_sha256": key, "source_path": source["path"],
                            "source_sha256": source["sha256"].hex(), "ordinal": stream["ordinal"],
                            "header_hex": stream["header"].hex(), "channels": stream["channels"],
                            "sample_rate": stream["sample_rate"], "loop": bool(stream["flags"]),
                            "declared_frames": stream["frames"], "block_count": stream["block_count"]})
    return records

def verify_aliases(catalog, selected):
    """Recheck actual duplicate bytes; an alias is not an extra codec run."""
    representatives = {indices[0] for _, indices in selected}
    native_sources = {catalog.streams[i]["source_index"] for i in representatives}
    by_source = {}
    for _, indices in selected:
        for index in indices[1:]:
            source_index = catalog.streams[index]["source_index"]
            by_source.setdefault(source_index, []).append(index)
    verified_sources, verified_blocks, verified_streams = [], 0, 0
    for source_index, indices in by_source.items():
        source = catalog.sources[source_index]
        path = (ORIGINAL / source["path"]).resolve(strict=True)
        need(path.is_relative_to(ORIGINAL.resolve()), "Alias path escapes original assets")
        with path.open("rb") as file:
            # Native File::select verifies represented sources in full. Missing
            # duplicate-only sources must also match their full original hash.
            if source_index not in native_sources:
                digest, size = hashlib.sha256(), 0
                while chunk := file.read(65536):
                    digest.update(chunk); size += len(chunk)
                need(size == source["file_bytes"] and digest.digest() == source["sha256"],
                     f"Original duplicate source SHA256/extent changed: {source['path']}")
                verified_sources.append(source_index)
            for index in indices:
                stream = catalog.streams[index]
                file.seek(stream["header_offset"])
                need(file.read(8) == stream["header"], "Original duplicate stream header changed")
                for b in range(stream["first_block"], stream["first_block"]+stream["block_count"]):
                    block = catalog.blocks[b]
                    file.seek(block["raw_offset"]); raw = file.read(block["raw_bytes"])
                    need(len(raw) == block["raw_bytes"] and hashlib.sha256(raw).digest() == block["raw_sha256"] and
                         raw[0] == block["segment_flag"] and
                         hashlib.sha256(bytes([raw[0] & 127])+raw[1:]).digest() == block["normalized_sha256"],
                         "Original duplicate ordered block differs")
                    verified_blocks += 1
                verified_streams += 1
    all_sources = native_sources | set(verified_sources)
    return {"verified_alias_streams": verified_streams, "verified_alias_blocks": verified_blocks,
            "additional_full_source_hashes": len(verified_sources),
            "native_representative_source_count": len(native_sources),
            "covered_source_count": len(all_sources),
            "method": "Every alias header and ordered raw/normalized block hash rechecked; full file hashes native for represented sources, Python for duplicate-only sources",
            "native_codec_repeated_for_aliases": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--max-streams", type=int)
    parser.add_argument("--timeout", type=int, default=1800, help="Bound native census wall time in seconds")
    parser.add_argument("--out", type=Path, default=ROOT / "build/stream-catalog-decode")
    args = parser.parse_args()
    need(args.max_streams is None or args.max_streams > 0, "Stream count must be positive")
    need(1 <= args.timeout <= 7200, "Timeout must be 1..7200 seconds")
    out = args.out.resolve()
    need(out.is_relative_to((ROOT / "build").resolve()) and out != (ROOT / "build").resolve(), "Output must be a build subdirectory")
    out.mkdir(parents=True, exist_ok=True)
    catalog = read_catalog(CATALOG)
    groups = chains(catalog)
    selected = select(catalog, groups, args.all, args.max_streams)
    verified = subprocess.run([sys.executable, "-B", str(ROOT / "tools/build_native_audio_codec.py"), "--verify"],
                              cwd=ROOT, capture_output=True, text=True, timeout=90)
    need(verified.returncode == 0, f"Owned codec verification failed: {verified.stderr[-1000:]}")
    spec = importlib.util.spec_from_file_location("stream_probe_toolchain", ROOT / "tests/test_host_fp.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    compiler, env = helper.toolchain()
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="simpsons-stream-decode-") as temporary:
        work = Path(temporary)
        input_path = work / "manifest.bin"
        records = manifest(catalog, selected, input_path)
        executable = work / "probe.exe"
        command = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/fp:strict",
                   "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", f"/I{ROOT}", f"/I{INSTALL/'include'}",
                   str(SOURCE), str(ROOT / "audio/native_xma_codec.cpp"), str(ROOT / "audio/ea_xma_block.cpp"),
                   f"/Fo{work.as_posix()}/", f"/Fe{executable}", "/link", f"/LIBPATH:{INSTALL/'lib'}",
                   "avcodec-simpsonsxma.lib", "avutil-simpsonsxma.lib", "bcrypt.lib", "/INCREMENTAL:NO"]
        built = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True, timeout=120)
        (out / "compile.log").write_text(built.stdout + built.stderr, encoding="utf-8")
        need(built.returncode == 0, f"Standalone compile failed: {(built.stdout+built.stderr)[-2000:]}")
        for name in DLLS:
            shutil.copyfile(INSTALL / "bin" / name, work / name)
        env["PATH"] = str(work) + os.pathsep + env.get("PATH", "")
        print(f"Decoding {len(records)} unique shipped chains; live rows: {out / 'native.tsv'}", flush=True)
        timed_out = False
        with (out / "native.tsv").open("w", encoding="utf-8") as stdout, (out / "native.stderr.log").open("w", encoding="utf-8") as stderr:
            process = subprocess.Popen([str(executable), str(input_path)], cwd=work, env=env, stdout=stdout, stderr=stderr)
            try:
                status = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                process.kill();process.wait();status = -1;timed_out = True
    lines = (out / "native.tsv").read_text(encoding="utf-8").splitlines()
    need(lines and lines[0] == HEADER, "Native census header differs")
    completed = []
    for index, line in enumerate(lines[1:]):
        fields = line.split("\t")
        need(len(fields) == 10 and int(fields[0]) == index, "Native census row is incomplete")
        row = records[index]
        row.update(native_status=fields[1], decoded_blocks=int(fields[2]),
                   packets=[int(x) for x in fields[3].split(",") if x],
                   raw_frames=[int(x) for x in fields[4].split(",") if x],
                   raw_hashes_fnv64=fields[5].split(",") if fields[5] else [],
                   minimum_segment_surplus=int(fields[6]), maximum_block_raw_frames=int(fields[7]),
                   milliseconds=int(fields[8]), reason=fields[9])
        need(row["native_status"] in ("OK", "FAIL"), "Unknown native census status")
        completed.append(row)
    failures = [row for row in completed if row["native_status"] != "OK"]
    alias_verification = verify_aliases(catalog, selected)
    report = {"probe_version": 1, "scope": "all_unique_stream_chains" if args.all else "representative_complete_stream_chains",
              "complete": status == 0 and len(completed) == len(records), "timed_out": timed_out,
              "catalog_sha256": sha(catalog.data), "catalog_stream_count": len(catalog.streams),
              "catalog_unique_chains": len(groups), "selected_unique_chains": len(records),
              "completed_unique_chains": len(completed), "completed_stream_instances": sum(len(r["catalog_stream_aliases"]) for r in completed),
              "completed_blocks": sum(r["decoded_blocks"] for r in completed), "failed_count": len(failures),
              "qualified_count": len(completed)-len(failures), "wall_seconds": time.monotonic()-started,
              "channels": dict(Counter(str(r["channels"]) for r in completed)),
              "probe_source_sha256": sha(SOURCE.read_bytes()), "codec_source_sha256": sha((ROOT / "audio/native_xma_codec.cpp").read_bytes()),
              "driver_source_sha256": sha(Path(__file__).read_bytes()), "alias_verification": alias_verification,
              "parser_source_sha256": sha((ROOT / "audio/ea_xma_block.cpp").read_bytes()),
              "codec_dll_sha256": {name: sha((INSTALL / "bin" / name).read_bytes()) for name in DLLS},
              "raw_eof_sent": False, "pcm_files_written": False, "audio_endpoint_opened": False,
              "loop_scope": "decode introduction once; explicit fresh body context; body once; no loop replay",
              "not_validated": ["seek", "restart/cancel scheduling", "reader claim lifetime", "XmaSource quota/ring scheduling", "speaker routing", "output endpoint", "audible quality"],
              "records": completed}
    (out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    for row in failures[:12]:
        print(f"FAIL {row['source_path']} stream{row['ordinal']}: {row['reason']}")
    print(f"{report['qualified_count']}/{len(completed)} complete chains passed; {report['wall_seconds']:.2f}s; report {out/'report.json'}")
    if failures or not report["complete"]:
        raise SystemExit(1)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        print(f"Stream decode probe failed: {error}", file=sys.stderr)
        raise SystemExit(1)
