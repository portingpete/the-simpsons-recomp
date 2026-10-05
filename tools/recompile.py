"""Offline generation and hash gate. Diagnostic output never implies completeness."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tomllib
from prepare_image import digest, GAME_SHA
import generator_identity

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/generated'

def native_sources():
    return [ROOT/name for name in json.loads((ROOT/'config/native_sources.json').read_text(encoding='utf-8'))]

def inputs():
    result=[Path(__file__), ROOT/'tools/prepare_image.py', ROOT/'tools/generator_identity.py', ROOT/'config/simpsons.toml',
            ROOT/'runtime/ppc_context.template.h', ROOT/'analysis/simpsons.unencrypted.xex',
            ROOT/'analysis/simpsons.pe', ROOT/'analysis/executable.json', ROOT/'analysis/switch_tables.toml',
            ROOT/'config/switch_overrides.toml']
    result+=[ROOT/'config/native_sources.json',ROOT/'config/inline_memory.json',
             ROOT/'runtime/guest_memory.h',ROOT/'runtime/aot_inline_memory.h',
             ROOT/'runtime/rigid_packet_owner.h',ROOT/'runtime/resource_audit.h',
             ROOT/'runtime/audit_stage.h',ROOT/'renderer/r16_index_validation.h',ROOT/'renderer/r16_strip_chunks.h',
             ROOT/'common/geometry_extent.h']+native_sources()
    # Inline validation and owner contracts also change native behavior. Pin
    # their complete source rather than trusting only translation-unit hashes.
    for directory in ('runtime','renderer','common'):
        result += sorted(p for p in (ROOT/directory).rglob('*')
                         if p.is_file() and p.suffix in ('.h','.hpp','.inl'))
    for f in sorted((ROOT/'third_party/XenonRecomp').rglob('*')):
        if f.is_file(): result.append(f)
    return result

def verify(diagnostic):
    generator_identity.verify()
    manifest=json.loads((OUT/'manifest.json').read_text(encoding="utf-8"))
    current={p.relative_to(ROOT).as_posix():digest(p) for p in inputs()}
    if current != manifest['inputs']:
        names=sorted(k for k in current.keys() | manifest['inputs'].keys() if current.get(k)!=manifest['inputs'].get(k))
        raise RuntimeError(f'AOT inputs changed; regenerate: {names[:12]}')
    for name,expected in manifest['outputs'].items():
        if digest(OUT/name)!=expected:
            raise RuntimeError(f'Generated output changed: {name}; fix source then regenerate')
    if manifest['semantic_diagnostics'] and not diagnostic:
        raise RuntimeError(f"{manifest['semantic_diagnostics']} semantic diagnostics; only explicit diagnostic build is permitted")
    print(f"AOT verified: {len(manifest['outputs'])} files, {manifest['semantic_diagnostics']} semantic diagnostics")

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--generator',type=Path)
    p.add_argument('--analyser',type=Path)
    p.add_argument('--diagnostic',action='store_true')
    p.add_argument('--verify',action='store_true')
    a=p.parse_args()
    if a.verify:
        verify(a.diagnostic)
        return
    if not a.generator or not a.analyser: p.error('--generator and --analyser are required')
    generator_identity.verify(a.generator,a.analyser)
    info=json.loads((ROOT/'analysis/executable.json').read_text(encoding="utf-8"))
    if info['input_sha256']!=GAME_SHA or info['image_sha256']!=digest(ROOT/'analysis/simpsons.pe'):
        raise RuntimeError('Image identity mismatch')
    OUT.mkdir(parents=True,exist_ok=True)
    previous={f.name:(f.read_bytes(),f.stat().st_mtime_ns) for f in OUT.glob('ppc_*') if f.is_file()}
    with (ROOT/'analysis/switch_analysis.log').open('w', encoding="utf-8") as log:
        subprocess.run([str(a.analyser.resolve()),str(ROOT/'analysis/simpsons.unencrypted.xex'),
                        str(ROOT/'analysis/switch_tables.toml')],stdout=log,stderr=subprocess.STDOUT,check=True, timeout=300)
    override_text=(ROOT/'config/switch_overrides.toml').read_text(encoding="utf-8")
    original=(ROOT/'analysis/simpsons.pe').read_bytes()
    for table in tomllib.loads(override_text)['switch']:
        for prefix in ('evidence','table'):
            expected=bytes.fromhex(table[prefix+'_hex'])
            offset=table[prefix+'_address']-info['image_base']
            if offset<0 or original[offset:offset+len(expected)]!=expected:
                raise RuntimeError(f'Switch evidence mismatch at {table["base"]:08x}')
    with (ROOT/'analysis/switch_tables.toml').open('a', encoding="utf-8") as stream:
        stream.write('\n'+override_text)
    config=tomllib.loads((ROOT/'config/simpsons.toml').read_text(encoding="utf-8"))
    main=config['main']
    for name,addresses in info['helpers'].items():
        if len(addresses)!=1 or main.get(name+'_address')!=addresses[0]:
            raise RuntimeError(f'Helper signature mismatch: {name}')
    for key in ('file_path','switch_table_file_path'):
        main[key]=Path(os.path.relpath((ROOT/'config'/main[key]).resolve(),OUT.resolve())).as_posix()
    main['out_directory_path']='.'
    def toml_value(value):
        if isinstance(value,dict): return '{'+', '.join(f'{k} = {toml_value(v)}' for k,v in value.items())+'}'
        if isinstance(value,list): return '['+', '.join(toml_value(v) for v in value)+']'
        return json.dumps(value)
    text='[main]\n'+'\n'.join(f'{k} = {toml_value(v)}' for k,v in main.items())+'\n'
    for hook in config.get('midasm_hook',[]):
        if 'evidence_hex' in hook:
            expected=bytes.fromhex(hook['evidence_hex'])
            offset=hook['address']-info['image_base']
            if not expected or offset<0 or original[offset:offset+len(expected)]!=expected:
                raise RuntimeError(f'Native hook evidence mismatch at {hook["address"]:08x}')
        text+='\n[[midasm_hook]]\n'+'\n'.join(f'{k} = {json.dumps(v)}' for k,v in hook.items())+'\n'
    (OUT/'generation.toml').write_text(text, encoding="utf-8")
    generation_inputs={f.relative_to(ROOT).as_posix():digest(f) for f in inputs()}
    with (OUT/'generation.log').open('w', encoding="utf-8") as log:
        run=subprocess.run([str(a.generator.resolve()),str(OUT/'generation.toml'),
                            str(ROOT/'runtime/ppc_context.template.h')],stdout=log,stderr=subprocess.STDOUT, timeout=300)
    log=(OUT/'generation.log').read_text(encoding="utf-8")
    counts=re.findall(r'Semantic diagnostics: (\d+)',log)
    units=re.findall(r'^Translation units: (\d+)',log,re.M)
    if not counts or not units or run.returncode not in (0,1):
        raise RuntimeError(f'Generator failed ({run.returncode}); inspect build/generated/generation.log')
    semantic=int(counts[-1])
    if run.returncode != int(semantic!=0): raise RuntimeError('Generator status contradicts diagnostic count')
    chunks=[f'ppc_recomp.{i}.cpp' for i in range(int(units[-1]))]
    header=(OUT/'ppc_recomp_shared.h').read_text(encoding="utf-8")
    names=set(re.findall(r'PPC_EXTERN_FUNC\((__imp__\w+)\)',header))
    native='\n'.join(f.read_text(encoding="utf-8") for f in native_sources())
    implemented=set(re.findall(r'PPC_FUNC\((__imp__\w+)\)',native))
    missing=sorted(names-implemented)
    (OUT/'ppc_imports.cpp').write_text('#include "ppc_context.h"\n'+''.join(
        f'PPC_FUNC({n}) {{ PPC_RECOMP_FAILURE(ctx, uint32_t(ctx.lr), "unimplemented import {n}"); }}\n' for n in missing))
    descriptors=info['imports']
    code={(d['library'],d['ordinal']) for d in descriptors if d['kind']==1}
    data=[d for d in descriptors if d['kind']==0 and (d['library'],d['ordinal']) not in code]
    meta='#pragma once\n#include <cstdint>\n'
    meta+=f'inline constexpr char kImageSha256[]="{info["image_sha256"]}";\n'
    meta+=f'inline constexpr char kDerivedXexSha256[]="{info["derived_xex_sha256"]}";\n'
    for key,value in info['tls'].items(): meta+=f'inline constexpr uint32_t kTls_{key}={value}u;\n'
    meta+='struct DataImport { uint32_t address; const char* name; };\n'
    meta+='inline constexpr DataImport kDataImports[] = {\n'+''.join(
        f'{{0x{d["address"]:08x}, "{d["name"]}"}},\n' for d in data)+'};\n'
    thunks={(d['library'],d['ordinal']):d['address'] for d in descriptors if d['kind']==1}
    meta+='struct CodeImport { uint32_t slot,thunk; };\ninline constexpr CodeImport kCodeImports[] = {\n'
    for d in descriptors:
        target=thunks.get((d['library'],d['ordinal']))
        if d['kind']==0 and target is not None: meta+=f'{{0x{d["address"]:08x},0x{target:08x}}},\n'
    meta+='};\n'
    (OUT/'ppc_image_metadata.h').write_text(meta, encoding="utf-8")
    files=[OUT/n for n in chunks+['ppc_func_mapping.cpp','ppc_imports.cpp']]+sorted(OUT.glob('ppc_*.h'))
    for f in files:
        if re.search(r'// ERROR(?:\s|:)',f.read_text(encoding="utf-8")):
            raise RuntimeError(f'Silent translation error in {f}')
    (OUT/'sources.cmake').write_text('set(SIMPSONS_PPC_SOURCES\n'+''.join(
        f'  "${{SIMPSONS_GENERATED_DIR}}/{n}"\n' for n in chunks+['ppc_func_mapping.cpp','ppc_imports.cpp'])+')\n')
    files += [OUT/'sources.cmake', OUT/'generation.toml']
    if generation_inputs!={f.relative_to(ROOT).as_posix():digest(f) for f in inputs()}:
        raise RuntimeError('Inputs changed during AOT generation; rerun after edits settle')
    generator_identity.verify(a.generator,a.analyser)
    manifest=dict(format=1, semantic_diagnostics=semantic, profile='diagnostic' if semantic else 'zero-reported-diagnostics',
                  generator_sha256=digest(a.generator),analyser_sha256=digest(a.analyser),
                  missing_imports=missing,inputs={f.relative_to(ROOT).as_posix():digest(f) for f in inputs()},
                  outputs={f.name:digest(f) for f in files})
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n', encoding="utf-8")
    for f in files:
        old=previous.get(f.name)
        if old and old[0]==f.read_bytes(): os.utime(f,ns=(f.stat().st_atime_ns,old[1]))
    print(f'Generated {len(chunks)} chunks; {len(missing)} explicit unimplemented imports')
    verify(a.diagnostic)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as e:
        print(f'AOT gate: {e}',file=sys.stderr)
        sys.exit(1)
