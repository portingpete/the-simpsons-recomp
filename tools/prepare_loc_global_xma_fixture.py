"""Verify the original loc_global bank and test its exact resident certificates."""
import argparse
from pathlib import Path
import subprocess
from qualify_loc_global_resident_xma import OUT, extract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    args = parser.parse_args()
    data, _ = extract()
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT/'loc_global.sbk'
    if path.exists() and path.read_bytes() != data:
        raise RuntimeError('Existing loc_global fixture changed')
    path.write_bytes(data)
    return subprocess.run([str(args.test.resolve(strict=True)),str(path),'--loc-global'],timeout=160).returncode


if __name__ == '__main__':
    raise SystemExit(main())
