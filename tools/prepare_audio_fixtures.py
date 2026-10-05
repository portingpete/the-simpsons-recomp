"""Prepare two hash-pinned original mono packet fixtures, without a decoder.

Only EA block/layer wrappers and stripped packet padding are adapted. Derived
packets stay in build/audio-fixtures, outside the immutable original data tree.
The test runner can execute the native ownership test after preparation.
"""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from inspect_assets import inspect_snu

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/audio-fixtures"
CASES = (
    ("short", "audiostreams/ri_xxx_0/d_shri_xxx_0005795.exa.snu",
     "4007581a5527caaab34b130f0c603a694f7273b1188abde36b3f5da97c6855e4",
     "312a9c2a63c7744f84df913da544e3fce047289cd42aa5ae76808eb2117a9996", 8064),
    ("documented", "audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu",
     "bfd2acf74af905f1521fbe1502c1f140a82a73f003fc1cbe2dec866e92b9f7a9",
     "9a837e71c7b71887a182202ada01c003a99515308967f55bf115e9ba8230014f", 54901),
)

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "Simpsons Game, The (USA)")
    parser.add_argument("--test", type=Path)
    args = parser.parse_args()
    original = args.root.resolve(strict=True)
    require(not OUT.resolve().is_relative_to(original), "Output overlaps original data")
    require(OUT.resolve().is_relative_to(ROOT / "build"), "Fixture output escapes build directory")
    OUT.mkdir(parents=True, exist_ok=True)
    report = []
    for label, relative, source_hash, packet_hash, declared in CASES:
        source = (original / relative).resolve(strict=True)
        require(source.is_relative_to(original), "Source escapes original root")
        data = source.read_bytes()
        require(sha(data) == source_hash, f"Original source identity changed: {relative}")
        info = inspect_snu(data)
        header = info["header"]
        require(header["channels"] == 1 and header["sample_rate"] == 48000 and
                not header["loop"] and header["samples"] == declared, "Unexpected fixture format")
        packets = bytearray()
        spans = []
        pos = info["audio"]["audio_offset"]
        while pos < len(data):
            block_length = int.from_bytes(data[pos:pos+4], "big") & 0xffffff
            layer = pos + 8
            layer_length = int.from_bytes(data[layer:layer+4], "big") >> 2
            payload = data[layer+4:layer+layer_length]
            require(payload[:4] == b"\x08\0\0\0", "Unexpected XMA packet header")
            padding = (-len(payload)) % 2048
            spans.append({"source_offset": layer+4, "source_bytes": len(payload),
                          "packet_offset": len(packets), "restored_ff_tail_bytes": padding})
            packets += payload + b"\xff" * padding
            pos += block_length
        require(sha(packets) == packet_hash, f"Adapted packet identity changed: {label}")
        target = OUT / (label + ".packets")
        require(target.resolve().parent == OUT.resolve(), "Fixture target escapes output directory")
        target.write_bytes(packets)
        require(sha(source.read_bytes()) == source_hash, "Original source changed during preparation")
        report.append({"label": label, "source": relative, "source_sha256": source_hash,
                       "packet_sha256": packet_hash, "packets": len(packets)//2048,
                       "declared_samples": declared, "spans": spans})
    (OUT / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Prepared two original mono packet fixtures; original hashes unchanged", flush=True)
    if args.test:
        return subprocess.run([str(args.test.resolve(strict=True)), str(OUT)], timeout=45).returncode
    return 0

if __name__ == "__main__":
    sys.exit(main())
