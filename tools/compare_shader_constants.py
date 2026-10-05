from pathlib import Path
import struct
import sys


def rows(path: Path):
    data = path.read_bytes()
    if len(data) % 16:
        raise SystemExit(f"{path}: size {len(data)} is not a float4 register bank")
    return [struct.unpack_from(">4f", data, at) for at in range(0, len(data), 16)]


paths = [Path(arg) for arg in sys.argv[1:]]
if len(paths) < 2:
    raise SystemExit("usage: compare_shader_constants.py <bank> <bank> [...]")

banks = [rows(path) for path in paths]
for register in range(min(map(len, banks))):
    values = [bank[register] for bank in banks]
    if any(value != values[0] for value in values[1:]):
        print(f"c{register:02d}")
        for path, value in zip(paths, values):
            print(f"  {path.name}: " + ", ".join(f"{component:.9g}" for component in value))
