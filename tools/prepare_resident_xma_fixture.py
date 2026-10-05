"""Extract the exact original bank without decoding; run the resident source test."""
from pathlib import Path
import argparse
import hashlib
import subprocess
import sys

sys.dont_write_bytecode = True
from extract_resource import main as extract

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    args = parser.parse_args()
    source = ROOT / 'Simpsons Game, The (USA)/frontend/frontend.str'
    if hashlib.sha256(source.read_bytes()).hexdigest() != '8d530622a0bad36a92c9ced3e278f3841ec5504980d893c6b8d523204cccd204':
        raise ValueError('Original frontend asset identity changed')
    directory = ROOT / 'build/resident-xma'
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / 'frontend.sbk'
    code = extract(['--input', str(source), '--entry', '1', '--name', 'frontend.sbk', '--output', str(output)])
    if code:
        return code
    return subprocess.run([str(args.test.resolve(strict=True)), str(output)], timeout=45).returncode


if __name__ == '__main__':
    sys.exit(main())
