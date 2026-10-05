#!/usr/bin/env python3
"""Pin original texture producer limits without granting native coverage.

Original SDK requirements, descriptor construction/reading and copied ITXD
loading are separate contracts. Device caps and field widths do not prove
every format/descriptor/serialized allocation is a valid ITXD resource.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_mission_asset_support as audit
import audit_effect_admission as effects

SPANS=(
    (0x8206AA30,0x130,'9bd4c11e90ada6dffc355bafe7c3c51bcd36c7b3b6cae2777679b4678c416e40','original_device_caps'),
    (0x8244E5E0,0x14,'974f193b30cb926d3e3ab417acb8e3eace159b8b2f3dd8dd08930f85c3cf0ab6','device_caps_copy'),
    (0x82B7F950,0x428,'3f64898f62b226c3f82d1a783a6d951a6464c7dff57e4097ff0a95d34b4164c1','sdk_texture_requirements'),
    (0x8243F928,0x310,'ee92de8b0f5b473de1d07921059b7047f8698015064745d7c13a5f3ef5dd59bc','sdk_descriptor_constructor'),
    (0x8243DED8,0x98,'ea9c909d5a0ce4d01670ba70a31f061f7ab480d3bf6a8cda28a4777d6779dc0b','sdk_descriptor_dimensions'),
    (0x8243FE90,0x10,'3899c148bc49cf42a54c2420d56efd76676e9d2904b856b40714a0a609321547','sdk_descriptor_level_count'),
    (0x82C20E80,0x68,'4070ece9d31e1a849f6f5dab949ebb0d7b674601d1edec524779f3264e3944aa','sdk_descriptor_address_relocation'),
    (0x82736F58,0x134,'477494445d3dbdd4a04f9acf997a86aeeb30962f25261925ab9feec49b0e4861','copied_itxd_allocation_and_copy'),
    (0x826F24D8,0x190,'d0e287aa5d84efd705eb15b5d0d563e04d90b5b3877a40e00a380d9794261fa3','copied_itxd_walker'),
    (0x826F26B0,0x60,'46c55e30714698b4d269c3bf49dd4690991334306093e8f7248b555d3d472864','normal_itxd_load_producer'),
)
INSTRUCTIONS=(
    (0x8244E5E8,0x388BAA30,'caps source8206AA30'),
    (0x8244E5EC,0x38A00130,'copy the entire0x130-byte caps record'),
    (0x82B7FAC4,0x4B8CEB1D,'call original caps copier'),
    (0x82B7FAD4,0x2B1B0400,'type17 volume depth limit1024'),
    (0x82B7FAE0,0x2B1F0800,'type17 volume width limit2048'),
    (0x82B7FAEC,0x2B1D0800,'type17 volume height limit2048'),
    (0x82B7FAFC,0x816100A8,'other types read caps+58 maxwidth'),
    (0x82B7FB0C,0x816100AC,'other types read caps+5C maxheight'),
    (0x82B7FB88,0x39600002,'type3 2D power-of-two requirement mask2'),
    (0x82B7FBC0,0x7D6B4839,'power-of-two normalization depends on caps/type/format'),
    (0x82B7FC28,0x397F0003,'supported BC format widths round up to blocks'),
    (0x82B7FC30,0x557F003A,'supported BC format width aligned4'),
    (0x8243FB10,0x550869A4,'2D height-minus-border packed into thirteen bits'),
    (0x8243FB30,0x552904FE,'2D width-minus-border retained in thirteen bits'),
    (0x8243DF48,0x554A04FE,'2D reader width thirteen bits'),
    (0x8243DF5C,0x554A9CFE,'2D reader height thirteen bits'),
    (0x8243FE94,0x556BD73E,'level-count reader extracts four bits'),
    (0x8243FE98,0x386B0001,'level count adds one'),
    (0x826F26DC,0x38C00000,'normal ITXD loader selects actual copy mode'),
    (0x82736FA4,0x838A0010,'copied pixel length comes from serialized extension+10'),
    (0x8273703C,0x4E800421,'actual pixel allocation through original allocator+20'),
    (0x8273704C,0x48305D35,'copy actual N-byte original payload'),
    (0x82C20EB0,0x4B81EFE1,'relocation queries actual descriptor level count'),
)


def pin_original(image):
    audit.require(len(image)==15466496 and audit.sha(image)==effects.IMAGE_SHA,'Original image identity changed')
    take=lambda address,n:image[address-effects.BASE:address-effects.BASE+n]
    for address,n,digest,name in SPANS:
        audit.require(audit.sha(take(address,n))==digest,'Original texture producer span changed: '+name)
    for address,word,meaning in INSTRUCTIONS:
        audit.require(struct.unpack('>I',take(address,4))[0]==word,'Original texture instruction changed: '+meaning)
    caps=struct.unpack('>76I',take(0x8206AA30,0x130))
    audit.require((caps[0x58//4],caps[0x5C//4],caps[0x3C//4])==(8192,8192,0x1EC45),'Original caps fields changed')
    return dict(spans=[dict(address=f'{a:08X}',bytes=n,sha256=h,meaning=name)for a,n,h,name in SPANS],
        instructions=[dict(address=f'{a:08X}',word=f'{w:08X}',meaning=m)for a,w,m in INSTRUCTIONS],
        caps=dict(source='8206AA30',width=8192,height=8192,texture_caps='0001EC45',
                  ordinary_2d_power_of_two_required=bool(caps[0x3C//4]&2)))


def inspect(root=ROOT,texture_report=None):
    producers=pin_original((root/'analysis/simpsons.pe').read_bytes())
    paths=('runtime/engine_itxd_textures.cpp','renderer/itxd_blocks.cpp','runtime/engine_builtin_textures.cpp')
    sources={p:(root/p).read_text()for p in paths}
    audit.require('width>2048 || height>2048' in sources[paths[1]] and
                  'width>1024||height>1024' in sources[paths[1]],'Reinspect changed ITXD dimension guards')
    def ref(path,needle):
        text=sources[path];audit.require(needle in text,'Texture guard anchor changed: '+needle)
        return dict(path=path,line=text[:text.index(needle)].count('\n')+1,sha256=audit.sha((root/path).read_bytes()))
    queue=[
        dict(id='itxd_l8_dimensions',status='qualified_profile_below_original_sdk_cap',
            guard=ref(paths[1],'if(width<64 || height<64 || width>2048 || height>2048'),
            native_profile='power-of-two64..2048, tiled L8 exact descriptor and owned mip extents',
            original_scope='ordinary type3 2D SDK requirements normalize compatible formats to1..8192; caps do not require power-of-two dimensions',
            copied_itxd_validity='Original offline ITXD serializer dimension/format domain remains unestablished',
            next_case='Obtain an actual original producer L8 dictionary below64, above2048 or non-power-of-two; run independent original copy/bind/sample/unbind/final allocator release plus descriptor/span negatives'),
        dict(id='itxd_bc_dimensions_pitch',status='qualified_profile_below_original_sdk_cap',
            guard=ref(paths[1],'if(width<4 || height<4 || width>2048 || height>2048'),
            native_profile='power-of-two4..2048; tiled pitch multiple128 and at most2048; BC1/2/3; current exact mip allocation profile',
            original_scope='SDK requirements round accepted BC dimensions to a multiple4; ordinary2D caps8192 and constructor/readers retain13-bit dimensions',
            copied_itxd_validity='Logical dimension cap8192 does not prove pitch/allocation/tail combinations of offline ITXD serialization',
            next_case='Trace offline serializer or obtain its genuine descriptors and pixel allocation; independently test high and padded pitch/partial-edge/mip cases through original owner lifetime'),
        dict(id='itxd_rgba_dimensions_tail',status='qualified_profile_below_original_sdk_cap',
            guard=ref(paths[1],'if((!smallBase&&(width<32||height<32))||width>1024||height>1024'),
            native_profile='power-of-two32..1024 plus16x16 base; exact pitch; separate mips only through first packed16-texel level',
            original_scope='SDK constructor carries width/height rather than an authored1024 or16/32 minimum; device2D cap8192 is separate from valid serialization',
            copied_itxd_validity='Deeper packed tails, padded pitch and descriptor controls remain unqualified producer/decoder scope',
            next_case='Use real producer output for other RGBA extents/tails; verify actual selected level pixels and original create/use/release before broadening'),
        dict(id='itxd_named_palette',status='asset_name_narrows_equal_format_decoder',
            guard=ref(paths[0],'if(format==0x18280186 && !word(r.relocated,0xB0) &&'),
            native_profile='simpsons_palette/dual_simpsons_palette force exact64x64 base decoder before general equal-format decoder',
            original_scope='Current names are shipped profiles; no original texture-name rule forces these dimensions',
            copied_itxd_validity='No original authored counterexample demonstrated; name is not sufficient format validity authority',
            next_case='Trace palette consumer dependency and serializer naming contract before removing dimension association; retain real palette/malformed lifecycle cases'),
        dict(id='itxd_format_auxiliary',status='producer_domain_unestablished',
            guard=ref(paths[0],'if(word(r.relocated,0xB0)||(format!='),
            native_profile='five copied GPU format words and zero auxiliary header',
            original_scope='Copy walker and address relocation preserve all serialized format/control bits; SDK low6-bit format field is an encoding, not proof that all64 values are supported',
            copied_itxd_validity='Offline serialized format/control/auxiliary producer domain remains unestablished; other original built-in texture formats use a separate loader',
            next_case='Trace native raster format tables and offline serializer; qualify actual additional copied format with consumed semantics, exact owner release and malformed cases'),
    ]
    report=dict(schema=1,scope='Static pinned original texture producer and serialized-loader triage; no native execution',
        authority=dict(image=dict(path='analysis/simpsons.pe',sha256=effects.IMAGE_SHA),original_producers=producers,
                       sources=[dict(path=p,sha256=audit.sha((root/p).read_bytes()))for p in paths]),
        established_scopes=[dict(id='sdk_ordinary_2d',compatible_format_required=True,normalized_dimensions=[1,8192],
            power_of_two_required=False,source='82B7F950 ->8244E5E0 ->8206AA30',
            qualification='Dimension normalization after successful original format qualification; allocation/GPU upload and ITXD serialized validity not established'),
            dict(id='sdk_volume',resource_type=17,width_height_max=2048,depth_max=1024,source='82B7FAD4..82B7FAF4'),
            dict(id='sdk_2d_descriptor_encoding',width_height_bits=13,nonborder_representable=[1,8192],level_count_bits=4,
                 representable_level_counts=[1,16],source='8243F928 /8243DED8 /8243FE90',
                 qualification='Representable fields, not proof that arbitrary dimensions, levels, formats or allocations are valid'),
            dict(id='normal_copied_itxd',copy_mode=0,pixel_bytes='serialized extension+10, exact original allocator request/copy',
                 source='826F26B0 ->826F24D8 ->82736F58 ->82C20E80',
                 qualification='No format or dimension whitelist in this copy/relocation scope; serializer and eventual GPU consumers impose separate validity')],
        actionable_queue=queue,native_lifecycle_tested=False,
        limits=['Device caps do not certify arbitrary copied ITXD descriptors or pixel extents.',
                'Field widths establish representability only; original malformed input may be copied without validation.',
                'No original offline ITXD serializer was located in this pinned executable; its full output range remains unresolved.',
                'Stock metadata census and source inspection grant no native sample/draw or complete original owner lifetime credit.'])
    if texture_report:
        data,reference=audit.load(texture_report)
        rows=data['textures'];report['authority']['texture_report']=reference
        report['shipped_metadata_scope']=dict(dictionaries=len(data['dictionaries']),occurrences=len(rows),
            admitted=sum(r['admission']=='admitted_metadata'for r in rows),formats=dict(sorted(Counter(r['format']for r in rows).items())),
            min_width=min(r['width']for r in rows),max_width=max(r['width']for r in rows),
            min_height=min(r['height']for r in rows),max_height=max(r['height']for r in rows),
            qualification='Observed stock envelope only; not an original producer bound or catalog-wide native lifecycle')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--texture-report',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'build/restrictive-check-audit/texture-producer-ranges.json')
    args=parser.parse_args();report=inspect(texture_report=args.texture_report)
    output=args.output.resolve();audit.require(output.is_relative_to(ROOT/'build') and
        (not args.texture_report or output!=args.texture_report.resolve()),'Invalid texture producer output')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(findings=len(report['actionable_queue']),sdk_caps=report['authority']['original_producers']['caps'],
                         shipped_scope=report.get('shipped_metadata_scope')),sort_keys=True))


if __name__=='__main__':main()
