"""Fetch the byte-pinned public NVIDIA SDK; never load or execute its binaries."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def fetch_sdk(destination: Path, verify_only: bool = False) -> dict:
    manifest = json.loads((ROOT / 'config/nvidia_dlss_sdk.json').read_bytes())
    commit = manifest['commit']
    if len(commit) != 40 or any(c not in '0123456789abcdef' for c in commit):
        raise ValueError('Invalid pinned SDK commit')
    destination = destination.resolve()

    def one(item):
        relative, metadata = item
        path = PurePosixPath(relative)
        if path.is_absolute() or any(p in ('', '.', '..') for p in path.parts):
            raise ValueError('Invalid SDK relative path')
        target = destination.joinpath(*path.parts)
        if not target.resolve().is_relative_to(destination):
            raise ValueError('SDK file resolves outside its requested destination')

        def valid(data):
            return (len(data) == metadata['bytes'] and
                    hashlib.sha256(data).hexdigest() == metadata['sha256'] and
                    hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == metadata['git_blob'])

        if target.exists():
            if not valid(target.read_bytes()):
                raise ValueError(f'Existing SDK file differs from the pinned source: {relative}')
            return relative, 'verified'
        if verify_only:
            raise FileNotFoundError(f'Pinned SDK file missing: {relative}')
        url = f'https://raw.githubusercontent.com/NVIDIA/DLSS/{commit}/{relative}'
        request = urllib.request.Request(url, headers={'User-Agent': 'SimpsonsNative-pinned-sdk-fetch'})
        with urllib.request.urlopen(request, timeout=90) as response:
            data = response.read(metadata['bytes'] + 1)
        if not valid(data):
            raise ValueError(f'Download size, SHA-256 or Git blob differs: {relative}')
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=target.parent, prefix='ngx-fetch-', suffix='.tmp', delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            if target.exists():
                if not valid(target.read_bytes()):
                    raise ValueError(f'SDK destination changed while downloading: {relative}')
            else:
                os.replace(temporary, target)
                temporary = None
        finally:
            if temporary is not None:
                temporary.unlink()
        return relative, 'downloaded'

    with ThreadPoolExecutor(max_workers=4) as pool:
        results = dict(pool.map(one, manifest['files'].items()))
    return {'repository': manifest['repository'], 'tag': manifest['tag'], 'commit': commit,
            'destination': str(destination), 'files': results,
            'executed_downloads': False, 'license': str(destination / manifest['license'])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, default=ROOT / 'build/deps/nvidia-dlss-v310.9.1')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    print(json.dumps(fetch_sdk(args.destination, args.verify_only), indent=2))


if __name__ == '__main__':
    main()
