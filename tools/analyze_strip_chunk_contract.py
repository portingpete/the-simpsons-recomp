"""Pin original primitive6 SDK packet splitting and restart-boundary winding.

This is source and arithmetic evidence. Native owner, pixel and retirement
evidence must come from independently executed original/GPU regressions.
"""
import argparse
import json
from pathlib import Path
from analyze_short_strip_contract import BASE, IMAGE_SHA, ROOT, SDK_START, SDK_STOP, SDK_SHA, verify as verify_draw

TABLE=0x821D3D30
TABLE_BYTES=bytes.fromhex("00000000 00000000 00000001 00000000 00000002 00000000 00000001 00000001 "
                         "00000003 00000000 00000001 00000002 00000001 00000002 00000000 00000000")
SKY_CULL_PINS={
    0x827408A8:bytes.fromhex("813F0000 550B06B4 2F0B0000 81690008 5575FFFE 409A0034 56A8063E 2B080000 409A0020 556B07FE 2B0B0000 409A0014 554B063E 2B0B0000 7FCBF378 419A0008 39600001 5577063E 897F000C 2B0B0000 409A0008 7FD7F378 56EB063E 2B0B0000 419A0008 9BDF000C"),
    0x82740298:bytes.fromhex("38C00000 891F000C 80E30010 80BF0018 809F0004 4BFC0F75"),
    0x8270131C:bytes.fromhex("563A063E 3AE00000 2B1A0000 419A0014"),
    0x8270133C:bytes.fromhex("38600005 4BFB6601 897C0003 7C771B78 2B0B0000 38A00001 38600005 38800000 419A0008 38800002 4BFB6605"),
    0x827013E0:bytes.fromhex("2B1A0000 419A0010 7F03C378 4BFFE26D 48000014 38A00001 7EE4BB78 38600005 4BFB6569"),
}


def verify(image,check_hash=True):
    verify_draw(image,check_hash)
    if image[TABLE-BASE:TABLE-BASE+64]!=TABLE_BYTES:
        raise ValueError("Original primitive factor/overlap table changed")
    for address,expected in SKY_CULL_PINS.items():
        if image[address-BASE:address-BASE+len(expected)]!=expected:
            raise ValueError("Original sky alpha/material cull transport changed")


def packets(count,start=0):
    if not 0<=count<=0xFFFFFFFF or not 0<=start<=0xFFFFFFFF:
        raise ValueError("Not original DWORD transport")
    factor=int.from_bytes(TABLE_BYTES[6*8:6*8+4],"big")
    overlap=int.from_bytes(TABLE_BYTES[6*8+4:6*8+8],"big")
    out=[]
    while True:
        emitted=count if count<=65535 else ((65535//factor)&~1)*factor
        # Original D5B0 uses only primitive&3F. D618/D624 add count<<16.
        initiator=(emitted<<16)|6
        if initiator&0x1000 or initiator>>16!=emitted:
            raise ValueError("SDK packet acquired continuation or truncated count")
        out.append((emitted,start))
        remainder=count-emitted
        if not remainder:return out
        count=remainder+overlap
        start=(start+emitted-overlap)&0xFFFFFFFF


def triangles(words):
    recent=[];parity=0;out=[]
    for value in words:
        if value==0xFFFF:
            recent=[];parity=0;continue
        recent.append(value)
        if len(recent)<3:continue
        a,b,c=recent[-3:]
        out.append((a,b,c) if not parity&1 else (b,a,c))
        parity+=1
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image",type=Path,default=ROOT/"analysis/simpsons.pe")
    args=parser.parse_args();image=args.image.read_bytes();verify(image)
    boundaries=[]
    for count in (0,1,2,65534,65535,65536,131067,131068,0x08000000,0xFFFFFFFF):
        emitted=packets(count,1)
        if emitted[0][1]!=1 or any(n>65535 for n,_ in emitted):
            raise ValueError("Original packet bounds differ")
        for first,second in zip(emitted,emitted[1:]):
            if first[0]!=65534 or second[1]!=(first[1]+65532)&0xFFFFFFFF:
                raise ValueError("Original primitive6 overlap differs")
        boundaries.append({"count":count,"packets":len(emitted),"first":emitted[0],"last":emitted[-1]})
    indices=[0xFFFF]*65536;indices[65531:]=[0,1,2,3,4]
    whole=triangles(indices);split=[]
    for count,start in packets(len(indices)):
        split.extend(triangles(indices[start:start+count]))
    if whole!=[(0,1,2),(2,1,3),(2,3,4)] or split!=[(0,1,2),(1,2,3),(3,2,4)] or whole==split:
        raise ValueError("Restart-boundary fixture no longer distinguishes packet winding")
    mutation_offsets=set(range(SDK_START-BASE,SDK_STOP-BASE))
    mutation_offsets.update(range(0x8270139C-BASE,0x827013B4-BASE))
    mutation_offsets.update(range(TABLE-BASE,TABLE-BASE+64))
    for address,expected in SKY_CULL_PINS.items():
        mutation_offsets.update(range(address-BASE,address-BASE+len(expected)))
    for offset in mutation_offsets:
        changed=bytearray(image);changed[offset]^=1
        try:verify(changed,False)
        except ValueError:pass
        else:raise ValueError("Mutated original splitting source admitted")
    print(json.dumps({"image_sha256":IMAGE_SHA,"complete_sdk_body_sha256":SDK_SHA,
        "original_primitive_table":"821D3D30 primitive6 factor1/overlap2",
        "original_recurrence":"8244D5D8..D614 splits counts above65535 into65534-word packets.8244D788..D7AC carries remaining+2 and advances start65532. Initiator bit12 is0 on every R16 packet.",
        "original_sky_cull":"827408A8..8274090C derives logical alpha from metadata and request, then clears packet+12 for the fixture's metadata-bit0 profile. Fallback logical alpha still selects the alpha technique;8274029C passes the cleared packet byte as static r8, so both complete shader pairs use the opaque material-state branch.82701344..64 selects cull0 for material byte3=0, else2, and827013F4..1400 restores requested entry cull2/6. The independent original fixture qualifies material byte3=0/1 and actual draw-time cull0/2. Actual cull6 is separately qualified by the backend suites; it is not claimed for this public producer profile.",
        "boundary_cases":boundaries,"restart_boundary":{"count":65536,"reset_index_position":65530,"single_strip_triangles":whole,"original_packet_triangles":split},
        "mutation_checks_passed":len(mutation_offsets),
        "scope":"Full DWORD count transport and source packet arithmetic only; wrapped starts and fetched-owner overflows remain unqualified. Native helper must run only after complete selected index/vertex ownership validation. New original/GPU regression execution pending."},indent=2))


if __name__=="__main__":main()
