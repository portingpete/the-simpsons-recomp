"""Probe the exact resident frontend bank source observed during native boot.

Keeps originals unchanged; uses only the privately owned native decoder.
No raw EOF, sample synthesis, packet restoration, or guest execution.
"""
from __future__ import annotations

import hashlib
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from probe_xma_multilayer import CPP, INSTALL

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/resident-xma"
BANK_HASH = "6605ab30a96453c9108af5866b3670ddda719cafe36301f857ae6279bc136482"
BLOCK_HASH = "69afdab657fa91c308ffc76a84b72ec20458d0c5740b8d7d54993fb7a7128381"


def need(ok, why):
    if not ok:
        raise RuntimeError(why)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def packet_boundary_diagnostic(packets):
    """Equivalent framing for the independent stock decoder's boundary bug.

    Only an end-of-packet frame trailer is cleared, never spectral frame data.
    Production decodes the original bytes; the native packet parser owns the fix.
    """
    need(len(packets) % 2048 == 0, 'Diagnostic requires complete XMA packets')
    changed = bytearray(packets)
    boundaries = []
    for offset in range(0, len(packets), 2048):
        bits = ''.join(f'{b:08b}' for b in packets[offset:offset+2048])
        position = 32+int(bits[6:21], 2)
        while position+15 <= len(bits):
            length = int(bits[position:position+15], 2)
            if length < 15 or length == 32767 or position+length > len(bits):
                break
            position += length
            if position == len(bits) and bits[position-1] == '1':
                boundaries.append(offset//2048)
                changed[offset+2047] &= 0xFE
            if bits[position-1] == '0':
                break
    return bytes(changed), boundaries


def run(command, env, name, timeout=60):
    result = subprocess.run([str(x) for x in command], cwd=OUT, env=env,
                            capture_output=True, timeout=timeout)
    (OUT / (name + ".log")).write_bytes(result.stdout + result.stderr)
    need(result.returncode == 0, f"{name} failed: {result.stderr.decode(errors='replace')[-2000:]}")
    return result


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('intro', 'start'), default='intro')
    args = parser.parse_args()
    data = (ROOT / 'build/resident-xma/frontend.sbk').read_bytes()
    need(sha(data) == BANK_HASH, "Extracted original SBK identity changed")
    if args.profile == 'intro':
        header_offset, expected_header, expected_bytes, expected_frames = 33335, '03041f400006678e', 18444, 419726
        expected_hash, channels, codec_rate, playback_rate, expected_selector = BLOCK_HASH, 2, 24000, 8000, 0
    else:
        OUT = ROOT / 'build/resident-start-xma'
        OUT.mkdir(parents=True, exist_ok=True)
        header_offset, expected_header, expected_bytes, expected_frames = 51787, '0300bb8000023015', 40972, 143381
        expected_hash, channels, codec_rate, playback_rate, expected_selector = 'ed9fda6df5b104e5a4a59ffad78918ebf89bfd83f5a7f332c2bf293c3b2e19b1', 1, 48000, 48000, 3
    header = data[header_offset:header_offset + 8]
    need(header.hex() == expected_header, "Live resident header differs")
    start = header_offset + 8
    length, declared = struct.unpack_from(">II", data, start)
    block = data[start:start + length]
    need(len(block) == length == expected_bytes and declared == expected_frames and sha(block) == expected_hash,
         "Live resident block identity/framing changed")
    word = struct.unpack_from(">I", block, 8)[0]
    size, selector = word >> 2, word & 3
    need(selector == expected_selector and size == length - 8, "Resident layer extent/selector changed")
    packets = block[12:]
    need(len(packets) == expected_bytes - 12 and len(packets) % 2048 == 0, "Resident packet extent changed")
    (OUT / "frontend-observed.packets").write_bytes(packets)
    spec = importlib.util.spec_from_file_location("resident_toolchain", ROOT / "tests/test_host_fp.py")
    tc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tc)
    compiler, env = tc.toolchain()
    env["PATH"] = str(INSTALL / "bin") + os.pathsep + env["PATH"]
    run([sys.executable, "-B", ROOT / "tools/build_native_audio_codec.py", "--verify"], env, "codec-verify")
    # Respect each original layer selector independently of the EA playback
    # clock (the intro uses 24000-Hz codec / 8000-Hz playback; Start uses 48000).
    # Increase only the bounded diagnostic frame extent for a complete block.
    harness = CPP.replace("{channels,48000,", f"{{channels,{codec_rate},").replace("frames<=262144", "frames<=524288")
    need(harness != CPP and f"{{channels,{codec_rate}," in harness, "Harness substitution failed")
    (OUT / "harness.cpp").write_text(harness, encoding="utf-8")
    run([compiler, "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/fp:strict",
         "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", f"/I{ROOT}", f"/I{INSTALL/'include'}",
         OUT / "harness.cpp", ROOT / "audio/native_xma_codec.cpp", "/Fo" + str(OUT) + "\\",
         "/Fe" + str(OUT / "harness.exe"), "/link", INSTALL / "lib/avcodec-simpsonsxma.lib",
         INSTALL / "lib/avutil-simpsonsxma.lib", "/INCREMENTAL:NO"], env, "compile", 90)
    report = {"bank_sha256": BANK_HASH, "header_offset": header_offset, "header": header.hex(),
              "block_sha256": expected_hash, "block_bytes": length, "declared_frames": declared,
              "channels": channels, "codec_selector": selector, "codec_rate": codec_rate, "playback_rate": playback_rate,
              "packet_sha256": sha(packets), "packet_count": len(packets) // 2048,
              "restored_bytes": 0, "raw_eof_sent": False, "runs": []}
    raw = None
    for mode, split in (("xma1", 0), ("xma2", 0), ("xma2", 1)):
        label = f"observed-{mode}-{split}"
        pcm, csv = OUT / (label + ".f32le"), OUT / (label + ".csv")
        result = run([OUT / "harness.exe", mode, channels, OUT / "frontend-observed.packets", pcm, csv, split], env, label)
        lines = result.stdout.decode().splitlines()
        modules = {}
        for line in lines:
            if line.startswith("module "):
                _, name, path = line.split(" ", 2)
                need(Path(path).resolve() == (INSTALL / "bin" / name).resolve(), "Unowned decoder DLL")
                modules[name] = path
        need(len(modules) == 3, "Incomplete decoder DLL identity")
        summary = next(x for x in lines if x.startswith("result ")).split()
        need(summary[-1] == "no_eof_sent" and int(summary[1]) == len(packets)//2048, "Packet accounting/EOF mismatch")
        decoded = pcm.read_bytes()
        frames = len(decoded) // (channels*4)
        need(len(decoded) == int(summary[2]) * channels*4 and frames >= declared + 384,
             f"Full declared quota and original initial skip unavailable: {frames}")
        if raw is None:
            raw = decoded
        need(decoded == raw, "XMA variants/read schedules differ")
        report["runs"].append({"mode": mode, "split": split, "raw_frames": frames,
                               "pcm_sha256": sha(decoded), "surplus_after_skip": frames - declared - 384,
                               "loaded_modules": modules})
        print(label, "raw frames", frames, "declared", declared, "surplus", frames - declared - 384, flush=True)
    need(sha((ROOT / "build/resident-xma/frontend.sbk").read_bytes()) == BANK_HASH, "Original bank changed during probe")
    report["raw_variants_and_schedules_byte_identical"] = True
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS resident packets cover full quota with original initial skip; no EOF", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError, StopIteration) as error:
        print("FAIL resident qualification:", error, file=sys.stderr)
        sys.exit(1)
