"""Pin the original rigid/sky alpha-to-opaque blend inheritance producer.

This read-only proof extends the shared skin fallback pins with the complete
zero-bone draw branch. It establishes the producer contract independently of
the native sequence and pixel regressions; no original game bytes are written.
"""
import argparse
import hashlib
import json
from pathlib import Path

from analyze_skin_blend_contract import BASE, IMAGE_SHA, PINS as SHARED_PINS

ROOT = Path(__file__).resolve().parents[1]
PINS = {**SHARED_PINS,
        # The zero-bone branch retains the packet alpha byte, selects its
        # object/typed FX/submeshes, and calls the original rigid/sky owner.
        0x82740298: "38C00000 891F000C 80E30010 80BF0018 809F0004 4BFC0F75"}


def verify(image, check_hash=True):
    if check_hash and hashlib.sha256(image).hexdigest() != IMAGE_SHA:
        raise ValueError("Original image hash changed")
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original shared fallback blend producer changed at {address:08X}")
    return {"image_sha256": IMAGE_SHA,
            "producer_sites": ["827401CC..82740210", "82740230..82740294",
                               "82740298..827402AC", "827402B0..827402E4"],
            "zero_bone_original_draw_entry": "82701220",
            "alpha_dispatch": {"blend_enable": 1, "source_factor": 6,
                               "destination_factor": 7, "expanded_blend": 1},
            "following_opaque_dispatch": {"blend_enable": 1,
                                          "blend_word": "07060706",
                                          "expanded_blend": 0},
            "scope": "Shared original alpha setup and cleanup plus the complete zero-bone call branch. Native support and release must be established separately by lifecycle regressions; arbitrary other blend equations and mission coverage are not qualified."}


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
                raise ValueError("Changed original shared fallback blend producer was accepted")
    report["mutation_checks_passed"] = mutations
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
