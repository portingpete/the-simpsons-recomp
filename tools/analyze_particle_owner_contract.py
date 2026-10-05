#!/usr/bin/env python3
"""Pin the original .prt size/allocator/activation/removal chain; source only."""
import argparse
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import audit_packaged_particle_fields as particles

EXTRA_SPANS = {
    '82774368': (0x827743BC, '0f6e53437b40eef2d06f08fa208e723defa2584cc8451c11d6dc14317058f8db'),
    '827743F0': (0x82774560, '22511d9bb2d2bc6cc9d790930e1ade8943a93c4d582d51bca0290e4d5235049c'),
    '8274D530': (0x8274D584, 'da808c94e921523d7d9c54ce80be824d73f32f9186f2c20f2ecf6a21b7d1fd8c'),
    '8274D588': (0x8274D5AC, 'fea3cac81ccfa2913dc986b91423cc785d1b69384764a56d1ef15551f45b1d8d'),
    '8275A978': (0x8275A9F4, '0379f782d40d6e80f3a8a4956159fd5b8ef08da4fb9e41287e773862c43bb2fc'),
    '827749F0': (0x82774A84, '04f4bcc9da45caaf5b21c06ad78b405671952575fe2904d41ec8adfe4116503e'),
    '82774AD0': (0x82774C94, '4e10fd855ea287bbcdf433b88c49667219412a7e9ca3373d2d59d4cea8a0d2d7'),
    '82753018': (0x82753074, '79b57c282c030a85066233a0c2f8ab0079b6309bd492c4be733744677b1b4096'),
}


def verify(image, full_hash=True):
    particles.verify(image, full_hash)
    for start, (end, digest) in EXTRA_SPANS.items():
        particles.fields.require(particles.fields.sha(particles.fields.span(
            image, int(start,16)-particles.fields.BASE, end-int(start,16))) == digest,
            'Original particle owner span differs: '+start)


