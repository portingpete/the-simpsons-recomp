"""Qualify the observed menu MUS stream using owned native decoders, without EOF.

Originals are read-only. Diagnostic packets, PCM and provenance stay in
build/menu-xma. The admission certificate is emitted only after verification.
"""
from __future__ import annotations

import argparse
import array
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import csv
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from inspect_assets import inspect_mus
from probe_xma_multilayer import CPP, CLI, INSTALL, riff, split_blocks

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/menu-xma"
ORIGINAL = ROOT / "Simpsons Game, The (USA)/audiostreams/menu_mus.mus"
SOURCE_SHA = "3154127815784c0de9331e1b69748ff47b2e5e70de012296fedb1aace9a31dda"


def need(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(command, env, name, timeout=60):
    result = subprocess.run([str(x) for x in command], cwd=OUT, env=env,
                            capture_output=True, timeout=timeout)
    (OUT / (name + ".log")).write_bytes(result.stdout + result.stderr)
    need(result.returncode == 0, f"{name} failed: {result.stderr.decode(errors='replace')[-1500:]}")
    return result


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect-only", action="store_true")
    parser.add_argument("--stream", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    # Exact profiles observed in live reader claims: title music and Start cue.
    profiles = {
        0: ("59d0d1a4", 2422353, 474, "599d89af7815070a5c7ce16a53dab05fec6cf51c63482d54ee77c3cfb3fd4a3b", "menu", "menuXmaCertificates"),
        1: ("3eb4b454", 88803, 18, "58c9ec1bf0c5cab428ae13eb74727075b697b97697ca4526b0e0b69930d2f798", "menu_start", "menuStartXmaCertificates"),
    }
    stream_id, frames, block_count, first_hash, stem, symbol = profiles[args.stream]
    OUT = ROOT / ("build/menu-xma" if args.stream == 0 else "build/menu-start-xma")
    data = ORIGINAL.read_bytes()
    need(sha(data) == SOURCE_SHA, "Original menu MUS identity changed")
    info = inspect_mus(data)["streams"][args.stream]
    header, audio = info["header"], info["audio"]
    need(info["id_hex"] == stream_id and header["channels"] == 6 and
         header["sample_rate"] == 48000 and not header["loop"] and
         header["samples"] == frames, "Observed menu source profile changed")
    blocks = split_blocks(data, audio["audio_offset"],
                          audio["audio_offset"] + audio["audio_size"], 6)
    need(len(blocks) == block_count, "Menu block count changed")
    for block in blocks:
        raw = data[block["offset"]:block["offset"] + block["bytes"]]
        normalized = (int.from_bytes(raw[:4], "big") & 0x7fffffff).to_bytes(4, "big") + raw[4:]
        block["owned_sha256"] = sha(normalized)
        layer_end = block["layers"][-1]["payload_offset"] + block["layers"][-1]["payload_bytes"]
        block["terminal_padding"] = block["offset"] + block["bytes"] - layer_end
    need(blocks[0]["owned_sha256"] == first_hash,
         "Observed live reader claim does not match menu source")
    print("Pinned menu MUS", SOURCE_SHA, flush=True)
    print("Blocks", len(blocks), "frames", header["samples"], "largest quota segment",
          max(b["samples"] for b in blocks), "terminal padding", blocks[-1]["terminal_padding"], flush=True)
    maximum_window = max(sum(b["samples"] for b in blocks[i:i+20]) for i in range(len(blocks)))
    print("Maximum 20-source declared window", maximum_window, flush=True)
    if args.inspect_only:
        print(json.dumps(blocks[-1], indent=2))
        return

    OUT.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("menu_toolchain", ROOT / "tests/test_host_fp.py")
    toolchain = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(toolchain)
    compiler, env = toolchain.toolchain()
    env["PATH"] = str(INSTALL / "bin") + os.pathsep + env["PATH"]
    run([sys.executable, "-B", ROOT / "tools/build_native_audio_codec.py", "--verify"], env, "codec-verify")
    # Same audited raw harness with finite bounds expanded for this complete track.
    harness = CPP.replace("size<=262144", "size<=4194304").replace("frames<=262144", "frames<=4194304")
    (OUT / "harness.cpp").write_text(harness, encoding="utf-8")
    run([compiler, "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/fp:strict",
         "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", f"/I{ROOT}", f"/I{INSTALL/'include'}",
         OUT / "harness.cpp", ROOT / "audio/native_xma_codec.cpp", "/Fo" + str(OUT) + "\\",
         "/Fe" + str(OUT / "harness.exe"), "/link", INSTALL / "lib/avcodec-simpsonsxma.lib",
         INSTALL / "lib/avutil-simpsonsxma.lib", "/INCREMENTAL:NO"], env, "compile", 90)

    report = {"source": str(ORIGINAL.relative_to(ROOT)), "source_sha256": SOURCE_SHA,
              "stream": info, "raw_eof_sent": False, "native_policy": "NativeRawF32/Xma2",
              "initial_skip_frames": 384, "blocks": blocks, "layers": [],
              "maximum_20_source_window": maximum_window}
    for layer in range(3):
        packets = bytearray()
        ends = []
        for block in blocks:
            span = block["layers"][layer]
            payload = data[span["payload_offset"]:span["payload_offset"] + span["payload_bytes"]]
            need(sha(payload) == span["payload_sha256"], "Owned layer span identity changed")
            packets.extend(payload)
            packets.extend(b"\xff" * span["restored_ff_bytes"])
            ends.append(len(packets) // 2048)
        packet_path = OUT / f"layer{layer}.packets"
        packet_path.write_bytes(packets)
        entry = {"layer": layer, "packet_sha256": sha(packets), "packets": len(packets)//2048, "runs": []}
        raw = None
        for mode, split in (("xma1", 0), ("xma2", 0), ("xma2", 1)):
            label = f"layer{layer}-{mode}-{split}"
            pcm, csv = OUT / (label + ".f32le"), OUT / (label + ".csv")
            result = run([OUT / "harness.exe", mode, 2, packet_path, pcm, csv, split], env, label)
            lines = result.stdout.decode("utf-8", errors="replace").splitlines()
            for line in lines:
                if line.startswith("module "):
                    _, name, path = line.split(" ", 2)
                    need(Path(path).resolve() == (INSTALL / "bin" / name).resolve(), "Unowned native decoder DLL")
            summary = next(x for x in lines if x.startswith("result ")).split()
            need(summary[-1] == "no_eof_sent" and int(summary[1]) == len(packets)//2048, "Packet accounting mismatch")
            decoded = pcm.read_bytes()
            need(len(decoded) == int(summary[2])*8, "Raw PCM extent mismatch")
            if raw is None:
                raw = decoded
            need(decoded == raw, "Raw XMA variants or read schedules differ")
            entry["runs"].append({"mode": mode, "split": split, "frames": len(decoded)//8,
                                  "pcm_sha256": sha(decoded)})
            if mode == "xma2" and split == 0:
                # All per-block quota boundaries must be satisfiable without future input.
                produced = {}
                with csv.open(newline="", encoding="utf-8") as stream:
                    reader = csv.reader(stream)
                    next(reader, None)
                    for row in reader:
                        _, accepted, offset, count = map(int, row[:4])
                        produced[accepted] = offset + count
                accumulated = 384
                last = 0
                margins = []
                for index, end in enumerate(ends):
                    last = max(last, max((v for k, v in produced.items() if k <= end), default=0))
                    accumulated += blocks[index]["samples"]
                    need(last >= accumulated, f"Layer {layer} block {index} cannot satisfy full declared quota")
                    margins.append(last - accumulated)
                entry["minimum_per_block_surplus"] = min(margins)
                entry["maximum_per_block_surplus"] = max(margins)
        # Independent stock decoder diagnostic. Its 576-frame trim is measured,
        # never used for native guest skipping or as hardware quantization proof.
        wave = OUT / f"layer{layer}.diagnostic.wav"
        wave.write_bytes(riff(packets))
        stock = run([CLI, "-nostdin", "-hide_banner", "-loglevel", "error", "-xerror", "-threads", 1,
                     "-f", "wav", "-i", wave, "-map", "0:a:0", "-c:a", "pcm_f32le", "-f", "f32le", "pipe:1"],
                    env, f"layer{layer}-stock")
        original_floats, stock_floats = array.array('f'), array.array('f')
        original_floats.frombytes(raw)
        stock_floats.frombytes(stock.stdout)
        need(len(stock_floats) >= 2048, "Stock diagnostic PCM missing")
        usable = min(len(stock_floats), len(original_floats)-1152)-1024
        maximum_error = max(abs(original_floats[i+1152]-stock_floats[i]) for i in range(usable))
        need(maximum_error < 0.0001, "Independent stock alignment failed")
        entry["stock_measured_offset_frames"] = 576
        entry["stock_maximum_error"] = maximum_error
        report["layers"].append(entry)
        print("Qualified layer", layer, "packets", entry["packets"], "raw frames", len(raw)//8,
              "quota surplus", entry["minimum_per_block_surplus"], "..", entry["maximum_per_block_surplus"], flush=True)
    need(sha(ORIGINAL.read_bytes()) == SOURCE_SHA, "Original changed during qualification")
    report["original_unchanged"] = True
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    # Binary fixture: normalized reader-owned blocks in original sequence.
    fixture = bytearray(struct.pack('<I', len(blocks)))
    for block in blocks:
        raw = bytearray(data[block["offset"]:block["offset"]+block["bytes"]])
        raw[0] &= 0x7f
        fixture.extend(struct.pack('<I', len(raw)))
        fixture.extend(raw)
    (OUT / "blocks.bin").write_bytes(fixture)
    rows = []
    for block in blocks:
        lengths = ",".join(str(x["payload_bytes"]) for x in block["layers"])
        ff = ",".join(str(x["restored_ff_bytes"]) for x in block["layers"])
        rows.append('    {"%s",%d,%d,{%s},{%s},%d},' % (block["owned_sha256"], block["bytes"],
                    block["samples"], lengths, ff, block["terminal_padding"]))
    certificate = ('// Verified by tools/qualify_menu_xma.py; source SHA256 ' + SOURCE_SHA + '\n'
                   '// Hashes cover normalized reader-owned blocks including every payload and terminal byte.\n'
                   '#pragma once\n#include <array>\n#include <cstdint>\n#include <string_view>\n'
                   + ('#include "menu_xma_certificates.h"\n' if args.stream else '') +
                   'namespace Simpsons::Audio {\n' +
                   ('' if args.stream else 'struct EaXmaCertificate {std::string_view sha256;uint32_t bytes,frames;'
                    'std::array<uint32_t,3> payloadBytes,restoredFF;uint32_t terminalPadding;};\n') +
                   f'inline constexpr std::array<EaXmaCertificate,{block_count}> {symbol} = {{{{\n'
                   + '\n'.join(rows) + '\n}};\n}\n')
    (ROOT / f"audio/{stem}_xma_certificates.h").write_text(certificate, encoding="utf-8")
    print("PASS complete menu source, all layers/blocks/schedules, no raw EOF; report", OUT / "report.json", flush=True)


if __name__ == "__main__":
    main()
