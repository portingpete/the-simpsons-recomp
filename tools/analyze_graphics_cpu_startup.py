"""Bounded original CPU constructor evidence, using pinned image bytes.

The parent initializer is context, not a claim that its other services are
ported. Allocator/save/restore helpers are existing original CPU services.
"""
from pathlib import Path
import argparse
import json
import subprocess

import analyze_poststart_integration as evidence

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "analysis/native-graphics-cpu-startup.json"
PARENT = 0x826B0DF8
FUNCTIONS = (PARENT, 0x826B6F60, 0x826B5AC8, 0x826B5CA8, 0x826B5D68,
             0x826B44B8, 0x826B7160, 0x826B7600, 0x826B59A8, 0x826B5B88)
# Reviewed leaf/code extents, not inferred from the nearest .pdata record.
# Matrix helpers end at their BLR. Memcpy has internal return blocks and ends
# with an unconditional branch into its own copy loop.
LEAVES = {0x8247F310: 0x184, 0x8247F730: 0x2B0,
          0x827225B0: 12, 0x82A3CD80: 0x458}
EXTERNAL_CPU = {0x8269BD70, 0x8269BE40, 0x8269BEB0, 0x8269BF10,
                0x82A3C3C0, 0x82A3C3C8, 0x82A3C3CC,
                0x82A3C410, 0x82A3C418, 0x82A3C41C}
CONSTANTS = {0x820B71DC: 0x826B7600, 0x821DD39C: 0x3E800000,
             0x821DD0D8: 0, 0x821DD110: 0xBF800000,
             0x82000BB0: 0x3F800000, 0x82001894: 0x3DCCCCCD}


def inspect():
    image = (ROOT / "analysis/simpsons.pe").read_bytes()
    evidence.validate_identity(image)
    pdata = evidence.layout(image)[1]
    bodies = []
    for address in sorted(set(FUNCTIONS) | LEAVES.keys()):
        size = LEAVES[address] if address in LEAVES else pdata[address][0]
        try:
            output = subprocess.check_output([
                str(ROOT / "build/generator-ninja/SimpsonsDisasm.exe"),
                str(ROOT / "analysis/simpsons.pe"), "0x82000000",
                hex(address), str(size // 4)], text=True, timeout=60)
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(f'Disassembler failed for {hex(address)}: {exc}')
        rows = evidence.checked_decode(image, address, size, output)
        edges = []
        for row in rows:
            pc, word = int(row["pc"], 16), int(row["word"], 16)
            edge = evidence.branch(pc, word)
            if edge:
                target, linked = edge
                edges.append({"pc": row["pc"], "target": evidence.hx(target), "linked": linked})
                if address != PARENT and not address <= target < address + size:
                    assert target in FUNCTIONS or target in LEAVES or target in EXTERNAL_CPU, (hex(pc), hex(target))
            if address != PARENT:
                assert not row["assembly"].startswith(("bctr", "mtctr")) or address == 0x82A3CD80, row
        if address in (0x8247F310, 0x8247F730, 0x827225B0):
            assert int(rows[-1]["word"], 16) == 0x4E800020
            assert not edges, "Reviewed leaf acquired a direct call/branch"
        bodies.append({"address": evidence.hx(address), "size": size,
                       "extent": "reviewed code/leaf" if address in LEAVES else ".pdata",
                       "sha256": evidence.sha(evidence.span(image, address, size)),
                       "instructions": rows, "direct_edges": edges})
    for address, value in CONSTANTS.items():
        assert evidence.word(image, address) == value, hex(address)
    # Address arithmetic must account for the signed displacement.
    assert evidence.word(image, 0x826B0E04) == 0x3D6082D6
    assert evidence.word(image, 0x826B0E1C) == 0x838BDA74
    assert 0x82D60000 + (0xDA74 - 0x10000) == 0x82D5DA74
    return {"schema": 1, "image_sha256": evidence.IMAGE_SHA,
            "scope": "First CPU graphics object and empty-array destructor; parent context only",
            "context_alias": "0x82D5DA74", "singleton": "0x82D08BFC",
            "allocation_size": 0x260, "array_count": 6,
            "borrowed_global": "0x82D6D2F8", "optional_flag": "0x82D5DB74",
            "optional_effect": "0x82E2D2D8",
            "constants": {evidence.hx(a): evidence.hx(v) for a, v in CONSTANTS.items()},
            "functions": bodies}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    result = inspect()
    text = json.dumps(result, indent=2) + "\n"
    if args.verify:
        assert REPORT.read_text(encoding="utf-8") == text, "CPU constructor evidence differs"
    else:
        REPORT.write_text(text, encoding="utf-8")
    print(f"PASS graphics CPU evidence: {len(result['functions'])} bodies, "
          f"{sum(len(f['instructions']) for f in result['functions'])} byte-checked words; parent not fully qualified")


if __name__ == "__main__":
    main()
