"""Extract the original characters bank and exercise all qualified resident sources."""
import argparse
import hashlib
import subprocess
from pathlib import Path
from extract_resource import select_payload

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test',type=Path,required=True)
    args=parser.parse_args()
    root=ROOT/'Simpsons Game, The (USA)'
    data,_=select_payload(root/'simpsons_chars/simpsons_chars_global.str',4,'simpsons_chars_global.sbk',root)
    if len(data)!=1212431 or hashlib.sha256(data).hexdigest()!='4cedafc16ca0c681a0e4284ca8ae65e38ef19197ce29387a3ac8cac2ed50f078':
        raise RuntimeError('Original characters bank changed')
    folder=ROOT/'build/characters-resident-xma'
    folder.mkdir(parents=True,exist_ok=True)
    path=folder/'simpsons_chars_global.sbk'
    if path.exists():
        if path.read_bytes()!=data:raise RuntimeError('Extracted characters fixture changed')
    else:
        path.write_bytes(data)
    return subprocess.run([str(args.test.resolve(strict=True)),str(path),'--characters'],timeout=150).returncode

if __name__=='__main__':raise SystemExit(main())
