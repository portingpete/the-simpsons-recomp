"""Qualify the pinned loc.sbk resident sounds, including fresh-context loops.

Original823428C4 and82342988 enqueue each resident loop block with r6=1.
Every block is therefore checked independently; no PCM looping or synthetic EOF.
"""
import array
import importlib.util
import json
import os
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import qualify_resident_xma as q
from probe_xma_multilayer import CLI, riff

ROOT = q.ROOT
OUT = ROOT/'build/loc-resident-xma'
BANK_HASH = '702aebda44b3a0f36866b35b89d00c0fd7d5cbfa8dfd7409df4cc562122e71eb'


def discover(data, *, audio_offset=0x7740, expected_count=132, expected_loops=12):
    profiles = []
    for offset in range(audio_offset, len(data)-24):
        if data[offset] != 3:
            continue
        first, second = struct.unpack_from('>II', data, offset)
        channels, rate = ((first >> 18) & 63)+1, first & 0x3ffff
        total, loop = second & 0x1fffffff, (second >> 29) & 1
        if (channels not in (1, 2) or rate not in (8000, 24000, 32000, 44100, 48000)
                or second >> 30 or not 0 < total <= 524288):
            continue
        start = struct.unpack_from('>I', data, offset+8)[0] if loop else None
        at = offset+8+4*loop
        blocks = []
        remaining = total
        for index in range(2 if loop and start else 1):
            if at+12 > len(data):
                break
            size, frames, layer = struct.unpack_from('>III', data, at)
            expected = start if loop and start and index == 0 else remaining
            if not (12 < size <= len(data)-at and layer >> 2 == size-8
                    and frames == expected and frames > 0):
                break
            blocks.append(dict(offset=offset, block_offset=at,
                header=data[offset:offset+8].hex(), block_bytes=size, frames=frames,
                channels=channels, playback_rate=rate, selector=layer & 3,
                codec_rate=(24000, 32000, 44100, 48000)[layer & 3],
                restored_ff=(-(size-12)) % 2048, block_hash=q.sha(data[at:at+size])))
            at += size
            remaining -= frames
        if remaining == 0 and blocks:
            profiles.append(dict(offset=offset, total_frames=total, loop_start=start, blocks=blocks))
    q.need(len(profiles) == expected_count and sum(p['loop_start'] is not None for p in profiles) == expected_loops,
           'Pinned resident source inventory changed')
    for a, b in zip(profiles, profiles[1:]):
        last = a['blocks'][-1]
        q.need(last['block_offset']+last['block_bytes'] <= b['offset'], 'Overlapping loc sounds')
    return profiles


def qualify(data, profile, env):
    at, size, channels = profile['block_offset'], profile['block_bytes'], profile['channels']
    packets = data[at+12:at+size]+b'\xff'*profile['restored_ff']
    packet_path = OUT/f'{at}.packets'
    packet_path.write_bytes(packets)
    raw = None
    profile['runs'] = []
    for mode, split in (('xma1', 0), ('xma2', 0), ('xma2', 1)):
        label = f'{at}-{mode}-{split}'
        pcm, csv = OUT/(label+'.f32le'), OUT/(label+'.csv')
        result = q.run([OUT/f'harness-{profile["codec_rate"]}.exe', mode, channels,
                        packet_path, pcm, csv, split], env, label)
        lines = result.stdout.decode().splitlines()
        modules = {}
        for line in lines:
            if line.startswith('module '):
                _, name, path = line.split(' ', 2)
                q.need(Path(path).resolve() == (q.INSTALL/'bin'/name).resolve(), 'Unowned decoder')
                modules[name] = path
        q.need(len(modules) == 3, 'Incomplete native module evidence')
        summary = next(line for line in lines if line.startswith('result ')).split()
        decoded = pcm.read_bytes()
        frames = len(decoded)//(channels*4)
        q.need(summary[-1] == 'no_eof_sent' and int(summary[1]) == len(packets)//2048
               and len(decoded) == int(summary[2])*channels*4 and frames >= profile['frames']+384,
               'Original complete quota unavailable without EOF')
        if raw is None:
            raw = decoded
        q.need(raw == decoded, 'Native decoder variants/read schedules differ')
        profile['runs'].append(dict(mode=mode, split=split, raw_frames=frames,
                                    raw_hash=q.sha(decoded), modules=modules))
    profile['raw_frames'], profile['raw_hash'] = frames, q.sha(raw)
    profile['surplus'] = frames-profile['frames']-384
    diagnostic, boundaries = q.packet_boundary_diagnostic(packets)
    if profile['restored_ff'] or boundaries:
        profile['stock_packet_boundary_trailers_cleared'] = boundaries
        envelope = bytearray(riff(diagnostic, channels))
        struct.pack_into('<I', envelope, 24, profile['codec_rate'])
        wave = OUT/f'{at}.diagnostic.wav'
        wave.write_bytes(envelope)
        stock = q.run([CLI, '-nostdin', '-hide_banner', '-loglevel', 'error', '-xerror',
            '-threads', 1, '-f', 'wav', '-i', wave, '-map', '0:a:0', '-c:a', 'pcm_f32le',
            '-f', 'f32le', 'pipe:1'], env, f'{at}-stock')
        native_floats, stock_floats = array.array('f'), array.array('f')
        native_floats.frombytes(raw)
        stock_floats.frombytes(stock.stdout)
        common = min(len(stock_floats), max(0, len(native_floats)-576*channels))
        usable = common-512*channels if common > 512*channels else common
        if usable:
            error = max(abs(native_floats[i+576*channels]-stock_floats[i]) for i in range(usable))
            q.need(error < 0.0001, 'Independent stock alignment failed')
            profile['stock_maximum_error'] = error
            profile['stock_compared_frames'] = usable//channels
        else:
            q.need(frames <= 576, 'Unexpected lack of stock diagnostic PCM')
            profile['stock_limitation'] = 'Single-frame intro is wholly trimmed by stock576-frame delay; native XMA1/XMA2 and both read schedules match.'


