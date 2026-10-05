"""Read-only original producer proof for alpha-to-opaque skin blend inheritance.

The native lifecycle regression executes both complete original dispatches;
this verifier separately pins their conditional setters and cleanup bytes.
No game bytes are written or redistributed.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
PINS = {
    # Opaque alpha-byte=0 skips every blend setter; otherwise expanded=1,
    # enable=1, source=6 and destination=7 are published by the original owner.
    0x827401CC: "2B1E0000 419A0060 38A00000 38800001 38600044 4BF77789 38A00001 38800001 38600006 4BF77779 38A00001 38800006 38600009 4BF77769 38A00001 38800007 3860000A 4BF77759",
    # Opaque skinned work changes depth/cull only, then both select their real
    # packet object, typed FX owner and submeshes for the skin draw producer.
    0x82740230: "807F0000 81630024 2B0B0000 4099005C 2B1E0000 409A0034 38A00001 38800001 38600003 4BF77715 38A00001 38800002 38600005 4BF77705 38A00001 38800001 38600003 4BF776F5 807F0000 38C00000 891F000C 80BF0018 809F0004 80E30010 4BFC13A9 4800001C",
    # Alpha cleanup publishes expanded=0, optionally restores depth write;
    # there is no blend-disable or factor-reset call before return.
    0x827402B0: "2B1E0000 419A0030 38A00000 38800000 38600044 4BF776A5 578B063E 2B0B0000 419A0014 38A00001 38800001 38600003 4BF77689 38600000",
}


def verify(image, check_hash=True):
    if check_hash and hashlib.sha256(image).hexdigest() != IMAGE_SHA:
        raise ValueError("Original image hash changed")
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original skin blend producer changed at {address:08X}")
    return {"image_sha256": IMAGE_SHA,
            "producer_sites": ["827401CC..82740210", "82740230..82740294", "827402B0..827402E4"],
            "alpha_dispatch": {"blend_enable": 1, "source_factor": 6, "destination_factor": 7, "expanded_blend": 1},
            "following_opaque_dispatch": "Retains blend enable and factors after the original alpha cleanup clears expanded blending to zero.",
            "native_lifecycle_test": "OriginalSkinTexturedInheritedPass invokes original 8273B4D0 alpha then opaque without a fixture blend reset, checks effective 07060706/enabled1/expanded0 and distinct pixels, and retires original declarations/FX/cache owners.",
            "scope": "Exact original inheritance sequence; this does not qualify arbitrary other blend equations or establish mission coverage."}


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
                raise ValueError("Changed original skin blend producer was accepted")
    report["mutation_checks_passed"] = mutations
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
