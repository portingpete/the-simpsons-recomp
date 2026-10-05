"""Preserve authored source and exact generation evidence in a local zip checkpoint."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('name')
p.add_argument('--exclude',action='append',default=None,help='Exact workspace-relative authored file still being edited outside this checkpoint')
p.add_argument('--include-file',action='append',default=None,help='Additional exact workspace-relative evidence file')
p.add_argument('--current-only',action='store_true',help='Preserve source/dependencies and explicitly selected evidence without historical build logs')
a=p.parse_args()
if not a.name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in a.name):
    p.error('Use a simple checkpoint name')
out=ROOT/'checkpoints'/f'{a.name}.zip'
out.parent.mkdir(exist_ok=True)
files=[f for d in ('app','runtime','renderer','audio','tools','config','tests','docs','third_party') for f in (ROOT/d).rglob('*')
       if f.is_file() and '__pycache__' not in f.parts]
files += [ROOT/'CMakeLists.txt',ROOT/'.gitignore',ROOT/'README.md',ROOT/'STATUS.md',ROOT/'Play The Simpsons Game.cmd']
authored=set(files)
files += [f for f in (ROOT/'analysis').glob('*.json')]
files += [f for f in (ROOT/'analysis/ida').rglob('*') if f.is_file() and f.suffix in ('.json','.txt')]
files += [f for f in (ROOT/'analysis/reagent').rglob('*') if f.is_file() and f.suffix in ('.json','.txt','.md','.tsv')]
files += [f for f in (ROOT/'build/generated').glob('manifest.json')]
files += [f for f in (ROOT/'build').glob('boot-*.log')]
files += [f for f in (ROOT/'build').glob('*-build.log')]
files += [f for f in (ROOT/'build/generator-ninja').glob('*identity.json')]
files += [f for f in (ROOT/'build/decoded-textures').glob('manifest.json')]
files += [f for f in (ROOT/'build/loading-art').glob('manifest.json')]
files += [f for f in (ROOT/'build').glob('native-graphics-*.log')]
files += [f for f in (ROOT/'build').glob('native-screen-*.log')]
files += [f for f in (ROOT/'build').glob('native-driver-*.log')]
files += [f for f in (ROOT/'build').glob('native-state-*.log')]
files += [f for f in (ROOT/'build').glob('application-*-verification.log')]
files += [f for f in (ROOT/'build').glob('native-material-*.log')]
files += [f for f in (ROOT/'build').glob('native-viewport-*.log')]
files += [f for f in (ROOT/'build').glob('native-present*.log')]
files += [f for f in (ROOT/'build').glob('native-recording-*.log')]
files += [f for f in (ROOT/'build').glob('native-thread-*.log')]
files += [f for f in (ROOT/'build').glob('native-notification-*.log')]
files += [f for f in (ROOT/'build').glob('native-mutant-*.log')]
files += [f for f in (ROOT/'build').glob('native-audio-*.log')]
files += [f for f in (ROOT/'build').glob('native-ansi-*.log')]
files += [f for f in (ROOT/'build').glob('native-configuration-*.log')]
files += [f for f in (ROOT/'build/audio-probe').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.csv','.cpp')]
files += [f for f in (ROOT/'build/audio-fixtures').glob('manifest.json')]
files += [f for f in (ROOT/'build/audio-multilayer').glob('*') if f.is_file() and f.suffix in ('.log','.json','.csv','.cpp')]
files += [f for f in (ROOT/'build/dac-pcm').glob('*') if f.is_file() and f.suffix in ('.log','.json')]
files += [f for f in (ROOT/'build/audio-output').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py')]
files += [f for f in (ROOT/'build/dac-sdk').glob('*') if f.is_file() and f.suffix in ('.json','.py','.txt')]
files += [f for f in (ROOT/'build/dac-gain').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py')]
files += [f for f in (ROOT/'build/dac-routing').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/dac-processing').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/native-tick').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt','.cpp')]
files += [f for f in (ROOT/'build/notification-audio').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/exm0-next').glob('*') if f.is_file() and f.suffix in ('.json','.py','.txt')]
files += [f for f in (ROOT/'build/native-configuration').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/xma-source').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt','.cpp')]
files += [f for f in (ROOT/'build/xma-source/fixtures').glob('manifest.json')]
files += [f for f in (ROOT/'build/native-player-entry').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/local-player-bridge').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/native-local-players').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build').glob('native-local-player-*.log')]
files += [f for f in (ROOT/'build').glob('native-word-switch-*.log')]
files += [f for f in (ROOT/'build').glob('native-status-*.log')]
files += [f for f in (ROOT/'build/audio-reader-lifetime').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/audio-reader-lifecycle').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/graphics-cpu-startup').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/graphics-startup-services').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build').glob('native-graphics-cpu-*.log')]
files += [f for f in (ROOT/'build').glob('native-first-effect-*.log')]
files += [f for f in (ROOT/'build').glob('native-effect-catalog-*.log')]
files += [f for f in (ROOT/'build').glob('native-effect-reflection-*.log')]
files += [f for f in (ROOT/'build').glob('native-shadow-camera-*.log')]
files += [f for f in (ROOT/'build').glob('native-shadow-texture-*.log')]
for evidence in ('first-effect-contract','first-effect-lifecycle','fourtap-shaders','im2d-native','im2d-depth','itxd-lifetime','itxd-font187','movie-shader','movie-geometry','movie-backend','movie-integration','native-process-sampling','thread-exit-status','movie-composition','fullsize-surface-alias'):
    files += [f for f in (ROOT/'build'/evidence).glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
files += [f for f in (ROOT/'build/itxd-font').glob('*') if f.is_file() and f.suffix in ('.json','.tiled','.bc2','.metadata')]
files += [f for f in (ROOT/'build/movie-shader').glob('*') if f.is_file() and f.suffix in ('.cso','.asm')]
for evidence in ('fourtap-caller','effect-catalog','effect-pool-cpu','effect-finalizers','effect-catalog-lifecycle',
                 'shadow-raster','shadow-camera-lifecycle','shadow-textures','writable-texture','shadow-texture-lifecycle','quad-declarations','unbuffered-assets','im2d-upload',
                 'post-effect-catalog','effect-reflection','effect-reflection-compatibility',
                 'effect-reflection-consumers','effect-reflection-all49','effect-reflection-gateway',
                 'effect-finalizer-integration','reflection-cubemap','builtin-textures','crossfade-texture','rectangular-camera','content-enumeration','native-controllers','camera-selection','viewport-copy','viewport-reset','original-screen'):
    files += [f for f in (ROOT/'build'/evidence).rglob('*') if f.is_file() and '__pycache__' not in f.parts
              and f.suffix in ('.log','.json','.py','.txt','.md','.cpp','.h')]
files += [f for f in (ROOT/'build/generated').glob('effect_catalog.h')]
files += [f for f in (ROOT/'build/exm0-admission').glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.txt')]
# Preserve the independently built codec, licenses, exact upstream source
# archive and patch. Derived original-game audio packets/PCM are never added.
codec_root=ROOT/'build/audio-codec'
files += [f for f in codec_root.glob('*') if f.is_file() and f.suffix in ('.log','.json','.py','.c','.patch')]
codec_provenance=codec_root/'install/PROVENANCE.json'
if codec_provenance.is_file():
    files.append(codec_provenance)
    try:
        provenance_doc=json.loads(codec_provenance.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Codec provenance is not valid JSON: {exc}") from exc
    for name,expected in provenance_doc['artifacts'].items():
        artifact=(codec_root/name).resolve(strict=True)
        if not artifact.is_relative_to(codec_root.resolve()):
            raise RuntimeError('Codec checkpoint artifact escapes owned directory')
        if hashlib.sha256(artifact.read_bytes()).hexdigest()!=expected:
            raise RuntimeError(f'Codec checkpoint artifact changed: {name}')
        files.append(artifact)
files += [f for f in (ROOT/'build/captures').glob('native-*.jpg')]
files += [f for f in (ROOT/'build/captures').glob('native-*.png')]
files += [f for d in (ROOT/'build/captures').glob('native-loading-*') for f in d.glob('*') if f.is_file() and f.suffix in ('.png','.rgb10a2','.json')]
files += [f for f in (ROOT/'build/shaders').glob('*.h')]
if a.current_only:
    files=[f for f in files if f in authored or f.is_relative_to(codec_root)
           or f.parent==ROOT/'analysis' or f.parent==ROOT/'build/generator-ninja'
           or f.parent==ROOT/'build/generated' or f.parent==ROOT/'build/shaders']
for name in (a.include_file or []):
    candidate=(ROOT/name).resolve()
    if not candidate.is_relative_to(ROOT.resolve()) or not candidate.is_file():
        p.error('Additional evidence must be an existing workspace-relative file')
    files.append(candidate)
excluded=set()
for name in (a.exclude or []):
    candidate=(ROOT/name).resolve()
    if not candidate.is_relative_to(ROOT.resolve()) or candidate.is_dir():
        p.error('Exclusions must be exact workspace-relative file paths')
    excluded.add(candidate.relative_to(ROOT.resolve()).as_posix())
selected=[f for f in sorted(set(files)) if f.relative_to(ROOT).as_posix() not in excluded]
print(f'Archiving {len(selected)} files ({sum(f.stat().st_size for f in selected)} source bytes)',flush=True)
manifest={}
with zipfile.ZipFile(out,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in selected:
        name=f.relative_to(ROOT).as_posix();digest=hashlib.sha256()
        with f.open('rb') as source,z.open(name,'w',force_zip64=True) as target:
            while block:=source.read(1024*1024):
                digest.update(block);target.write(block)
        manifest[name]=digest.hexdigest()
    if excluded:
        data=(json.dumps(sorted(excluded),indent=2)+'\n').encode()
        z.writestr('checkpoint-exclusions.json',data)
        manifest['checkpoint-exclusions.json']=hashlib.sha256(data).hexdigest()
    z.writestr('checkpoint-manifest.json',json.dumps(manifest,indent=2)+'\n')
with out.open('rb') as source: archive_hash=hashlib.file_digest(source,'sha256').hexdigest()
print(f'{out}: {len(manifest)} files; SHA256 {archive_hash}')
