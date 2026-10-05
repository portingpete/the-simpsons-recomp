"""Verify the reached ANSI descriptor ABI using pinned original image bytes."""
from pathlib import Path
import hashlib
import json
import sys

sys.dont_write_bytecode = True
import analyze_poststart_integration as original

root = Path(__file__).resolve().parents[1]
data = (root / 'analysis/simpsons.pe').read_bytes()
original.validate_identity(data)
pins = {
    0x82B750B4: 0x7F84E378,  # r4 = original source pointer
    0x82B750B8: 0x38610068,  # r3 = SP+68 descriptor
    0x82B750BC: 0x4814D869,  # BL original RtlInitAnsiString thunk
    0x82B750C0: 0xA1610068,  # read BE16 Length from descriptor
    0x82B750CC: 0x7D6BE214,  # original source pointer + Length
    0x82B750D4: 0x896BFFFF,  # last source byte before terminator
    0x82B75128: 0x38E10068,  # descriptor pointer for object attributes
    0x82B75138: 0x90E1007C,  # ObjectName at object attributes+4
}
for pc, expected in pins.items():
    if original.word(data, pc) != expected:
        raise ValueError(f'Original ANSI contract changed at {pc:08X}')
if original.branch(0x82B750BC, pins[0x82B750BC]) != (0x82CC2924, True):
    raise ValueError('ANSI call target changed')
report = {
    'image_sha256': hashlib.sha256(data).hexdigest(),
    'pins': {f'{pc:08X}': f'{word:08X}' for pc, word in pins.items()},
    'scope': 'Reached source/descriptor/length/object-attribute ABI; Windows contract and actual native oracle qualify initialization behavior',
}
(root / 'analysis/native-ansi-contract.json').write_text(json.dumps(report, indent=2)+'\n', encoding="utf-8")
print(f'PASS original ANSI contract: {len(pins)} words and exact import branch; original image hash verified')
