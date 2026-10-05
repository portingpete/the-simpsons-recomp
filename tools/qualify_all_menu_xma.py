"""Qualify exact menu or Land of Chocolate MUS streams with independent PCM checks."""
import argparse
import array
import importlib.util
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import qualify_menu_xma as q

ROOT=q.ROOT
OUT=ROOT/'build/all-menu-xma'


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bank',choices=('menu','loc'),default='menu')
    args=parser.parse_args();loc=args.bank=='loc'
    if loc:
        q.ORIGINAL=ROOT/'Simpsons Game, The (USA)/audiostreams/loc_mus.mus'
        q.SOURCE_SHA='b350a5b1ce17ff5b53a2b0a6c035d9c36eafbf213252dad07b2c387f6c0743fa'
        OUT=ROOT/'build/loc-music-xma'
    expected_count=50 if loc else 37
    data=q.ORIGINAL.read_bytes()
    q.need(q.sha(data)==q.SOURCE_SHA,'Original menu asset changed')
    streams=q.inspect_mus(data)['streams']
    q.need(len(streams)==expected_count,'Music stream count changed')
    OUT.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('all_menu_toolchain',ROOT/'tests/test_host_fp.py')
    tc=importlib.util.module_from_spec(spec);spec.loader.exec_module(tc)
    compiler,env=tc.toolchain();env['PATH']=str(q.INSTALL/'bin')+os.pathsep+env['PATH']
    q.OUT=OUT
    q.run([sys.executable,'-B',ROOT/'tools/build_native_audio_codec.py','--verify'],env,'codec-verify')
    harness=q.CPP.replace('size<=262144','size<=16777216').replace('frames<=262144','frames<=16777216')
    source=OUT/'harness.cpp';source.write_text(harness,encoding='utf-8')
    q.run([compiler,'/nologo','/std:c++20','/EHsc','/MD','/O2','/fp:strict','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
        f'/I{ROOT}',f'/I{q.INSTALL/"include"}',source,ROOT/'audio/native_xma_codec.cpp','/Fo'+str(OUT)+'\\',
        '/Fe'+str(OUT/'harness.exe'),'/link',q.INSTALL/'lib/avcodec-simpsonsxma.lib',q.INSTALL/'lib/avutil-simpsonsxma.lib','/INCREMENTAL:NO'],env,'compile',90)
    reports=[]
    for info in streams:
        index=info['index'];header,audio=info['header'],info['audio']
        q.need(header['channels']==6 and header['sample_rate']==48000 and not header['loop'],'Unqualified menu format')
        blocks=q.split_blocks(data,audio['audio_offset'],audio['audio_offset']+audio['audio_size'],6)
        for block in blocks:
            raw=bytearray(data[block['offset']:block['offset']+block['bytes']]);raw[0]&=0x7f
            block['owned_sha256']=q.sha(raw)
            last=block['layers'][-1]
            block['terminal_padding']=block['offset']+block['bytes']-last['payload_offset']-last['payload_bytes']
        report=dict(stream=info,source_sha256=q.SOURCE_SHA,raw_eof_sent=False,initial_skip_frames=384,
            header=data[header['header_offset']:header['header_offset']+8].hex(),blocks=blocks,layers=[])
        if not loc and index in (0,1):
            old=ROOT/('build/menu-xma' if index==0 else 'build/menu-start-xma')
            prior=json.loads((old/'report.json').read_text(encoding="utf-8"))
            q.need(prior['source_sha256']==q.SOURCE_SHA and prior['stream']==info and prior['blocks']==blocks and
                   prior['raw_eof_sent'] is False and prior['initial_skip_frames']==384,'Prior qualification no longer matches original source')
            for layer in range(3):
                entry=dict(prior['layers'][layer]);runs=entry['runs']
                q.need([(r['mode'],r['split']) for r in runs]==[('xma1',0),('xma2',0),('xma2',1)],'Prior decoder schedules incomplete')
                for run in runs:
                    raw=(old/f'layer{layer}-{run["mode"]}-{run["split"]}.f32le').read_bytes()
                    q.need(q.sha(raw)==run['pcm_sha256'] and len(raw)==run['frames']*8,'Prior raw qualification file changed')
                entry['trimmed_sha256']=q.sha(raw[384*8:(384+header['samples'])*8])
                report['layers'].append(entry)
            report['reused_qualification']=str(old/'report.json')
        else:
            directory=OUT/f'stream{index}';directory.mkdir(parents=True,exist_ok=True);q.OUT=directory
            for layer in range(3):
                packets=bytearray();ends=[]
                for block in blocks:
                    span=block['layers'][layer]
                    payload=data[span['payload_offset']:span['payload_offset']+span['payload_bytes']]
                    q.need(q.sha(payload)==span['payload_sha256'],'Layer identity changed')
                    packets.extend(payload);packets.extend(b'\xff'*span['restored_ff_bytes']);ends.append(len(packets)//2048)
                packet_path=directory/f'layer{layer}.packets';packet_path.write_bytes(packets)
                entry=dict(layer=layer,packet_sha256=q.sha(packets),packets=len(packets)//2048,runs=[])
                raw=None
                for mode,split in (('xma1',0),('xma2',0),('xma2',1)):
                    label=f'layer{layer}-{mode}-{split}';pcm,csv=directory/(label+'.f32le'),directory/(label+'.csv')
                    result=q.run([OUT/'harness.exe',mode,2,packet_path,pcm,csv,split],env,label,120)
                    lines=result.stdout.decode().splitlines();modules={}
                    for line in lines:
                        if line.startswith('module '):
                            _,name,path=line.split(' ',2)
                            q.need(Path(path).resolve()==(q.INSTALL/'bin'/name).resolve(),'Unowned native decoder DLL');modules[name]=path
                    q.need(len(modules)==3,'Decoder module evidence incomplete')
                    summary=next(x for x in lines if x.startswith('result ')).split()
                    q.need(summary[-1]=='no_eof_sent' and int(summary[1])==len(packets)//2048,'Packet accounting/EOF changed')
                    decoded=pcm.read_bytes();q.need(len(decoded)==int(summary[2])*8,'Raw PCM extent mismatch')
                    if raw is None:raw=decoded
                    q.need(decoded==raw,'Raw variants/read schedules differ')
                    entry['runs'].append(dict(mode=mode,split=split,frames=len(decoded)//8,pcm_sha256=q.sha(decoded),modules=modules))
                    if mode=='xma2' and split==0:
                        produced={}
                        for line in csv.read_text(encoding="utf-8").splitlines()[1:]:
                            _,accepted,offset,count=map(int,line.split(','));produced[accepted]=offset+count
                        accumulated=384;last=0;margins=[]
                        for block,end in zip(blocks,ends):
                            last=max(last,max((v for k,v in produced.items() if k<=end),default=0));accumulated+=block['samples']
                            q.need(last>=accumulated,f'Stream{index}/layer{layer} block quota requires future input');margins.append(last-accumulated)
                        entry['minimum_per_block_surplus'],entry['maximum_per_block_surplus']=min(margins),max(margins)
                wave=directory/f'layer{layer}.diagnostic.wav';wave.write_bytes(q.riff(packets))
                stock=q.run([q.CLI,'-nostdin','-hide_banner','-loglevel','error','-xerror','-threads',1,'-f','wav','-i',wave,
                    '-map','0:a:0','-c:a','pcm_f32le','-f','f32le','pipe:1'],env,f'layer{layer}-stock',120)
                native_floats,stock_floats=array.array('f'),array.array('f');native_floats.frombytes(raw);stock_floats.frombytes(stock.stdout)
                q.need(len(stock_floats)>=2048,'Missing independent decoder PCM')
                usable=min(len(stock_floats),len(native_floats)-1152)-1024
                error=max(abs(native_floats[i+1152]-stock_floats[i]) for i in range(usable))
                q.need(error<0.0001,'Independent stock decoder alignment failed')
                entry['stock_measured_offset_frames'],entry['stock_maximum_error']=576,error
                entry['trimmed_sha256']=q.sha(raw[384*8:(384+header['samples'])*8]);report['layers'].append(entry)
            (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        reports.append(report)
        print('Qualified stream',index,info['id_hex'],header['samples'],'frames',len(blocks),'blocks',flush=True)
    q.need(q.sha(q.ORIGINAL.read_bytes())==q.SOURCE_SHA,'Original changed during qualification')
    (OUT/'report.json').write_text(json.dumps(dict(source_sha256=q.SOURCE_SHA,streams=reports),indent=2)+'\n',encoding='utf-8')
    arrays=[];profiles=[]
    for report in reports:
        index=report['stream']['index'];blocks=report['blocks']
        symbol=f'locMusicStream{index}Certificates' if loc else 'menuXmaCertificates' if index==0 else 'menuStartXmaCertificates' if index==1 else f'menuStream{index}Certificates'
        if loc or index>1:
            rows=[]
            for b in blocks:
                lengths=','.join(str(x['payload_bytes']) for x in b['layers']);ff=','.join(str(x['restored_ff_bytes']) for x in b['layers'])
                rows.append('    {"%s",%d,%d,{%s},{%s},%d},'%(b['owned_sha256'],b['bytes'],b['samples'],lengths,ff,b['terminal_padding']))
            arrays.append(f'inline constexpr std::array<EaXmaCertificate,{len(blocks)}> {symbol} = {{{{\n'+'\n'.join(rows)+'\n}};\n')
        header=','.join(f'0x{x:02X}' for x in bytes.fromhex(report['header']))
        hashes=','.join('"'+x['trimmed_sha256']+'"' for x in report['layers'])
        profiles.append('    {{'+header+'},'+str(report['stream']['header']['samples'])+','+symbol+',{'+hashes+'}},')
    includes='#include "all_menu_xma_certificates.h"\n' if loc else '#include "menu_xma_certificates.h"\n#include "menu_start_xma_certificates.h"\n#include <span>\n'
    declaration='' if loc else 'struct MenuXmaProfile {std::array<uint8_t,8> header;uint32_t frames;std::span<const EaXmaCertificate> blocks;std::array<std::string_view,3> trimmedPcmHash;};\n'
    profile_symbol='locMusicXmaProfiles' if loc else 'menuXmaProfiles'
    certificate=('// Independently qualified by tools/qualify_all_menu_xma.py --bank '+args.bank+'; original MUS SHA256 '+q.SOURCE_SHA+'\n'
        '#pragma once\n'+includes+'namespace Simpsons::Audio {\n'+'\n'.join(arrays)+'\n'+declaration+
        f'inline constexpr std::array<MenuXmaProfile,{expected_count}> {profile_symbol} = {{{{\n'+'\n'.join(profiles)+'\n}};\n}\n')
    filename='loc_music_xma_certificates.h' if loc else 'all_menu_xma_certificates.h'
    (ROOT/'audio'/filename).write_text(certificate,encoding='utf-8')
    print(f'PASS all{expected_count} {args.bank} streams; exact blocks, complete quotas, three schedules and independent stock diagnostic; no raw EOF',flush=True)


if __name__=='__main__':main()
