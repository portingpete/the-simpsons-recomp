"""Extract the pinned original loc bank and exercise native resident loop quotas."""
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
    data,_=select_payload(root/'loc/loc.str',2,'loc.sbk',root)
    assert hashlib.sha256(data).hexdigest()=='702aebda44b3a0f36866b35b89d00c0fd7d5cbfa8dfd7409df4cc562122e71eb'
    folder=ROOT/'build/loc-resident-xma'
    folder.mkdir(parents=True,exist_ok=True)
    path=folder/'loc.sbk'
    if path.exists():
        assert path.read_bytes()==data
    else:
        path.write_bytes(data)
    return subprocess.run([str(args.test.resolve(strict=True)),str(path),'--loc'],timeout=150).returncode

if __name__=='__main__':
    raise SystemExit(main())
