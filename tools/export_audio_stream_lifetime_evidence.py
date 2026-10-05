#!/usr/bin/env python3
"""Join exact configured MUS CPU/worker lifetimes without draw/gameplay credit."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_mission_asset_support as audit
from export_geometry_draw_evidence import ctest_commands

SPECS={'OriginalConfiguredMusCancel':0,'OriginalConfiguredMusComplete':1}
PASS=re.compile(r'^PASS configured original MUS case=(\d+): (\d+) checks; ordinal=(\d+) '
    r'header=([0-9A-F]+) audio=([0-9A-F]+) selector=(\d+) blocks=(\d+) decoded=(\d+) '
    r'normal-releases=(\d+) cancel-releases=(\d+); original owned file/read/request/mixer/stop/'
    r'codec free/metadata free/deferred reader retirement/service close/worker join$',re.MULTILINE)


def selected_row(matrix,stream):
    lookup=stream['catalog_lookup']
    audit.require(lookup['kind']=='stream','Unsupported authored audio kind')
    matched=[r for r in matrix['rows'] if r['kind']=='sound_stream' and
        (r['source'],r['ordinal'],r['header_offset'])==
        (lookup['source'],lookup['ordinal'],lookup['header_offset'])]
    audit.require(len(matched)==1,'Selected audio catalog identity is not unique')
    row=matched[0]
    audit.require(row['id']==audit.identity('sound_stream',row['source'],row['ordinal'],row['header_offset']),
        'Selected audio catalog id differs')
    audit.require(row['header_hex']==stream['original_header_hex'] and
        row['audio_offset']==stream['original_audio_offset'] and
        row['audio_bytes']==stream['original_audio_bytes'], 'Selected audio catalog span differs')
    return row


def validate_binary(row,stream,data):
    lookup=stream['catalog_lookup'];at=stream['original_record_offset']
    audit.require(audit.sha(data)==lookup['source_sha256'],'Selected audio source hash differs')
    audit.require(at==40+28*row['ordinal'] and 0<=at<=len(data)-28,'Selected original MUS record differs')
    ordinal,zero=struct.unpack_from('>HH',data,at+4)
    header,audio,size=struct.unpack_from('>I',data,at+8)[0]*16,struct.unpack_from('>I',data,at+12)[0]*128,struct.unpack_from('>I',data,at+20)[0]
    audit.require((ordinal,zero,header,audio,size)==(row['ordinal'],0,row['header_offset'],row['audio_offset'],row['audio_bytes']) and
        struct.unpack_from('>I',data,at+16)[0]==8 and struct.unpack_from('>I',data,at+24)[0]==0,
        'Selected original MUS record/header/audio association differs')
    audit.require(0<=header<=len(data)-8 and data[header:header+8].hex()==row['header_hex'] and
        0<=audio<audio+size<=len(data),'Selected original MUS header/payload bounds differ')


def passing_output(stream,text,row):
    name=stream['test'];audit.require(name in SPECS and stream['create_use_release'] is True,
        'Incomplete or unsupported selected audio lifetime')
    audit.require(row['ordinal']==SPECS[name], 'Selected fixture ordinal differs from original case')
    matches=list(PASS.finditer(text.replace('\r\n','\n')))
    audit.require(len(matches)==1,'Missing or ambiguous original audio lifecycle marker')
    m=matches[0]
    values=[int(m[i],16 if i in (4,5) else 10) for i in range(1,11)]
    case,checks,ordinal,header,audio,selector,blocks,decoded,normal,cancel=values
    audit.require((case,ordinal,header,audio,selector,blocks,decoded,normal,cancel)==
        (SPECS[name],row['ordinal'],row['header_offset'],row['audio_offset'],stream['selector'],
         stream['observed_blocks'],stream['decoded_samples'],stream['normal_releases'],stream['cancellation_releases']) and
        checks>0 and stream['pass_output']==m[0],'Audio marker differs from selected original input')
    audit.require(blocks>0 and decoded>0 and decoded<=row['samples'] and normal+cancel==blocks,
        'Audio claims or decoded extent differs')
    if case==0:
        audit.require(selector==0 and cancel==blocks and normal==0 and decoded<row['samples'],
            'Selected cancel did not retain actual live claims')
    else:
        audit.require(normal==blocks==row['blocks'] and cancel==0 and decoded==row['samples'],
            'Selected completion did not reach the complete authored sample extent')
    return m[0]


def reference(path,root):
    path=Path(path).resolve()
    audit.require(path.is_relative_to(root),'Audio evidence input escapes workspace')
    return dict(path=path.relative_to(root).as_posix(),sha256=audit.sha(path.read_bytes()))


def export(matrix,receipt_path,config_path,root=ROOT):
    root=root.resolve();receipt,proof=audit.load(receipt_path)
    audit.require(receipt['schema']==1 and receipt['format']=='configured_original_mus_selected_stream_lifetimes_v1',
        'Unsupported authored audio lifetime receipt')
    inputs=[]
    for entry in receipt['files']:
        ref=reference(entry['path'],root)
        audit.require(ref['sha256']==entry['sha256'],'Audio source/executable/library identity changed')
        inputs.append(ref)
    xml_path=Path(receipt['native_test_xml']['path']).resolve();xml_ref=reference(xml_path,root)
    audit.require(xml_ref['sha256']==receipt['native_test_xml']['sha256'],'Audio JUnit identity changed')
    inputs.append(xml_ref);inputs.append(reference(config_path,root))
    cases_by_name={}
    for node in ET.fromstring(xml_path.read_bytes()).iter('testcase'):
        if node.get('name') in SPECS:
            audit.require(node.get('name') not in cases_by_name,'Duplicate selected audio testcase')
            cases_by_name[node.get('name')]=node
    commands=ctest_commands(config_path.read_text(encoding='utf-8'))
    cases=[];seen=set()
    for stream in receipt['streams']:
        name=stream['test'];audit.require(name in SPECS and name not in seen,'Duplicate/unsupported selected audio receipt')
        seen.add(name);row=selected_row(matrix,stream);node=cases_by_name.get(name)
        audit.require(node is not None and not any(node.find(t) is not None for t in ('failure','error','skipped')),
            'Selected audio native testcase is not passing')
        marker=passing_output(stream,'\n'.join(node.itertext()),row)
        command=commands.get(name,[])
        audit.require(len(command)==3 and Path(command[0]).stem=='ConfiguredAudioStreamTests' and
            command[2]==str(SPECS[name]),'Selected audio CTest command differs')
        audit.require(reference(command[0],root) in inputs and reference(command[1],root) in inputs,
            'Selected audio CTest executable/image was not bound')
        original=(root/'Simpsons Game, The (USA)').resolve()
        source=(original/row['source']).resolve()
        audit.require(source.is_relative_to(original),'Selected audio source escapes original root')
        validate_binary(row,stream,source.read_bytes())
        source_ref=reference(source,root)
        cases.append(dict(result='passed',scope='lifecycle',catalog_id=row['id'],test=name,
            use_kind='pcm_mixer_service',
            phases=['create','use','release'],proof=proof,inputs=inputs+[source_ref],
            catalog_identity=stream['catalog_lookup'],original_record_offset=stream['original_record_offset'],
            native_pass_marker=marker,decoded_samples=stream['decoded_samples'],termination=stream['termination'],
            original_path=receipt['original_path'],limits=stream['credit_limit'],
            malformed_scope='Separate generic synthetic regressions; none granted to this exact stream',
            gameplay_credit=False,full_payload_decode=SPECS[name]==1))
    audit.require(seen==set(SPECS),'Missing independent selected audio lifetime')
    return dict(schema=1,format='exact_original_audio_stream_lifecycle_receipts',
        cases=cases,synthetic_rows=[],complete=True,coverage_gaps=[],
        authority=dict(original_receipt=proof,ctest=reference(config_path,root)),
        limits=['Exactly two authored streams; full bank and generic synthetic audio cases remain separate.',
            'PCM use is lifecycle scope, not draw scope. No runtime/gameplay or malformed exact-asset credit.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--ctest-config',type=Path,default=ROOT/'build/native/CTestTestfile.cmake')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();matrix,_=audit.load(args.matrix.resolve())
    report=export(matrix,args.receipt.resolve(),args.ctest_config.resolve())
    output=args.output.resolve()
    audit.require(output.is_relative_to(ROOT/'build') and output not in
        (args.matrix.resolve(),args.receipt.resolve(),args.ctest_config.resolve()),'Invalid audio evidence output')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(receipts=len(report['cases']),complete=report['complete'],draw_credit=False,gameplay_credit=False)))


if __name__=='__main__':main()
