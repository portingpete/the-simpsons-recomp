"""Extract the unchanged authored menu MUS descriptor for original owner tests."""
import sys
sys.dont_write_bytecode = True
import hashlib
import json
from pathlib import Path
import struct
from extract_resource import checked_path, select_payload, write_payload

ROOT = Path(__file__).resolve().parents[1]

def main():
    asset_root = checked_path(ROOT / 'Simpsons Game, The (USA)')
    source = checked_path(asset_root / 'frontend/frontend.str')
    payload, receipt = select_payload(source, 30, 'menu_mus.msx', asset_root)
    if (receipt['source']['sha256'] != '8d530622a0bad36a92c9ced3e278f3841ec5504980d893c6b8d523204cccd204'
            or len(payload) != 4766
            or hashlib.sha256(payload).hexdigest() != '3fa1abe6b4a8eac02d44191e42900fc672b85cbfe1619e8f128d05c8c13944e9'):
        raise ValueError('Original menu MUS descriptor source changed')
    base = 0x50
    if payload[base:base+6] != b'PFDx\x05\x03' or payload[base+13] != 1:
        raise ValueError('Original menu MUS track owner changed')
    table = struct.unpack_from('>I', payload, base+0x2C)[0]
    descriptor = 4*struct.unpack_from('>I', payload, base+table)[0]
    extent = struct.unpack_from('>I', payload, base+descriptor+12)[0]
    if table != 0xDBC or descriptor != 0xDC0 or extent != 0x750:
        raise ValueError('Original menu MUS metadata extent changed')
    output = ROOT / 'build/restrictive-audit/menu_mus.msx'
    write_payload(output, payload, asset_root, (source, Path(__file__), ROOT/'tools/inspect_assets.py'))
    receipt.update(output=str(output), descriptor_body_offset=base,
                   descriptor_track_table_offset=table,
                   descriptor_track_offset=descriptor, metadata_bytes=extent)
    (ROOT/'build/restrictive-audit/menu_mus-msx-extraction.json').write_text(
        json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print('Prepared exact frontend menu_mus.msx descriptor: original 1872-byte metadata extent; no original writes')

if __name__ == '__main__':
    main()
