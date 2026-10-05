"""Verify original morph stream selection, fetch dirty groups and resource access."""
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
SPANS=((0x826FF3B8,0xD8,'7b8edf4e3effcf18d3aba2df21a20d1fca6adbd2614a2d4eb79d53754d4c8c6e'),
       (0x8243C5C0,0x70,'5aa9911dfa1d6a87c1547c0478a93abd9584173e18e68014422674b1b6067da6'))

def verify(image):
    for address,size,digest in SPANS:
        if hashlib.sha256(image[address-0x82000000:address-0x82000000+size]).hexdigest()!=digest:
            raise ValueError('Original morph stream CPU span changed')
    def words(address,n):return struct.unpack_from('>'+str(n)+'I',image,address-0x82000000)
    if words(0x826FF41C,7)!=(0x217E005F,0x1D6B5556,0x556B843E,0x396B0020,0x796B0020,0x7FA85C36,0x4BD3D18D):
        raise ValueError('Original64-bit morph dirty mask call differs')
    if words(0x8243C624,3)!=(0xE97F0018,0x7D6B4378,0xF97F0018):
        raise ValueError('Stream-source final argument no longer updates the fetch dirty mask')
    # The original computes bit groups for fetch95-stream. Six stream slots
    # map to the first three groups; r8 does not change the float3 data format.
    masks=[(1<<63)>>((((95-stream)*21846)>>16)+32) for stream in range(1,7)]
    if masks!=[1,1,2,2,2,4]:raise ValueError('Original fetch dirty groups differ')
    return dict(spans=SPANS,streams=list(range(1,7)),stride=12,dirty_masks=masks,
                source='r24+8 -> table[r25] -> selected header[r10]; data header+18,extent header+1C',
                native='Own selected float3 snapshots and interleave into the corresponding six morph attributes.',
                sdk_context_emulated=False)

def main():
    image=(ROOT/'analysis/simpsons.pe').read_bytes();report=verify(image)
    for at in (0x6FF3CC,0x6FF418,0x6FF430,0x43C628):
        changed=bytearray(image);changed[at]^=1
        try:verify(changed)
        except ValueError:pass
        else:raise RuntimeError('Changed morph stream contract accepted')
    report['mutation_checks_passed']=4
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
