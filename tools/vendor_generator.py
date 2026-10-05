"""Import a local offline translator snapshot with per-file provenance."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('K:/DarkRecomp/refs/UnleashedRecomp/tools/XenonRecomp'))
    a=p.parse_args()
    dest=ROOT/'third_party/XenonRecomp'
    if dest.exists():
        raise RuntimeError('Existing translator must be edited or reviewed explicitly; refusing overwrite')
    shutil.copytree(a.source,dest,ignore=shutil.ignore_patterns('.git','build','__pycache__','out','.vs'))
    inventory={f.relative_to(dest).as_posix():hashlib.sha256(f.read_bytes()).hexdigest()
               for f in sorted(dest.rglob('*')) if f.is_file()}
    (ROOT/'third_party/generator_origin.json').write_text(json.dumps(dict(
        source=str(a.source),upstream='https://github.com/hedge-dev/XenonRecomp',
        note='Includes local DarkRecomp generator fixes; hashes describe imported source before Simpsons changes.',
        files=inventory),indent=2)+'\n')
    print(f'Imported {len(inventory)} source/dependency files. Reference unchanged.')

if __name__=='__main__': main()
