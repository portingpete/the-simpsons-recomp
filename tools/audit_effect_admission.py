"""Bounded offline admission audit of every registered original effect/pass.

Separates metadata identity, native shader artifacts, explicit runtime profile
selection and observed log evidence. Missing artifacts are latent admission
gaps, not evidence that a pass has been reached or that gameplay will crash.
No effect, shader, particle or resource is executed or silently discarded.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
BASE=0x82000000
IMAGE_SHA='6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0'
CATALOGS=(('analysis/native-effect-catalog.json','031241d1bff10c771433ce0c5154c05a93628a4b83b114c0d0bfcfdadea2a43b'),
          ('analysis/native-post-effect-catalog.json','325d8f64aff8e6d260cabecffc06f923cd2c8b426b1bba917bd30e16a1e4338e'))


def need(ok,why):
    if not ok:raise ValueError(why)


def sha(data):return hashlib.sha256(data).hexdigest()


def artifact_inventory(source):
    start=source.index('NativeBackend::createMaterialArtifact(')
    body=source[start:source.index('void NativeBackend::validateEdgeShaders(',start)]
    cases=list(re.finditer(r'case\s+(0x[0-9A-Fa-f]{8})\s*:',body));pending=[];result={}
    for i,m in enumerate(cases):
        address=int(m[1],16);need(address not in result and address not in pending,'Duplicate native artifact case')
        pending.append(address)
        end=cases[i+1].start()if i+1<len(cases)else body.index('default:',m.end())
        create=re.search(r'Create(Vertex|Pixel)Shader\((k\w+),',body[m.end():end])
        if create:
            for address in pending:result[address]=dict(stage=create[1].lower(),artifact=create[2])
            pending=[]
    need(not pending,'Native artifact cases do not resolve to an actual shader creation')
    return result


def mono_declared_pairs(body):
    """Static selector declarations only; no native draw or lifetime credit."""
    body=re.sub(r'//[^\n]*|/\*.*?\*/',' ',body,flags=re.S)
    opaque=(0x82120C04,0x82122BD4);alpha=(0x82121BE8,0x82122D38)
    need(all(f'0x{address:08X}'in body for address in opaque),'Dedicated mono opaque guard changed')
    # The old Burp-only continuation remains opaque. Mere alpha literals in a
    # comment, rejected diagnostic or metadata table must not add admission.
    selects_alpha=all(token in body for token in ('c.r31.u32&255','c.r5.u32==technique',
        'passes[alpha?1u:0u]','0x82121BE8','0x82122D38'))
    return (opaque,alpha) if selects_alpha else (opaque,)


def declared_selection():
    # These are precise existing profile selectors, not string-presence claims.
    result={}
    for name in ('rigid_profile.h','skin_profile.h'):
        source=(ROOT/'runtime'/name).read_text()
        # Accept literal direct returns and named profile construction. Both
        # carry exact source->front-pair tuples; booleans never select alpha.
        for m in re.finditer(r'if\(source==(0x[0-9A-Fa-f]{8})\)(?:return\s*|\s*\{\s*\w+Profile\s+p)\{(0x[0-9A-Fa-f]{8}),(0x[0-9A-Fa-f]{8}),',source):
            pair=tuple(int(m[i],16)for i in (2,3));result[pair]=dict(file='runtime/'+name,line=source[:m.start()].count('\n')+1)
    # Rigid alpha tuples are selected by rigidAlphaPass (and skin by
    # skinAlphaPass). All represented selectors are checked above.
    source=(ROOT/'runtime/engine_effects.cpp').read_text()
    for m in re.finditer(r'\{(0x[0-9A-Fa-f]{8}),0x[0-9A-Fa-f]{8},(0x[0-9A-Fa-f]{8}),(0x[0-9A-Fa-f]{8}),[^\n]+"(?:EDGE|AA|EDGEAA)"\}',source):
        result[tuple(int(m[i],16)for i in (2,3))]=dict(file='runtime/engine_effects.cpp',line=source[:m.start()].count('\n')+1)
    # Special VS-only/mono routes have explicit checked pass identities.
    for pair,function in (((0x820C2FA0,0),'beginShadowDepth'),((0x820C1E6C,0x820CA530),'beginShadowDepth'),
                          ((0x8214A8A4,0),'beginZPrepass'),
                          ((0x8205BE70,0x8205C100),'beginVfxRigid')):
        start=source.index('void EngineEffects::'+function+'(')
        end=source.find('\nvoid EngineEffects::',start+1)
        body=source[start:end if end>=0 else len(source)]
        if function!='beginVfxRigid':
            need(all(f'0x{address:08X}'in body for address in pair if address),'Dedicated pass guard changed')
        else:
            need('isVfxRigidSource(owner.source)'in body,'VFX source guard changed')
            profile=(ROOT/'runtime/rigid_profile.h').read_text()
            need('if(isVfxRigidSource(source))return {0x8205BE70,0x8205C100,'in profile,'VFX shader selector changed')
        result[pair]=dict(file='runtime/engine_effects.cpp',function=function,line=source[:start].count('\n')+1,
                          qualification='dedicated source/pass guard; source declaration alone grants no native lifecycle evidence')
    start=source.index('void EngineEffects::beginMono(')
    end=source.index('\nvoid EngineEffects::beginZPrepass(',start)
    for pair in mono_declared_pairs(source[start:end]):
        result[pair]=dict(file='runtime/engine_effects.cpp',function='beginMono',line=source[:start].count('\n')+1,
            qualification='source-declared original selected mono pair; native lifecycle evidence required',
            native_lifecycle_tested=False)
    return result


def observed_pairs(log_text):
    result={}
    for line,text in enumerate(log_text.splitlines(),1):
        if not text.startswith('[NATIVE '):continue
        match=re.search(r'\bvs=([0-9A-F]{8})\s+ps=([0-9A-F]{8})',text)
        if not match:match=re.search(r'\bVS([0-9A-F]{8})(?:/| )PS([0-9A-F]{8})',text)
        if match:
            pair=tuple(int(match[i],16)for i in (1,2));result.setdefault(pair,dict(line=line,text=text))
    return result


def load_original():
    image=(ROOT/'analysis/simpsons.pe').read_bytes()
    need(len(image)==15466496 and sha(image)==IMAGE_SHA,'Original image identity changed')
    catalogs=[]
    for path,digest in CATALOGS:
        raw=(ROOT/path).read_bytes();need(sha(raw)==digest,'Checked original catalog identity changed: '+path)
        catalogs.append(json.loads(raw))
    return image,catalogs


def registry_inventory(catalogs,image):
    result={int(s['va'],16):dict(stage=s['stage'],bytes=s['bytes'],sha256=s['sha256'],name=r['name'])
            for d in catalogs for r in d['rows']for s in r['shaders']}
    source=(ROOT/'renderer/material_resources.cpp').read_text()
    for m in re.finditer(r'\{(0x[0-9A-Fa-f]{8}),MaterialStage::(Vertex|Pixel),([0-9A-Fa-fx]+),"([^"]*)",\s*"([0-9a-f]{64})"\}',source):
        address=int(m[1],16);need(address not in result,'Duplicate immutable shader identity')
        result[address]=dict(stage=m[2].lower(),bytes=int(m[3],0),sha256=m[5],name=m[4])
    need(len(result)==256,'Original material registry must retain all 256 identities')
    for address,r in result.items():need(sha(image[address-BASE:address-BASE+r['bytes']])==r['sha256'],'Original material record changed')
    return result


def build_report(catalogs,image,artifacts,selections,observed):
    rows=[];shader_count=0;instruction_candidates=[]
    for table,catalog in enumerate(catalogs):
        for r in catalog['rows']:
            need(sha(image[int(r['blob_va'],16)-BASE:int(r['blob_va'],16)-BASE+r['bytes']])==r['sha256'],'Original effect envelope changed')
            shaders={s['sha256']:s for s in r['shaders']};shader_count+=len(shaders)
            for t in r['techniques']:
                for p in t['passes']:
                    pair=[];missing=[]
                    for stage in ('vertex','pixel'):
                        digest=p['shaders'][stage]['shader_sha256']
                        if digest is None:pair.append(0);continue
                        need(digest in shaders and shaders[digest]['stage']==stage,'Pass association does not name a shader of the correct stage')
                        s=shaders[digest];address=int(s['va'],16);pair.append(address)
                        if address in artifacts:need(artifacts[address]['stage']==stage,'Native artifact stage differs from original pass')
                        else:missing.append(f'0x{address:08X}')
                    pair=tuple(pair)
                    row=dict(table=table,row=r['index'],source=r['blob_va'],effect=r['name'],technique=t['name'],
                             technique_handle=t['handle'],pass_handle=p['handle'],context_offset=p['context_offset'],
                             vertex=f'0x{pair[0]:08X}',pixel=f'0x{pair[1]:08X}',missing_native_artifacts=missing,
                             artifact_status='missing'if missing else 'admitted',
                             admission_status=('observed_with_artifacts'if pair in observed else
                                               'declared_with_artifacts'if pair in selections else 'artifact_only')if not missing else 'missing_artifacts',
                             runtime_selection=selections.get(pair),observed=observed.get(pair),
                             qualification='Log selection proves reachability only; it does not prove complete draw/lifecycle correctness.')
                    rows.append(row)
    need(len(rows)==110 and shader_count==215,'Original registered pass/shader population changed')
    # The last 12 bytes are a separately pinned 4E4A original trailer, not
    # issued instructions. Equal bodies are only review candidates: original
    # constant mapping, input semantics and ownership still need separate proof.
    all_shaders=[(r,s)for d in catalogs for r in d['rows']for s in r['shaders']]
    def signature(s):
        base=int(s['va'],16)-BASE;raw=image[base:base+s['bytes']]
        need(raw[-12:-10]==bytes.fromhex('4e4a'),
             'Unqualified shader trailer framing')
        start=s['header_bytes']+s['prefix_bytes']
        need(sha(raw)==s['sha256'] and sha(raw[start:])==s['code_sha256'],'Catalog shader identity changed')
        if s['prefix_bytes']:
            need(sha(raw[s['header_bytes']:start])==s['prefix_sha256'],'Catalog literal prefix changed')
        return s['stage'],s['prefix_sha256'],sha(raw[start:-12])
    known={signature(s):[]for _,s in all_shaders if int(s['va'],16)in artifacts}
    for _,s in all_shaders:
        if int(s['va'],16)in artifacts:known[signature(s)].append(s['va'])
    for r,s in all_shaders:
        if int(s['va'],16)not in artifacts and signature(s)in known:
            instruction_candidates.append(dict(effect=r['name'],shader=s['va'],stage=s['stage'],body_sha256=signature(s)[2],
                literal_prefix_sha256=s['prefix_sha256'],candidate_artifacts=known[signature(s)],
                qualification='Instruction/literal equivalence only; no runtime admission or alias is authorized.'))
    registry=registry_inventory(catalogs,image)
    need(set(artifacts)<=set(registry),'Native compiler admits an unknown original material')
    population=(ROOT/'tests/header/native_translated_materials.h').read_text()
    addresses=re.findall(r'0x[0-9A-Fa-f]{8}',population)
    expected={int(v,16)for v in addresses};need(len(addresses)==len(expected),'Duplicate independent artifact pin')
    need(set(artifacts)==expected,'Native compiler admission differs from independent pinned test population')
    states=Counter(r['artifact_status']for r in rows)
    return dict(schema=1,scope='49 registered effects, all 110 technique/pass combinations and 256 immutable material identities',
                image_sha256=IMAGE_SHA,summary=dict(effects=49,passes=len(rows),registered_shaders=shader_count,
                immutable_materials=len(registry),native_artifacts=len(artifacts),registered_native_artifacts=sum(int(s['va'],16)in artifacts for _,s in all_shaders),
                admitted_passes=states['admitted'],missing_artifact_passes=states['missing'],
                observed_passes=sum(bool(r['observed'])for r in rows),declared_selection_passes=sum(bool(r['runtime_selection'])for r in rows)),
                passes=rows,instruction_equivalence_review_candidates=instruction_candidates,
                unsupported_materials=[dict(address=f'0x{a:08X}',**r)for a,r in registry.items()if a not in artifacts])


def asset_scope():
    path=ROOT/'analysis/assets.json';raw=path.read_bytes();data=json.loads(raw);resources=[];streams=[]
    for f in data['files']:
        if f['extension']=='.str':streams.append(f['path'])
        for entry in f.get('inspection',{}).get('entries',[]):
            for chunk in entry.get('chunks',[]):
                if chunk.get('type_name')=='VFX':resources.append(dict(archive=f['path'],entry=entry['index'],name=chunk['name'],
                    payload_sha256=chunk['payload_sha256'],payload_bytes=chunk['payload_size']))
    return dict(manifest_sha256=sha(raw),archives=len(streams),vfx_occurrences=len(resources),
                vfx_unique_payloads=len({r['payload_sha256']for r in resources}),vfx_resources=resources,
                qualification='Hash-pinned existing asset metadata inventory. VFX emitter fields and material references remain opaque; counts do not establish emitter admission or runtime reachability.')


def direct_particle_scope(image):
    # This direct SDK path has its own exact registry and is separate from the
    # registered FX catalog. Keep its declared matrix separate from log reachability.
    import analyze_dual_projected_particle_shader as family
    report=family.inspect(image)
    backend=(ROOT/'renderer/particle_draw.cpp').read_text()
    need('CreatePixelShader(kPSParticleDualProjectedDraw,' in backend and
         'dual?p.dualProjectedPixel.Get():p.projectedPixel.Get()' in backend and
         'type5?p.type5Vertex.Get():p.vertex.Get()' in backend and
         'Type-5 particle shader combination is unqualified' not in backend and
         'Projected dual particles are unqualified' not in backend,
         'Direct particle backend does not implement the complete original matrix')
    rows=[]
    for vertex in report['vertex_addresses']:
        for index,pixel in enumerate(report['pixel_addresses']):
            rows.append(dict(vertex=f'0x{vertex:08X}',pixel=f'0x{pixel:08X}',
                             vertex_policy='type5' if vertex==0x821578E0 else 'ordinary',
                             dual=bool(index&1),projected=bool(index&2),
                             observed=None,admission_status='declared_original_selection'))
    return dict(source='runtime/engine_particles.cpp and renderer/particle_draw.cpp',
                matrix=rows,qualified=['ordinary','dual','projected','dual projected',
                                      'type5 ordinary','type5 dual','type5 projected','type5 dual projected',
                                      'requested projection without a live original projector: ordinary/dual PS fallback'],
                guarded=['dual mode without a second original texture wrapper',
                         'nonzero original r29 special path'],
                null_projector_fallback=dict(original_branch='0x82772E00 -> 0x82772F94 with r10=0',
                    pixel_records=['0x82156770','0x82156948'],consumed_exports=['SV_POSITION','TEX0','TEX2'],
                    proof='tests/test_particle_null_projector.py',
                    proof_sha256=sha((ROOT/'tests/test_particle_null_projector.py').read_bytes()),
                    qualification='Original instruction execution proves inherited c21..25 affect only unconsumed TEX1; neither fallback PS samples depth. No shadow owner is invented.'),
                logical_to_native_texture_stages=report['texture_slots'],
                original_cpu_setup_sha256=report['cpu_setup_sha256'],
                qualification='All2VSx4PS original registry selections have exact programs and owned resource contracts. Declared support is not gameplay reachability evidence. Registered particles FX shaders remain a separate unported path; asset emitter fields remain opaque.')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--json',type=Path);parser.add_argument('--log',type=Path)
    args=parser.parse_args();image,catalogs=load_original()
    artifacts=artifact_inventory((ROOT/'renderer/screen_pipeline.cpp').read_text())
    observed=observed_pairs(args.log.read_text(encoding='utf-8',errors='replace'))if args.log else {}
    report=build_report(catalogs,image,artifacts,declared_selection(),observed)
    report['assets']=asset_scope()
    report['inputs']=[dict(path=path,sha256=sha((ROOT/path).read_bytes()))for path in
        ('renderer/screen_pipeline.cpp','renderer/material_resources.cpp','runtime/rigid_profile.h','runtime/skin_profile.h',
         'runtime/engine_effects.cpp','runtime/engine_particles.cpp','renderer/particle_draw.cpp','renderer/particle_draw.hlsl',
         'tools/analyze_dual_projected_particle_shader.py','tests/test_particle_null_projector.py',
         'tests/header/native_translated_materials.h')]
    report['catalogs']=[dict(path=path,sha256=digest)for path,digest in CATALOGS]
    report['direct_particles']=direct_particle_scope(image)
    if args.log:report['log']=dict(path=str(args.log),sha256=sha(args.log.read_bytes()))
    if args.json:args.json.parent.mkdir(parents=True,exist_ok=True);args.json.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report['summary'],sort_keys=True));print('VFX asset inventory:',report['assets']['archives'],'archives /',report['assets']['vfx_occurrences'],'resources /',report['assets']['vfx_unique_payloads'],'unique payloads; opaque emitter semantics kept explicit')

if __name__=='__main__':main()
