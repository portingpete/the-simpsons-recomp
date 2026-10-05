"""Bounded, reversible core-placement comparison for one verified game thread.

Changes only that thread's native affinity and restores it in finally. No guest
memory/register writes, priority changes, input commands or system settings.
Uses real presentation CSV intervals between requested raw renderer captures.
"""
import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import struct
import time
from sample_native_process import verified_process, ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--tid',type=int,required=True)
    parser.add_argument('--captures',type=Path,required=True)
    parser.add_argument('--frames',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    captures=args.captures.resolve();frames=args.frames.resolve();output=args.output.resolve()
    if captures.parent!=ROOT/'build/captures' or not captures.is_dir():parser.error('Capture directory must exist beneath build/captures')
    if frames.parent!=ROOT/'build' or not frames.is_file():parser.error('CSV must exist directly beneath build')
    if output.parent!=ROOT/'build/native-process-sampling' or output.exists() or output.suffix!='.json':parser.error('Output must be a new sampling JSON')
    k=c.WinDLL('kernel32',use_last_error=True)
    def api(name,types,result):
        fn=getattr(k,name);fn.argtypes=types;fn.restype=result;return fn
    open_thread=api('OpenThread',[w.DWORD,w.BOOL,w.DWORD],w.HANDLE)
    close=api('CloseHandle',[w.HANDLE],w.BOOL)
    owner=api('GetProcessIdOfThread',[w.HANDLE],w.DWORD)
    class GroupAffinity(c.Structure):
        _fields_=[('mask',c.c_size_t),('group',w.WORD),('reserved',w.WORD*3)]
    get_affinity=api('GetThreadGroupAffinity',[w.HANDLE,c.POINTER(GroupAffinity)],w.BOOL)
    set_affinity=api('SetThreadAffinityMask',[w.HANDLE,c.c_size_t],c.c_size_t)
    topology=api('GetLogicalProcessorInformationEx',[c.c_int,c.c_void_p,c.POINTER(w.DWORD)],w.BOOL)
    def check(ok):
        if not ok:raise c.WinError(c.get_last_error())
    size=w.DWORD();topology(0,None,c.byref(size))
    if not 0<size.value<1024*1024:raise RuntimeError('Unexpected processor topology size')
    storage=c.create_string_buffer(size.value);check(topology(0,storage,c.byref(size)))
    cores=[];at=0
    while at<size.value:
        relation,length=struct.unpack_from('<II',storage.raw,at)
        if relation!=0 or length<48 or at+length>size.value:raise RuntimeError('Unexpected core topology record')
        flags,efficiency=struct.unpack_from('<BB',storage.raw,at+8)
        groups=struct.unpack_from('<H',storage.raw,at+30)[0]
        if groups!=1:raise RuntimeError('Comparison requires single-group cores')
        mask,group=struct.unpack_from('<QH',storage.raw,at+32)
        if group!=0 or not mask:raise RuntimeError('Comparison requires group zero')
        cores.append({'mask':mask,'efficiency':efficiency,'flags':flags});at+=length
    fastest=max(core['efficiency'] for core in cores)
    performance=[core for core in cores if core['efficiency']==fastest]
    if len(performance)<3:raise RuntimeError('Need at least three performance cores for this comparison')
    def capture():
        request=captures/'capture.request'
        try:
            with request.open('xb'):pass
        except FileExistsError:
            raise RuntimeError('Capture request already pending')
        deadline=time.monotonic()+10
        while request.exists():
            if time.monotonic()>deadline:raise TimeoutError('Capture remains pending')
            time.sleep(.02)
        candidates=list(captures.glob('native-frame-*.rgb10a2'))
        if not candidates:
            raise RuntimeError('No renderer captures found')
        def _key(path):
            try:
                return int(path.stem.removeprefix('native-frame-'))
            except ValueError:
                return -1
        candidates=[p for p in candidates if _key(p)>=0]
        if not candidates:
            raise RuntimeError('No well-formed renderer captures found')
        latest=max(candidates,key=_key)
        try:
            meta=json.loads(latest.with_suffix('.json').read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise RuntimeError(f'Unreadable capture metadata {latest}: {exc}')
        if not meta['display_accepted'] or (meta['width'],meta['height'])!=(1280,720):raise RuntimeError('Capture is not a visible full-size presentation')
        return {'path':str(latest),'presentation':meta['presentation'],'metadata':meta}
    with verified_process(args.pid) as identity:
        thread=open_thread(0x0060,False,args.tid) # QUERY_INFORMATION | SET_INFORMATION
        check(thread)
        try:
            if owner(thread)!=args.pid:raise RuntimeError('Thread is not owned by the verified game')
            original=GroupAffinity();check(get_affinity(thread,c.byref(original)))
            if original.group!=0 or not original.mask or original.mask&(original.mask-1):raise RuntimeError('Original game affinity must be one group-zero processor')
            alternatives=[core['mask']&-core['mask'] for core in performance if not core['mask']&original.mask]
            masks=[original.mask,alternatives[len(alternatives)//2],alternatives[-1],original.mask]
            result={'identity':identity,'tid':args.tid,'topology':cores,'original_mask':original.mask,'phases':[],'restored':False}
            try:
                for mask in masks:
                    check(set_affinity(thread,mask));selected=GroupAffinity();check(get_affinity(thread,c.byref(selected)))
                    if selected.mask!=mask or selected.group!=0:raise RuntimeError('Requested thread affinity was not selected')
                    time.sleep(1);begin=capture();time.sleep(8);end=capture()
                    phase={'mask':mask,'begin':begin,'end':end};result['phases'].append(phase)
                    print(f"Measured mask={mask:X} presentations={begin['presentation']}..{end['presentation']}",flush=True)
            finally:
                check(set_affinity(thread,original.mask));restored=GroupAffinity();check(get_affinity(thread,c.byref(restored)))
                result['restored']=restored.group==original.group and restored.mask==original.mask
                output.write_text(json.dumps(result,indent=2)+'\n', encoding="utf-8")
                if not result['restored']:raise RuntimeError('Original game affinity was not restored')
            time.sleep(4) # Allow the CSV's next bounded flush, outside every measured window.
            with frames.open(newline='',encoding='utf-8') as stream:
                rows=list(csv.DictReader(stream))
            for phase in result['phases']:
                first=phase['begin']['presentation']+21;last=phase['end']['presentation']-21
                selected=[row for row in rows if None not in row and row.get('display_accepted')=='1' and first<=int(row['presentation'])<=last]
                if not selected or len(selected)!=last-first+1:raise RuntimeError('Incomplete accepted presentation window')
                phase.update(first=first,last=last,count=len(selected),fps=1000/(sum(float(row['frame_ms']) for row in selected)/len(selected)))
                print(f"mask={phase['mask']:X}: {phase['fps']:.3f} FPS ({len(selected)} real presentation intervals)",flush=True)
            output.write_text(json.dumps(result,indent=2)+'\n', encoding="utf-8")
        finally:close(thread)


if __name__=='__main__':main()
