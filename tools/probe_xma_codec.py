"""Bounded original SNU -> diagnostic XMA2 envelope -> native FFmpeg PCM probe.

No compressed frame bits are changed or re-encoded. EA block/layer wrappers are
removed, stripped packet tails restored with FF bytes per vgmstream, and a NEW
diagnostic RIFF header added. Its transport fields are not original game bytes.
This is an offline capability experiment, not a production stream/seek adapter.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import math
import os
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/audio-probe"
OUT.mkdir(parents=True, exist_ok=True)
def find_ffmpeg():
    for candidate in (os.environ.get("FFMPEG", ""), shutil.which("ffmpeg") or ""):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return Path(r"C:/Program Files (x86)/Steam/steamapps/common/ShareX/ShareX/ffmpeg.exe")
FFMPEG = find_ffmpeg()
CASES = [
    ("short", "audiostreams/ri_xxx_0/d_shri_xxx_0005795.exa.snu",
     "4007581a5527caaab34b130f0c603a694f7273b1188abde36b3f5da97c6855e4"),
    ("documented", "audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu",
     "bfd2acf74af905f1521fbe1502c1f140a82a73f003fc1cbe2dec866e92b9f7a9"),
]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def run(args):
    return subprocess.run([str(FFMPEG), "-nostdin", "-hide_banner", *args],
                          stdin=subprocess.DEVNULL, capture_output=True, timeout=20)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "Simpsons Game, The (USA)")
    args = parser.parse_args()
    source_root = args.root.resolve(strict=True)
    if not (OUT == ROOT / "build/audio-probe" and not OUT.is_relative_to(source_root)):
        raise RuntimeError("Audio probe output must stay inside build/audio-probe")
    spec = importlib.util.spec_from_file_location("asset_inspector", ROOT / "tools/inspect_assets.py")
    inspector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inspector)
    inventory = json.loads((ROOT / "analysis/assets.json").read_text(encoding="utf-8"))
    indexed = {f["path"]: f for f in inventory["files"]}
    report = {"scope": "two original mono nonlooping EA-XMA SNU files; no runtime ABI/output device/loop proof",
              "ffmpeg": str(FFMPEG), "ffmpeg_sha256": sha(FFMPEG.read_bytes()),
              "adapter_sources": [
                  "https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_eaac_streamfile.h",
                  "https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_eaac.c",
                  "https://github.com/vgmstream/vgmstream/blob/master/src/coding/ffmpeg_decoder_utils.c"],
              "cases": []}
    for name, arguments in [("version", ["-version"]), ("decoders", ["-decoders"])]:
        result = run(arguments)
        if result.returncode != 0:
            raise RuntimeError(f"ffmpeg {name} probe failed with {result.returncode}")
        (OUT / (name + ".log")).write_bytes(result.stdout + result.stderr)
    for label, relative, pinned in CASES:
        source = (source_root / relative).resolve(strict=True)
        if not source.is_relative_to(source_root):
            raise RuntimeError("Audio source escapes game root")
        data = source.read_bytes()
        source_hash = sha(data)
        if source_hash != indexed[relative]["sha256"]:
            raise RuntimeError(f"Source hash mismatch for {relative}")
        if pinned and source_hash != pinned:
            raise RuntimeError(f"Pinned hash mismatch for {relative}")
        metadata = inspector.inspect_snu(data)
        header = metadata["header"]
        assert header["channels"] == 1 and not header["loop"] and header["samples"] < 60000
        packets = bytearray()
        spans = []
        pos = metadata["audio"]["audio_offset"]
        while pos < len(data):
            block_length = int.from_bytes(data[pos:pos+4], "big") & 0xffffff
            layer = pos + 8
            layer_length = int.from_bytes(data[layer:layer+4], "big") >> 2
            # Keep the original packet header (08 00 00 00); remove only the
            # layer-size word. Full source spans were checked by inspect_snu.
            payload = data[layer+4:layer+layer_length]
            assert payload[:4] == b"\x08\0\0\0"
            padding = (-len(payload)) % 2048
            spans.append({"block_offset": pos, "payload_offset": layer+4,
                          "payload_bytes": len(payload), "payload_sha256": sha(payload),
                          "adapter_offset": len(packets), "restored_ff_tail_bytes": padding})
            packets += payload + b"\xff" * padding
            pos += block_length
        # New XMA2WAVEFORMATEX transport envelope, not an original asset format.
        # Only channel/rate come from EAAC. Encoded/play/loop extents are left
        # unspecified: do not make the demuxer truncate PCM to the EAAC count.
        fmt = bytearray(52)
        struct.pack_into("<HHIIHHH", fmt, 0, 0x166, 1, header["sample_rate"], 0, 2, 16, 34)
        struct.pack_into("<HIII", fmt, 18, 1, 4, 0, 0x10000)
        fmt[49] = 4
        struct.pack_into("<H", fmt, 50, (len(packets) + 0xffff)//0x10000)
        wave = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(packets)) + packets
        wave = b"RIFF" + struct.pack("<I", len(wave)) + wave
        adapter_path = OUT / (label + ".diagnostic.xma.wav")
        adapter_path.write_bytes(wave)
        arguments = ["-loglevel", "info", "-xerror", "-threads", "1", "-f", "wav", "-i", str(adapter_path),
                     "-map", "0:a:0", "-c:a", "pcm_f32le", "-f", "f32le", "pipe:1"]
        result = run(arguments)
        (OUT / (label + ".decode.log")).write_bytes(result.stderr)
        pcm = result.stdout
        (OUT / (label + ".f32le")).write_bytes(pcm)
        assert len(pcm) % 4 == 0
        samples = [item[0] for item in struct.iter_unpack("<f", pcm)]
        case = {"source": str(source), "relative_path": relative, "source_bytes": len(data),
                "source_sha256": source_hash, "source_header": header, "spans": spans,
                "adapter_sha256": sha(wave), "packet_bytes": len(packets),
                "command": [str(FFMPEG), "-nostdin", "-hide_banner", *arguments],
                "decode_exit": result.returncode, "pcm_bytes": len(pcm), "pcm_sha256": sha(pcm),
                "decoded_samples": len(samples), "declared_samples": header["samples"],
                "sample_count_delta": len(samples)-header["samples"],
                "all_finite": all(math.isfinite(x) for x in samples),
                "nonzero_samples": sum(abs(x) > 1e-12 for x in samples),
                "peak_abs": max(map(abs, samples), default=0),
                "rms": math.sqrt(sum(x*x for x in samples)/len(samples)) if samples else 0,
                "original_unchanged": sha(source.read_bytes()) == source_hash}
        report["cases"].append(case)
        print(label, "exit", result.returncode, "PCM samples", len(samples), "declared", header["samples"],
              "peak", case["peak_abs"], "finite", case["all_finite"])
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not all(c["decode_exit"] == 0 and c["all_finite"] and c["nonzero_samples"] and
               c["sample_count_delta"] == 0 and c["original_unchanged"] for c in report["cases"]):
        raise RuntimeError("Audio probe cases did not all decode cleanly")

if __name__ == "__main__":
    main()