def requested_owner(authored_count, available_blocks):
    """Source arithmetic only, not an allocator/native live-input validator."""
    particles.fields.require(-32768 <= authored_count <= 32767 and
        isinstance(authored_count,int) and isinstance(available_blocks,int) and
        available_blocks >= 0, 'Unqualified encoded count or available-block model input')
    normalized=max(authored_count,1)
    blocks=(normalized+7)//8
    pointer_owner=4*(blocks+76)
    embedded=blocks>available_blocks
    raw_size=((pointer_owner+15)&~15)+544*blocks if embedded else pointer_owner
    owner_bytes=(raw_size+15)&~15
    return dict(authored_count=authored_count,normalized_count=normalized,requested_blocks=blocks,
        requested_rounded_capacity=blocks*8,available_blocks=available_blocks,embedded=embedded,
        callback_size=raw_size,allocator_alignment=16,allocator_size=owner_bytes,
        fits_generic_allocation_size_branch=0<owner_bytes<0x40000000,
        signed_live_capacity_representable=blocks*8<=32767,
        native_create_use_release_tested=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    parser.add_argument('--particle-report',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    image=args.image.read_bytes();verify(image)
    input_bytes=args.particle_report.read_bytes();inventory=json.loads(input_bytes)
    particles.fields.require(inventory['scope']=='source_pinned_offline_prt_requested_fields' and
        inventory['image_sha256']==particles.fields.IMAGE_SHA and
        inventory['tool_sha256']==particles.fields.sha(Path(particles.__file__).read_bytes()) and
        inventory['reader_sha256']==particles.fields.sha(Path(particles.fields.__file__).read_bytes()),
        'Particle inventory source identity differs')
    summary=inventory['summary']
    cases=[requested_owner(c,a) for c in (-32768,-1,0,1,8,9,4096,4097,5001,32767)
           for a in (0,4096)]
    # Exhaust the encoded arithmetic, without treating the model as a native case.
    for count in range(-32768,32768):
        for available in (0,4096):
            row=requested_owner(count,available)
            particles.fields.require(row['allocator_size']%16==0 and
                row['fits_generic_allocation_size_branch'], 'Owner arithmetic model differs')
    spans=dict(particles.SPANS,**EXTRA_SPANS)
    report=dict(schema=1,scope='source_pinned_original_particle_owner_arithmetic',
        authority=dict(image_sha256=particles.fields.IMAGE_SHA,
            tool_sha256=particles.fields.sha(Path(__file__).read_bytes()),
            field_tool_sha256=inventory['tool_sha256'],reader_sha256=inventory['reader_sha256'],
            particle_inventory=dict(path=str(args.particle_report),sha256=particles.fields.sha(input_bytes))),
        original_spans=[dict(function=f,end=f'{end:08X}',sha256=digest) for f,(end,digest) in spans.items()],
        contracts=[
            dict(id='requested_owner_bytes',calls=['8275DFF0','82774368','827743F0','8274D530'],
                original_points=['8275E014','8275E024','8275E02C','8275E034','8275E03C',
                    '8275E04C','8275E05C','8275E060','8275E064','8275E06C','8275E098',
                    '82774380','82774388','82774390','827743A4','82774480','827744A0','827744DC'],
                formula='normalized=max(signed16(definition+DA),1); blocks=(normalized+7)/8; shared=4*(blocks+76); embedded=align16(shared)+544*blocks; generic rounds size to16 and passes alignment16 to allocator',
                qualification='Explicit original size producer; allocator success, enclosing resource/state and actual draw/lifetime remain separate'),
            dict(id='activation_context',calls=['82763218','82762F08','82774AD0','82762980'],
                original_points=['82763220','82763228','82762F20','82762F30','82762F54'],
                contract='Generic setup info retains parent, selected20-byte instance row and index; activation fetches the row+12 module block; base constructor owns parent/matrix context, then particle constructor publishes actual block/capacity counts',
                qualification='No fake direct callback caller or standalone guest-emitter publication qualifies this path'),
            dict(id='instance_quota',calls=['8275CE78','827743F0','827749F0'],
                original_points=['8275CEA4','8275CEB0','82774460','82774464','82774468',
                    '8277453C','82774548','82774A5C','82774A68'],
                contract='Module+24 literal2048 bounds active instance count+20; creation increments and removal decrements. It is distinct from definition requested ring capacity.'),
            dict(id='normal_module_owner_retirement',calls=['827749F0','8275A978','8275CED0',
                    '82760EB0','82760728','8275F090','8274D588'],
                original_points=['82774A18','82774A6C','8275A9CC','8275A9D4','8275A9DC'],
                contract='Normal non-retained removal calls module vt+16, texture/block retirement and original allocator free; retained-sign-bit owners are linked into module cache rather than immediately freed',
                qualification='Normal and retained/cache paths need distinct authentic original lifetime regression cases'),
            dict(id='shared_pool_final_retirement',calls=['8275E2E0','82753018','8270CB00'],
                contract='Original pool final destructor releases allocator-owned storage and pool owner',
                qualification='Full module/texture and pool shutdown fixture remains unexecuted')],
        stock_summary=summary,arithmetic_cases=cases,encoded_count_model_cases=131072,
        native_guard_candidates=[dict(path='runtime/engine_particles.cpp',line=39,
            sha256=particles.fields.sha((ROOT/'runtime/engine_particles.cpp').read_bytes()),expression='capacity<=4096',
            disposition='unqualified_original_create_use_release_path'),
            dict(path='renderer/particle_draw.cpp',line=97,
            sha256=particles.fields.sha((ROOT/'renderer/particle_draw.cpp').read_bytes()),expression='vertices.size()<=16384',
            disposition='unqualified_original_create_use_release_path')],
        limits=['No stock requested capacity exceeds4096; stock maximum does not prove original maximum.',
            'Encoded signed16 maximum rounds to32768, which the direct producer later reads as signed capacity; representability alone is not valid live input proof.',
            'Allocator arithmetic for4097/5001 is source-valid but synthetic model use grants no original activation, malformed rejection, rendering or retirement credit.',
            'Actual texture/parameter/matrix/state readiness, pool failure/embedded ownership, retained module cache and lifetime regression remain to be exercised.'])
    output=args.output.resolve()
    particles.fields.require(output.is_relative_to(ROOT/'build') and
        output not in (args.particle_report.resolve(),args.image.resolve()),'Invalid owner-contract output')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(pinned_spans=len(spans),encoded_arithmetic_cases=131072,
        stock_requested_above4096=summary['requested_over_native4096'],native_lifecycle_credit=False)))


if __name__=='__main__':main()
