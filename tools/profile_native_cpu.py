"""Bounded instruction-pointer sampling of one verified native-game thread.

Uses Windows control context (plus integer registers for copy diagnostics), resumes after every read, and symbolizes
the game's offsets against its local PDB. Does not write target memory or
registers. Sampling pauses are diagnostic overhead, not FPS evidence.
"""
import argparse
import collections
import ctypes as c
from ctypes import wintypes as w
import json
import hashlib
import re
import shutil
from pathlib import Path
import os
import subprocess
import time

from sample_native_process import verified_process, EXPECTED, ROOT


def validated_symbol_image(expected):
    """Keep LLVM's adjacent-PDB preference from silently loading stale symbols."""
    llvm=Path(r'C:\Program Files\LLVM\bin')
    def output(command):
        return subprocess.run(command,text=True,capture_output=True,check=True,timeout=30,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
    directory=output([str(llvm/'llvm-readobj.exe'),'--coff-debug-directory',str(expected)])
    guid=re.search(r'PDBGUID:\s*(\{[^}]+\})',directory)
    age=re.search(r'PDBAge:\s*(\d+)',directory)
    path=re.search(r'PDBFileName:\s*(.+)',directory)
    if not guid or not age or not path:raise ValueError('Game image has no complete PDB identity')
    embedded=Path(path[1].strip()).resolve()
    adjacent=expected.with_suffix('.pdb')
    for candidate in dict.fromkeys((adjacent,embedded)):
        if not candidate.is_relative_to((ROOT/'build').resolve()) or not candidate.is_file():continue
        summary=output([str(llvm/'llvm-pdbutil.exe'),'dump','--summary',str(candidate)])
        actual_guid=re.search(r'GUID:\s*(\{[^}]+\})',summary)
        actual_age=re.search(r'Age:\s*(\d+)',summary)
        if not actual_guid or not actual_age or actual_guid[1]!=guid[1] or actual_age[1]!=age[1]:continue
        if candidate==adjacent:return expected
        # The isolated build's root PDB can be held open externally. Its new
        # matching symbols live here. Copy only the image beside that verified
        # PDB; the live executable identity remains the original expected path.
        if candidate.parent!=expected.parent/'symbols' or candidate.name!=adjacent.name:
            raise ValueError('Matching PDB requires an unsupported symbol-image directory')
        image=candidate.parent/expected.name
        digest=hashlib.sha256(expected.read_bytes()).digest()
        if not image.exists() or hashlib.sha256(image.read_bytes()).digest()!=digest:
            shutil.copy2(expected,image)
        if hashlib.sha256(image.read_bytes()).digest()!=digest:raise ValueError('Symbol image copy differs from game executable')
        return image
    raise ValueError('No local PDB matches the executable GUID and age')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    p.add_argument('--executable',type=Path,default=EXPECTED,help='Exact native game build to verify and symbolize')
    p.add_argument('--tid',type=int,required=True)
    p.add_argument('--seconds',type=float,default=8)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--leaf-contract',type=Path,help='Verified no-stack-change leaf RVA/size/code hash for reading its caller only')
    p.add_argument('--copy-callers',action='store_true',help='Read callers only at the exact verified VCRUNTIME rep-movsb instruction')
    p.add_argument('--nt-wait-callers',action='store_true',help='Read callers only at the verified stack-preserving NtWaitForSingleObject return instruction')
    a=p.parse_args()
    expected=a.executable.resolve()
    if expected.name!='SimpsonsNative.exe' or not expected.is_relative_to((ROOT/'build').resolve()) or not expected.is_file():
        p.error('--executable must identify an existing SimpsonsNative.exe beneath this workspace build directory')
    if not 0<a.seconds<=20:p.error('Sampling duration must be within 0..20 seconds')
    output=a.output.resolve()
    if output.parent!=EXPECTED.parent.parent/'native-process-sampling' or output.suffix!='.json':
        p.error('Output must be a new JSON file in build/native-process-sampling')
    output.parent.mkdir(exist_ok=True)
    if output.exists():p.error('Output already exists')
    leaf=json.loads(a.leaf_contract.read_text(encoding="utf-8")) if a.leaf_contract else None
    if leaf and (not isinstance(leaf.get('rva'),int) or leaf['rva']<4096 or
                 not isinstance(leaf.get('size'),int) or not 1<=leaf['size']<=4096 or
                 len(leaf.get('sha256',''))!=64):
        p.error('Invalid verified leaf contract')
    k=c.WinDLL('kernel32',use_last_error=True)
    def api(name,args,result):
        fn=getattr(k,name);fn.argtypes=args;fn.restype=result;return fn
    open_process=api('OpenProcess',[w.DWORD,w.BOOL,w.DWORD],w.HANDLE)
    open_thread=api('OpenThread',[w.DWORD,w.BOOL,w.DWORD],w.HANDLE)
    close=api('CloseHandle',[w.HANDLE],w.BOOL)
    thread_pid=api('GetProcessIdOfThread',[w.HANDLE],w.DWORD)
    suspend=api('SuspendThread',[w.HANDLE],w.DWORD)
    resume=api('ResumeThread',[w.HANDLE],w.DWORD)
    context=api('GetThreadContext',[w.HANDLE,c.c_void_p],w.BOOL)
    read=api('ReadProcessMemory',[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)],w.BOOL)
    enum=api('K32EnumProcessModulesEx',[w.HANDLE,c.POINTER(w.HMODULE),w.DWORD,c.POINTER(w.DWORD),w.DWORD],w.BOOL)
    name=api('K32GetModuleFileNameExW',[w.HANDLE,w.HMODULE,w.LPWSTR,w.DWORD],w.DWORD)
    class ModuleInfo(c.Structure):
        _fields_=[('base',c.c_void_p),('size',w.DWORD),('entry',c.c_void_p)]
    info=api('K32GetModuleInformation',[w.HANDLE,w.HMODULE,c.POINTER(ModuleInfo),w.DWORD],w.BOOL)
    counts=collections.Counter();callers=collections.Counter();copies=collections.Counter();waits=collections.Counter();wait_owners=collections.Counter()
    modules=[];max_pause=0;caller_reads_failed=0;copy_reads_failed=0;copy_module=None
    wait_reads_failed=0;wait_owner_reads_failed=0;wait_module=None;wait_wrapper=None
    with verified_process(a.pid,expected) as identity:
        symbol_image=validated_symbol_image(expected)
        process=open_process(0x410,False,a.pid)
        if not process:raise c.WinError(c.get_last_error())
        thread=None
        try:
            handles=(w.HMODULE*1024)();needed=w.DWORD()
            if not enum(process,handles,c.sizeof(handles),c.byref(needed),3):raise c.WinError(c.get_last_error())
            if needed.value>c.sizeof(handles):raise RuntimeError('Module enumeration exceeded its bound')
            for handle in handles[:needed.value//c.sizeof(w.HMODULE)]:
                path=c.create_unicode_buffer(32768);m=ModuleInfo()
                if not name(process,handle,path,len(path)) or not info(process,handle,c.byref(m),c.sizeof(m)):
                    raise c.WinError(c.get_last_error())
                modules.append(dict(path=path.value,base=m.base,size=m.size))
            if not any(Path(m['path'])==expected for m in modules):raise RuntimeError('Expected game module missing')
            game_module=next(m for m in modules if Path(m['path'])==expected)
            if a.nt_wait_callers:
                # Reviewed local x64 syscall stub: mov r10,rcx; mov eax,4;
                # test shared-data flag; conditional syscall/int2e; ret. No
                # stack adjustment, pushes or calls. At +0x14 the syscall has
                # returned and RSP still points to its direct caller's return.
                wait_module=next((m for m in modules if Path(m['path'])==Path(r'C:\Windows\System32\ntdll.dll')),None)
                if not wait_module:raise ValueError('Reviewed NT runtime module is not loaded')
                reviewed_wait=bytes.fromhex('4c8bd1b804000000f604250803fe7f0175030f05c3cd2ec30f1f840000000000')
                code=c.create_string_buffer(len(reviewed_wait));received=c.c_size_t()
                if not read(process,wait_module['base']+0x1603F0,code,len(code),c.byref(received)) or received.value!=len(code):
                    raise c.WinError(c.get_last_error())
                if code.raw!=reviewed_wait:raise ValueError('Live NT wait stub differs from reviewed instruction/stack contract')
                # The complete reviewed WaitForSingleObjectEx body uses three
                # pushes and a fixed0x80 frame, with no other stack adjustment
                # before its NT call at1C118. For exactly that return, the
                # wrapper's caller is at the sampled NT RSP+8+0x98. This is
                # one verified frame, not a general stack-unwinding heuristic.
                wait_wrapper=next((m for m in modules if Path(m['path'])==Path(r'C:\Windows\System32\KERNELBASE.dll')),None)
                if not wait_wrapper:raise ValueError('Reviewed wait wrapper module is not loaded')
                wrapper_hash='5b6928fab736b23f8ef6e81239e28eb848236c186e1dae159ff0e49e711e49ad'
                code=c.create_string_buffer(0x157);received=c.c_size_t()
                if not read(process,wait_wrapper['base']+0x1C070,code,len(code),c.byref(received)) or received.value!=len(code):
                    raise c.WinError(c.get_last_error())
                if hashlib.sha256(code.raw).hexdigest()!=wrapper_hash:raise ValueError('Live wait wrapper differs from reviewed fixed-frame contract')
            if a.copy_callers:
                # Local disassembly: two pushes, three register moves, rep movsb,
                # two pops, ret. At rep movsb the return address is RSP+16 and R8
                # retains the copy size. Sample no other instruction/prologue.
                copy_module=next((m for m in modules if Path(m['path'])==Path(r'C:\Windows\System32\VCRUNTIME140.dll')),None)
                if not copy_module:raise ValueError('Reviewed runtime module is not loaded')
                reviewed=bytes.fromhex('5756488bf9488bf2498bc8f3a45e5fc3')
                code=c.create_string_buffer(len(reviewed));received=c.c_size_t()
                if not read(process,copy_module['base']+0x1DAF0,code,len(code),c.byref(received)) or received.value!=len(code):
                    raise c.WinError(c.get_last_error())
                if code.raw!=reviewed:raise ValueError('Live copy helper differs from reviewed instruction/stack contract')
            if leaf:
                if leaf['rva']+leaf['size']>game_module['size']:raise ValueError('Leaf contract exceeds game module')
                code=c.create_string_buffer(leaf['size']);received=c.c_size_t()
                if not read(process,game_module['base']+leaf['rva'],code,len(code),c.byref(received)) or received.value!=len(code):
                    raise c.WinError(c.get_last_error())
                if hashlib.sha256(code.raw).hexdigest()!=leaf['sha256']:raise ValueError('Live leaf code differs from the reviewed contract')
            thread=open_thread(0x80A,False,a.tid)
            if not thread:raise c.WinError(c.get_last_error())
            if thread_pid(thread)!=a.pid:raise ValueError('Thread does not belong to the verified game')
            # Windows SDK x64 CONTEXT: 16-byte alignment, flags at48, RIP at248.
            storage=c.create_string_buffer(1248);address=(c.addressof(storage)+15)&~15
            started=time.perf_counter();deadline=started+a.seconds
            while time.perf_counter()<deadline:
                c.c_uint32.from_address(address+48).value=0x100003 if a.copy_callers else 0x100001
                before=time.perf_counter();previous=suspend(thread)
                if previous==0xffffffff:raise c.WinError(c.get_last_error())
                try:
                    if previous:raise RuntimeError('Thread was already suspended; refusing concurrent debugging')
                    if not context(thread,address):raise c.WinError(c.get_last_error())
                    pc=c.c_uint64.from_address(address+248).value
                    if wait_module and pc==wait_module['base']+0x160404:
                        stack=c.c_uint64.from_address(address+152).value
                        caller=c.c_uint64();received=c.c_size_t()
                        if read(process,stack,c.byref(caller),8,c.byref(received)) and received.value==8:
                            waits[caller.value-1]+=1
                            if caller.value==wait_wrapper['base']+0x1C11F:
                                owner=c.c_uint64();received=c.c_size_t()
                                if read(process,stack+0xA0,c.byref(owner),8,c.byref(received)) and received.value==8:
                                    wait_owners[owner.value-1]+=1
                                else:wait_owner_reads_failed+=1
                        else:wait_reads_failed+=1
                    if leaf and game_module['base']+leaf['rva']<=pc<game_module['base']+leaf['rva']+leaf['size']:
                        # SDK x64 CONTEXT.Rsp is at152. The supplied exact code
                        # contract must have been inspected to prove no pushes,
                        # stack adjustment or calls. This is not a general unwind.
                        stack=c.c_uint64.from_address(address+152).value
                        caller=c.c_uint64();received=c.c_size_t()
                        if read(process,stack,c.byref(caller),8,c.byref(received)) and received.value==8:
                            if game_module['base']<caller.value<game_module['base']+game_module['size']:
                                callers[caller.value-1]+=1
                        else:caller_reads_failed+=1
                    if copy_module and pc==copy_module['base']+0x1DAFB:
                        stack=c.c_uint64.from_address(address+152).value
                        size=c.c_uint64.from_address(address+184).value
                        caller=c.c_uint64();received=c.c_size_t()
                        if read(process,stack+16,c.byref(caller),8,c.byref(received)) and received.value==8:
                            copies[(caller.value-1,size)]+=1
                        else:copy_reads_failed+=1
                finally:
                    if resume(thread)==0xffffffff:raise c.WinError(c.get_last_error())
                max_pause=max(max_pause,time.perf_counter()-before)
                counts[pc]+=1
                time.sleep(0.001)
            elapsed=time.perf_counter()-started
        finally:
            if thread:close(thread)
            close(process)
    points=[]
    for pc,count in counts.most_common():
        module=next((m for m in modules if m['base']<=pc<m['base']+m['size']),None)
        points.append(dict(pc=hex(pc),samples=count,module=module['path'] if module else None,
                           offset=hex(pc-module['base']) if module else None))
    caller_points=[dict(pc=hex(pc),offset=hex(pc-game_module['base']),samples=count)
                   for pc,count in callers.most_common()]
    copy_points=[]
    for (pc,size),count in copies.most_common():
        module=next((m for m in modules if m['base']<=pc<m['base']+m['size']),None)
        copy_points.append(dict(pc=hex(pc),copy_bytes=size,samples=count,module=module['path'] if module else None,
                                offset=hex(pc-module['base']) if module else None))
    wait_points=[]
    for pc,count in waits.most_common():
        module=next((m for m in modules if m['base']<=pc<m['base']+m['size']),None)
        wait_points.append(dict(pc=hex(pc),samples=count,module=module['path'] if module else None,
                                offset=hex(pc-module['base']) if module else None))
    wait_owner_points=[]
    for pc,count in wait_owners.most_common():
        module=next((m for m in modules if m['base']<=pc<m['base']+m['size']),None)
        wait_owner_points.append(dict(pc=hex(pc),samples=count,module=module['path'] if module else None,
                                      offset=hex(pc-module['base']) if module else None))
    game=[v for v in points+copy_points+wait_points+wait_owner_points if v['module'] and Path(v['module'])==expected]+caller_points
    if game:
        symbolizer=os.environ.get("LLVM_SYMBOLIZER", r'C:\Program Files\LLVM\bin\llvm-symbolizer.exe')
        command=[symbolizer,'--obj='+str(symbol_image),
                 '--relative-address','--output-style=JSON']
        result=subprocess.run(command,input='\n'.join(v['offset'] for v in game)+'\n',
                              text=True,capture_output=True,check=True,timeout=30,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        records=[json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        if len(records)!=len(game):raise RuntimeError('Symbolizer returned an unexpected record count')
        for point,symbol in zip(game,records):point['symbols']=symbol.get('Symbol',[])
    summary=collections.Counter()
    for point in points:
        symbols=point.get('symbols',[])
        label=symbols[0].get('FunctionName','unknown') if symbols else Path(point['module']).name if point['module'] else 'unknown'
        summary[label]+=point['samples']
    report=dict(identity=identity,symbol_image=str(symbol_image),tid=a.tid,seconds=elapsed,samples=sum(counts.values()),
                max_pause_ms=max_pause*1000,summary=summary.most_common(),points=points)
    if leaf:report.update(leaf_contract=leaf,leaf_callers=caller_points,caller_reads_failed=caller_reads_failed)
    if a.copy_callers:report.update(copy_callers=copy_points,copy_reads_failed=copy_reads_failed,
                                   copy_instruction_contract=dict(module=copy_module['path'],rva=0x1DAFB,stack_offset=16,
                                                                  helper_hex='5756488bf9488bf2498bc8f3a45e5fc3'))
    if a.nt_wait_callers:report.update(nt_wait_callers=wait_points,nt_wait_reads_failed=wait_reads_failed,
                                     nt_wait_owners=wait_owner_points,nt_wait_owner_reads_failed=wait_owner_reads_failed,
                                     nt_wait_wrapper_contract=dict(module=wait_wrapper['path'],rva=0x1C070,size=0x157,sha256=wrapper_hash,
                                                                   nt_return_rva=0x1C11F,caller_offset_from_nt_rsp=0xA0),
                                     nt_wait_instruction_contract=dict(module=wait_module['path'],rva=0x160404,stack_offset=0,
                                                                        helper_hex=reviewed_wait.hex()))
    with output.open('x',encoding='utf-8') as f:json.dump(report,f,indent=2)
    print(json.dumps({k:v for k,v in report.items() if k not in ('points','leaf_callers','copy_callers','nt_wait_callers','nt_wait_owners')},indent=2))


if __name__=='__main__':main()
