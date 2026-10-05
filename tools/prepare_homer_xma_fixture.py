"""Verify both original Homer banks and exercise their exact resident certificates."""
import argparse
import subprocess
from qualify_homer_resident_xma import BANKS, OUT, extract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', required=True)
    args = parser.parse_args()
    from pathlib import Path
    test_path = Path(args.test).resolve()
    if not test_path.is_file():
        parser.error('--test must identify an existing test executable')
    OUT.mkdir(parents=True, exist_ok=True)
    for bank, mode in zip(BANKS, ('--homer', '--homer-variant')):
        data, _ = extract(bank)
        path = OUT/bank['name']
        if path.exists() and path.read_bytes() != data:
            raise RuntimeError('Existing Homer fixture changed')
        path.write_bytes(data)
        try:
            result = subprocess.run([str(test_path), str(path), mode], timeout=180, capture_output=True, text=True)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(f'Homer fixture test failed: {exc}')
        if result.returncode:
            print(result.stdout, flush=True)
            print(result.stderr, flush=True)
            return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
