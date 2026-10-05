"""Offline PPC/VMX128 disassembly helper for verified image addresses."""
import argparse
from pathlib import Path
import subprocess
from prepare_image import digest
import json

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('address',type=lambda v:int(v,0))
    p.add_argument('--count',type=int,default=40)
    a=p.parse_args()
    if not 1<=a.count<=1024:
        p.error('--count must be between 1 and 1024')
    try:
        meta=json.loads((ROOT/'analysis/executable.json').read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f'Cannot read executable metadata: {exc}')
    path=ROOT/'analysis/simpsons.pe'
    try:
        if digest(path)!=meta['image_sha256']: raise RuntimeError('Image hash mismatch')
    except (OSError, KeyError, ValueError) as exc:
        raise SystemExit(f'Image verification failed: {exc}')
    offset=a.address-meta['image_base']
    if offset<0 or a.count<1 or offset+a.count*4>path.stat().st_size or offset%4:
        raise ValueError('Invalid disassembly range')
    subprocess.run([str(ROOT/'build/generator-ninja/SimpsonsDisasm.exe'),str(path),
                    hex(meta['image_base']),hex(a.address),str(a.count)],check=True,timeout=60)


if __name__=='__main__':
    main()
