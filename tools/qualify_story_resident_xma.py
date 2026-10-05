"""Qualify bounded mono/stereo sources in the original story-mode sound bank.

Discovery scans the pinned original bank offline. Production admits only the
emitted exact offsets, headers, block hashes and independently decoded quotas.
"""
import importlib.util
import array
import json
import os
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import qualify_resident_xma as q
from probe_xma_multilayer import CLI, riff

ROOT = q.ROOT
OUT = ROOT / 'build/story-resident-xma'
BANK_HASH='0392a01388db435d73cd81a565c9bc94273a13c53245cb30d596283594bdeca1'


def main():
    from extract_resource import select_payload
    root=ROOT/'Simpsons Game, The (USA)'
    data,provenance=select_payload(root/'loc/loc/story_mode/story_mode_design.str',3,'Story_Mode_Design.sbk',root)
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/'Story_Mode_Design.sbk'
    path.write_bytes(data)
    q.BANK_HASH=BANK_HASH
    q.need(q.sha(data) == q.BANK_HASH, 'Original story bank changed')
    profiles = []
    for offset in range(0x152C0, len(data)-20):
        if data[offset] != 3:
            continue
        first, second, size, frames, layer = struct.unpack_from('>IIIII', data, offset)
        channels, rate = ((first >> 18) & 63) + 1, first & 0x3ffff
        if (channels not in (1,2) or rate not in (8000,24000,32000,44100,48000)
                or second >> 29 or not 0 < frames <= 524288 or second != frames
                or not 12 < size <= len(data)-offset-8
                or layer >> 2 != size-8):
            continue
        profiles.append(dict(offset=offset, header=data[offset:offset+8].hex(),
            block_bytes=size, frames=frames, channels=channels, playback_rate=rate,
            selector=layer & 3, codec_rate=(24000,32000,44100,48000)[layer & 3],
            restored_ff=(-(size-12)) % 2048,
            block_hash=q.sha(data[offset+8:offset+8+size])))
    q.need(len(profiles)==429, 'Pinned story source count changed')
    excluded={}
    for a,b in zip(profiles, profiles[1:]):
        q.need(a['offset']+8+a['block_bytes'] <= b['offset'], 'Resident source spans overlap')
    OUT.mkdir(parents=True, exist_ok=True)
    q.OUT = OUT
    spec = importlib.util.spec_from_file_location('frontend_toolchain', ROOT / 'tests/test_host_fp.py')
    tc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tc)
    compiler, env = tc.toolchain()
    env['PATH'] = str(q.INSTALL / 'bin') + os.pathsep + env['PATH']
    q.run([sys.executable, '-B', ROOT/'tools/build_native_audio_codec.py', '--verify'], env, 'codec-verify')
    for rate in sorted({p['codec_rate'] for p in profiles}):
        harness = q.CPP.replace('{channels,48000,', f'{{channels,{rate},').replace('frames<=262144', 'frames<=524288')
        source = OUT / f'harness-{rate}.cpp'
        source.write_text(harness, encoding='utf-8')
        q.run([compiler, '/nologo','/std:c++20','/EHsc','/MD','/O2','/fp:strict',
            '/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',f'/I{ROOT}',f'/I{q.INSTALL/"include"}',
            source,ROOT/'audio/native_xma_codec.cpp','/Fo'+str(OUT)+'\\',
            '/Fe'+str(OUT/f'harness-{rate}.exe'),'/link',q.INSTALL/'lib/avcodec-simpsonsxma.lib',
            q.INSTALL/'lib/avutil-simpsonsxma.lib','/INCREMENTAL:NO'],env,f'compile-{rate}',90)
    qualified=[]
    for profile in profiles:
        try:
            offset, size, channels = profile['offset'], profile['block_bytes'], profile['channels']
            packets = data[offset+20:offset+8+size] + b'\xff'*profile['restored_ff']
            packet_path = OUT / f'{offset}.packets'
            packet_path.write_bytes(packets)
            raw = None
            profile['runs'] = []
            for mode, split in (('xma1',0),('xma2',0),('xma2',1)):
                label = f'{offset}-{mode}-{split}'
                pcm, csv = OUT/(label+'.f32le'), OUT/(label+'.csv')
                result = q.run([OUT/f'harness-{profile["codec_rate"]}.exe',mode,channels,packet_path,pcm,csv,split],env,label)
                lines = result.stdout.decode().splitlines()
                modules = {}
                for line in lines:
                    if line.startswith('module '):
                        _, name, module_path = line.split(' ',2)
                        q.need(Path(module_path).resolve() == (q.INSTALL/'bin'/name).resolve(), 'Unowned decoder module')
                        modules[name] = module_path
                q.need(len(modules)==3,'Incomplete decoder module evidence')
                summary = next(line for line in lines if line.startswith('result ')).split()
                q.need(summary[-1]=='no_eof_sent' and int(summary[1])==len(packets)//2048,'Packet accounting or EOF changed')
                decoded = pcm.read_bytes()
                raw_frames = len(decoded)//(channels*4)
                q.need(len(decoded)==int(summary[2])*channels*4 and raw_frames>=profile['frames']+384,'Original complete quota unavailable without EOF')
                if raw is None:
                    raw = decoded
                q.need(raw==decoded,'Raw decoder variants/read schedules differ')
                profile['runs'].append(dict(mode=mode,split=split,raw_frames=raw_frames,raw_hash=q.sha(decoded),modules=modules))
            profile['raw_frames'],profile['raw_hash']=raw_frames,q.sha(raw)
            profile['surplus']=raw_frames-profile['frames']-384
            diagnostic,boundaries=q.packet_boundary_diagnostic(packets)
            if profile['restored_ff'] or boundaries:
                # The short resident cues use the same explicitly bounded native
                # FF-tail policy as streamed EA-XMA. Independently check their PCM.
                profile['stock_packet_boundary_trailers_cleared']=boundaries
                envelope=bytearray(riff(diagnostic,channels))
                struct.pack_into('<I',envelope,24,profile['codec_rate'])
                wave=OUT/f'{offset}.diagnostic.wav';wave.write_bytes(envelope)
                stock=q.run([CLI,'-nostdin','-hide_banner','-loglevel','error','-xerror','-threads',1,
                    '-f','wav','-i',wave,'-map','0:a:0','-c:a','pcm_f32le','-f','f32le','pipe:1'],env,f'{offset}-stock')
                native_floats,stock_floats=array.array('f'),array.array('f')
                native_floats.frombytes(raw);stock_floats.frombytes(stock.stdout)
                shift=576*channels;common=min(len(stock_floats),len(native_floats)-shift)
                # Long sources omit the final decoder overlap. Tiny cues have no
                # full512-frame tail; compare their available common PCM directly.
                usable=common-512*channels if common>512*channels else common
                q.need(usable>0,'Missing short-cue stock diagnostic output')
                error=max(abs(native_floats[i+shift]-stock_floats[i]) for i in range(usable))
                q.need(error<0.0001,'Short-cue independent stock alignment failed')
                profile['stock_measured_offset_frames'],profile['stock_maximum_error']=576,error
            print('Qualified',hex(offset),profile['header'],'frames',profile['frames'],'raw',raw_frames,'surplus',profile['surplus'],flush=True)
            qualified.append(profile)
        except RuntimeError as error:
            excluded[profile['offset']]=str(error)
            print('UNSUPPORTED',hex(profile['offset']),str(error).splitlines()[0],flush=True)
    profiles=qualified
    q.need(any(p['offset']==2491363 for p in profiles),'Reached story sound did not qualify')
    q.need(q.sha(path.read_bytes())==q.BANK_HASH,'Original changed during qualification')
    report=dict(bank_sha256=q.BANK_HASH,provenance=provenance,raw_eof_sent=False,profiles=profiles,excluded=excluded)
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    rows=[]
    for p in profiles:
        header=','.join(f'0x{x:02X}' for x in bytes.fromhex(p['header']))
        values=','.join(str(p[k]) for k in ('offset','block_bytes','frames','raw_frames','channels','selector','codec_rate','playback_rate','restored_ff'))
        rows.append('    {{'+header+'},'+values+',"'+p['block_hash']+'","'+p['raw_hash']+'"},')
    certificate=('// Emitted only after tools/qualify_story_resident_xma.py verifies every emitted source.\n'
        '// Original Story_Mode_Design.sbk SHA256 '+BANK_HASH+'\n'
        '#pragma once\n#include "frontend_resident_xma_certificates.h"\nnamespace Simpsons::Audio {\n'
        f'inline constexpr std::array<ResidentXmaProfile,{len(profiles)}> storyResidentXmaProfiles = {{{{\n'+
        '\n'.join(rows)+'\n}};\n}\n')
    (ROOT/'audio/story_resident_xma_certificates.h').write_text(certificate,encoding='utf-8')
    print('PASS',len(profiles),'resident profiles; all schedules match, complete quotas, no EOF; exact restored FF tails recorded',flush=True)


if __name__ == '__main__':
    main()
