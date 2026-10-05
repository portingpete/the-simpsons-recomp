"""Extract the pinned original story sound bank and exercise native quotas."""
from pathlib import Path
import argparse,hashlib,subprocess
from extract_resource import select_payload
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--test',type=Path,required=True);args=parser.parse_args()
    root=ROOT/'Simpsons Game, The (USA)'
    data,_=select_payload(root/'loc/loc/story_mode/story_mode_design.str',3,'Story_Mode_Design.sbk',root)
    assert hashlib.sha256(data).hexdigest()=='0392a01388db435d73cd81a565c9bc94273a13c53245cb30d596283594bdeca1'
    folder=ROOT/'build/story-resident-xma';folder.mkdir(parents=True,exist_ok=True)
    path=folder/'Story_Mode_Design.sbk'
    if path.exists():assert path.read_bytes()==data
    else:path.write_bytes(data)
    return subprocess.run([str(args.test.resolve(strict=True)),str(path),'--story'],timeout=150).returncode
if __name__=='__main__':raise SystemExit(main())
