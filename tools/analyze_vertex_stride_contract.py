"""Read-only original producer evidence for padded skin/sky vertex streams.

Synthesized layouts exercise valid original inputs, not encountered mission assets.
The verifier pins the original instructions instead of inferring an API from
the native decoder. No game bytes are written or redistributed.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
PINS = {
    # The generic non-auxiliary branch loads G+4 and binds descriptor G+38.
    0x826FF31C: "80FB0004 2B050000 3BEB3028 39000001 38C00000 38800000 807F0000 409A0048 38BB0038 4BD3D281",
    # The auxiliary skin branch uses the same geometry stride and descriptor.
    0x826FF394: "39000001 80FB0004 3BEB3028 38C00000 38BB0038 38800000 7C791B78 807F0000 4BD3D20D",
    # SDK retains incoming r7 in r26, then shifts by two and stores a byte.
    0x8243C5D8: "7CFA3B78",
    0x8243C6A8: "574BF0BF 7FDBF92E 7D5FEA14 996A30E8",
    # Fetch association compares usage and index, scanning twelve-byte rows.
    0x8245EE64: "813B0000 553FA73E 8AA50009 7F15F840 409A0014 8AA5000A 5534873E 7F15A040 419A0014 394A0001 38A5000C 7F0AC840 4198FFD8",
    # Fetch patch reads the selected stream and byte offset, not fixed offsets.
    0x8245EF98: "A1050000 554A05AE A0A50002",
}


def verify(image, check_hash=True):
    if check_hash and hashlib.sha256(image).hexdigest() != IMAGE_SHA:
        raise ValueError("Original image hash changed")
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original producer/fetch instruction changed at {address:08X}")
    # The original byte encoding can retain exactly these nonzero strides.
    strides = [stride for stride in range(1, 4097)
               if stride % 4 == 0 and ((stride >> 2) & 255) * 4 == stride]
    if strides != list(range(4, 1021, 4)):
        raise ValueError("Original DWORD-count byte range differs")
    return {"image_sha256": IMAGE_SHA,
            "producer_sites": ["826FF31C..826FF340", "826FF394..826FF3B4"],
            "sdk_stride_sites": ["8243C5D8", "8243C6A8..8243C6B4"],
            "stride_range": {"minimum": 4, "maximum": 1020, "alignment": 4},
            "layout_constraint": "Each selected typed attribute must fit within its stride; native adapters retain separate required semantics and ownership checks.",
            "association": "8245EE64..8245EE94 selects by semantic/index;8245EF98/8245EFA0 loads selected stream/offset.",
            "scope": "Original producer-qualified synthesized padded/relocated layouts; no claim of mission encounter or whole renderer completeness."}


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
                raise ValueError("Changed original producer contract accepted")
    report["mutation_checks_passed"] = mutations
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
