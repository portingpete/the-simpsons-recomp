"""Read-only original fetch proof for an unused packed shadow tangent row."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
SHADER = 0x820C2FA0
SHADER_BYTES = 4396
SHADER_SHA = "b22ecbda409e80ba9bbc5a8da237c8894c2f09c79b93646868e925c70ef058be"
PINS = {
    # Four original fetch associations: usage/index0 for position, UV,
    # weights and indices. None selects usage6 tangent.
    0x820C3DA8: "00100007 00005008 00001009 0020200A",
    # SDK fetch patch chooses a declaration row by semantic AND index,
    # scanning all twelve-byte rows independently of their fixed offsets.
    0x8245EE64: "813B0000 553FA73E 8AA50009 7F15F840 409A0014 8AA5000A 5534873E 7F15A040 419A0014 394A0001 38A5000C 7F0AC840 4198FFD8",
    0x8245EF98: "A1050000 554A05AE A0A50002",
}


def verify(image, check_hash=True):
    if check_hash and hashlib.sha256(image).hexdigest() != IMAGE_SHA:
        raise ValueError("Original image hash changed")
    if hashlib.sha256(image[SHADER-BASE:SHADER-BASE+SHADER_BYTES]).hexdigest() != SHADER_SHA:
        raise ValueError("Original shadow shader record changed")
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original shadow fetch contract changed at {address:08X}")
    return {"image_sha256": IMAGE_SHA, "shader_source": f"{SHADER:08X}",
            "shader_record_sha256": SHADER_SHA,
            "fetch_metadata": "820C3DA8..820C3DB4", "fetched_usages": [0, 5, 1, 2],
            "association_sites": ["8245EE64..8245EE94", "8245EF98..8245EFA0"],
            "unused_tangent": {"usage": 6, "index": 0, "stream": 0, "type": "002A2187", "bytes": 4},
            "native_test": "OriginalShadowTangentPass runs original 82707678 with stride40 plus tangent0, checks depth invariance after dead payload mutation and original declaration/FX/cache retirement.",
            "scope": "Extra typed tangent row not selected by the original shader fetch association; no arbitrary unused semantic or nonempty alpha queue is qualified."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    args = parser.parse_args()
    image = args.image.read_bytes()
    report = verify(image)
    mutations = 0
    for address, words in PINS.items():
        for at in range(len(bytes.fromhex(words))):
            changed = bytearray(image)
            changed[address-BASE+at] ^= 1
            try:
                verify(changed, check_hash=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError("Changed original shadow fetch association was accepted")
    report["mutation_checks_passed"] = mutations
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
