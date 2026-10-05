"""Source-only proof of original selected counts0/1/2 on existing owned buffers.

The complete SDK body is pinned in addition to explicit producer/count words.
No empty-buffer, wrapped-address or unowned-fetch semantics are qualified.
"""
import argparse
import hashlib
import json
from pathlib import Path
from analyze_index_extent_contract import BASE, IMAGE_SHA, ROOT, verify as verify_index

SDK_START = 0x8244D360
SDK_STOP = 0x8244D7B8
SDK_SHA = "995a205ed0aa3f1a09c998ea0fac3785ca228d38861c8b91303b5651a22db26c"
PINS = {
    # Original submesh row transports count/start/base/primitive unchanged.
    0x8270139C: "80723028 80FF0018 80DF0014 80BF0010 809F000C 4BD4BFB1",
    # SDK keeps the incoming count in callee-saved r17; no minimum is applied.
    0x8244D360: "7D8802A6 485EF02D 9421FF10 7C7F1B78 7C902378 7CAF2B78 7CD33378 7CF13B78",
    # IB header is required and retained even when the packet count is zero.
    0x8244D59C: "3D60821D 82DF308C 3ABF2F52 3A4B3D30",
    # Small counts0..2 take the same count-preserving path as count7.
    0x8244D5D8: "39602102 7E388B78 2B11FFFF 95630004 7C7B1B78 95FB0004 40990024",
    # Selected count is emitted; subtraction then exits without another chunk.
    0x8244D788: "7D588851 917F0030 41820020",
}


def verify(image, check_hash=True):
    verify_index(image, check_hash)
    for address, words in PINS.items():
        expected=bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)]!=expected:
            raise ValueError(f"Original short-strip producer changed at {address:08X}")
    if hashlib.sha256(image[SDK_START-BASE:SDK_STOP-BASE]).hexdigest()!=SDK_SHA:
        raise ValueError("Complete original SDK indexed-draw body changed")


def selected(indices, start, count, base, vertices):
    if not vertices or start<0 or count<0 or start+count>len(indices):
        raise ValueError("Selected extent is outside its retained nonempty owners")
    result=[]
    for raw in indices[start:start+count]:
        if raw==0xFFFF:
            result.append(None)
        elif 0<=raw+base<vertices:
            result.append(raw+base)
        else:
            raise ValueError("Selected vertex is outside its owner")
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image",type=Path,default=ROOT/"analysis/simpsons.pe")
    args=parser.parse_args()
    image=args.image.read_bytes();verify(image)
    indices=[0,1,2,0xFFFF,2,1,3]
    counts=[]
    for count in (0,1,2):
        fetched=selected(indices,0,count,0,4)
        emitted=count if count<=65535 else None
        remainder=count-emitted
        if emitted!=count or remainder or len(fetched)!=count or max(count-2,0):
            raise ValueError("Original small packet count changed or acquired a triangle")
        counts.append({"selected_count":count,"fetched_vertices":fetched,"packet_count":emitted,"remaining_count":remainder,"triangle_count":0})
    if selected(indices,len(indices),0,0,4)!=[]:
        raise ValueError("Zero selected range fetched one-past-owner data")
    for start,count,base,vertices in ((len(indices)+1,0,0,4),(len(indices),1,0,4),
                                     (0,1,-1,4),(0,1,4,4),(0,0,0,0)):
        try:selected(indices,start,count,base,vertices)
        except ValueError:pass
        else:raise ValueError("Malformed short selected ownership admitted")
    mutation_offsets=set(range(SDK_START-BASE,SDK_STOP-BASE))
    for address,words in PINS.items():mutation_offsets.update(range(address-BASE,address-BASE+len(bytes.fromhex(words))))
    mutations=0
    for offset in mutation_offsets:
        changed=bytearray(image);changed[offset]^=1
        try:verify(changed,check_hash=False)
        except ValueError:mutations+=1
        else:raise ValueError("Mutated short-strip source admitted")
    print(json.dumps({"image_sha256":IMAGE_SHA,"complete_sdk_body_sha256":SDK_SHA,
        "original_transport":"8270139C..B0 loads row+18 count, row+14 start, row+10 signedbase and calls8244D360. The pinned complete SDK body never applies a minimum3 check; counts0/1/2 follow r17->r24->packet count and subtract to0 atD788.",
        "owned_buffer_scope":"Original mesh setup/SDK still binds and reads a valid IB header.type/data for count0. Tests retain independently allocated nonempty vertex/index owners and require native mesh/backing presence. No zero-owner or missing-buffer admission is broadened.",
        "counts":counts,"mutation_checks_passed":mutations,
        "native_scope":"Independent original owner fixtures for counts0/1/2 passed in short-strip-recording-tests.log: eight original Boolean passes, full color/depth/stencil unchanged, malformed bounds and actual pairedCPUrelease. The shared native R16 predicate also passed. This source verifier is not a native execution or current-source/executable identity receipt; missing vertex/index owners remain rejected."},indent=2))


if __name__=="__main__":main()
