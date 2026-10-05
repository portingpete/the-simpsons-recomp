"""Bind offline tool binaries to the exact source snapshot that was built."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/generator-ninja'
STAMP=BUILD/'generator-identity.json'
PENDING=BUILD/'source-snapshot.json'

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def sources():
    return {p.relative_to(ROOT).as_posix():digest(p) for p in sorted((ROOT/'third_party/XenonRecomp').rglob('*')) if p.is_file()}
def binaries():
    return {str(p.relative_to(ROOT)).replace('\\','/'):digest(p) for p in
            (BUILD/'XenonRecomp/XenonRecomp.exe',BUILD/'XenonAnalyse/XenonAnalyse.exe',BUILD/'SimpsonsDisasm.exe')}
def verify(generator=None,analyser=None):
    stamp=json.loads(STAMP.read_text(encoding="utf-8"))
    if stamp['sources']!=sources() or stamp['binaries']!=binaries():
        raise RuntimeError('Offline tool identity changed; rebuild tools with tools/build.ps1')
    for path in (generator,analyser):
        if path and (path.resolve().relative_to(ROOT).as_posix() not in stamp['binaries']):
            raise RuntimeError('Requested generator/analyser is outside the verified tool build')
    return stamp
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('capture','seal','verify'))
    a=p.parse_args()
    if a.action=='capture':
        BUILD.mkdir(parents=True,exist_ok=True)
        PENDING.write_text(json.dumps(sources(),sort_keys=True), encoding="utf-8")
    elif a.action=='seal':
        before=json.loads(PENDING.read_text(encoding="utf-8"))
        if before!=sources(): raise RuntimeError('Generator sources changed during compilation; rerun build after edits settle')
        STAMP.write_text(json.dumps(dict(sources=before,binaries=binaries()),indent=2)+'\n', encoding="utf-8")
    else: verify()
if __name__=='__main__': main()