def row(p):
    header = ','.join(f'0x{x:02X}' for x in bytes.fromhex(p['header']))
    values = ','.join(str(p[k]) for k in ('offset', 'block_bytes', 'frames', 'raw_frames',
        'channels', 'selector', 'codec_rate', 'playback_rate', 'restored_ff'))
    return '{{'+header+'},'+values+',"'+p['block_hash']+'","'+p['raw_hash']+'"}'


def main():
    from extract_resource import select_payload
    root = ROOT/'Simpsons Game, The (USA)'
    data, provenance = select_payload(root/'loc/loc.str', 2, 'loc.sbk', root)
    q.need(q.sha(data) == BANK_HASH, 'Original loc bank changed')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'loc.sbk').write_bytes(data)
    q.OUT = OUT
    profiles = discover(data)
    spec = importlib.util.spec_from_file_location('loc_toolchain', ROOT/'tests/test_host_fp.py')
    tc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tc)
    compiler, env = tc.toolchain()
    env['PATH'] = str(q.INSTALL/'bin')+os.pathsep+env['PATH']
    q.run([sys.executable, '-B', ROOT/'tools/build_native_audio_codec.py', '--verify'], env, 'codec-verify')
    for rate in sorted({b['codec_rate'] for p in profiles for b in p['blocks']}):
        harness = q.CPP.replace('{channels,48000,', f'{{channels,{rate},').replace('frames<=262144', 'frames<=524288')
        source = OUT/f'harness-{rate}.cpp'
        source.write_text(harness, encoding='utf-8')
        q.run([compiler, '/nologo', '/std:c++20', '/EHsc', '/MD', '/O2', '/fp:strict',
            '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', f'/I{ROOT}', f'/I{q.INSTALL/"include"}',
            source, ROOT/'audio/native_xma_codec.cpp', '/Fo'+str(OUT)+'\\',
            '/Fe'+str(OUT/f'harness-{rate}.exe'), '/link', q.INSTALL/'lib/avcodec-simpsonsxma.lib',
            q.INSTALL/'lib/avutil-simpsonsxma.lib', '/INCREMENTAL:NO'], env, f'compile-{rate}', 90)
    admitted, excluded = [], {}
    for profile in profiles:
        try:
            for block in profile['blocks']:
                qualify(data, block, env)
            admitted.append(profile)
            print('Qualified', profile['offset'], 'loop', profile['loop_start'], flush=True)
        except RuntimeError as error:
            excluded[profile['offset']] = str(error)
            print('UNSUPPORTED', profile['offset'], str(error).splitlines()[0], flush=True)
    q.need(any(p['offset'] == 1829484 for p in admitted), 'Reached loop did not qualify')
    (OUT/'report.json').write_text(json.dumps(dict(bank_sha256=BANK_HASH, provenance=provenance,
        raw_eof_sent=False, profiles=admitted, excluded=excluded), indent=2)+'\n', encoding='utf-8')
    normals = [p['blocks'][0] for p in admitted if p['loop_start'] is None]
    loops = [p for p in admitted if p['loop_start'] is not None]
    lines = ['// Generated by tools/qualify_loc_resident_xma.py after native decode verification.',
        '// Original loc.sbk SHA256 '+BANK_HASH, '#pragma once', '#include "resident_xma.h"',
        'namespace Simpsons::Audio {',
        f'inline constexpr std::array<ResidentXmaProfile,{len(normals)}> locResidentXmaProfiles = {{{{']
    lines += ['    '+row(p)+',' for p in normals]+['}};']
    for p in loops:
        lines += [f'inline constexpr std::array<ResidentXmaProfile,{len(p["blocks"])}> locLoop{p["offset"]} = {{{{']
        lines += ['    '+row(b)+',' for b in p['blocks']]+['}};']
    lines += [f'inline constexpr std::array<ResidentLoopProfile,{len(loops)}> locResidentLoopProfiles = {{{{']
    lines += ['    {'+f'{p["offset"]},{p["total_frames"]},{p["loop_start"]},locLoop{p["offset"]}'+'},' for p in loops]
    lines += ['}};', '}']
    (ROOT/'audio/loc_resident_xma_certificates.h').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print('PASS', len(normals), 'normal sources,', len(loops), 'loops;', len(excluded), 'excluded', flush=True)


if __name__ == '__main__':
    main()
