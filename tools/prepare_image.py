"""Derive an identified analysis image without modifying the player's files."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
GAME_SHA = '71d99dad06be1b512fc3058123b84fdad71339205a7e9249058ac5e34a82a231'

def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def inspect_image(image: bytes, xex: bytes, exports_root: Path):
    if image[:2] != b'MZ' or xex[:4] != b'XEX2':
        raise ValueError('Expected mapped PE and XEX2')
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    if image[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, count, timestamp, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', image, pe+4)
    optional = pe+24
    magic = struct.unpack_from('<H', image, optional)[0]
    if machine != 0x1f2 or magic != 0x10b:
        raise ValueError(f'Unexpected PE architecture {machine:x}/{magic:x}')
    base = struct.unpack_from('<I', image, optional+28)[0]
    entry = base+struct.unpack_from('<I', image, optional+16)[0]
    pe_image_size = struct.unpack_from('<I', image, optional+56)[0]
    security_offset = struct.unpack_from('>I', xex, 16)[0]
    image_size = struct.unpack_from('>I', xex, security_offset+4)[0]
    if len(image) != image_size:
        raise ValueError('XexTool must produce the complete flat mapped image')
    sections = []
    for n in range(count):
        pos = optional+optional_size+n*40
        name, size, rva, raw_size, raw_offset = struct.unpack_from('<8sIIII', image, pos)
        flags = struct.unpack_from('<I', image, pos+36)[0]
        if rva+size > len(image) and flags & 0x2000000 == 0:
            raise ValueError('Section exceeds mapped image')
        sections.append(dict(name=name.rstrip(b'\0').decode('ascii'), address=base+rva,
                             size=size, raw_size=raw_size, raw_offset=raw_offset, flags=flags))
    pdata = next(s for s in sections if s['name']=='.pdata')
    # Xbox PE .pdata uses 8-byte IMAGE_CE_RUNTIME_FUNCTION_ENTRY records.
    functions = []
    for pos in range(pdata['address']-base, pdata['address']-base+pdata['size']-7, 8):
        address, packed = struct.unpack_from('>II', image, pos)
        if address == packed == 0:
            continue
        length = ((packed >> 8) & 0x3fffff)*4
        if not base <= address < base+len(image) or length == 0 or address+length > base+len(image):
            raise ValueError(f'Invalid .pdata record at {base+pos:08x}')
        functions.append(dict(address=address, size=length, prologue_bytes=(packed & 0xff)*4,
                              has_exception_handler=bool(packed & 0x80000000), packed=packed))
    helpers = {}
    for name, pattern in {
        'savegprlr_14': 'f9c1ff68f9e1ff70fa01ff78',
        'restgprlr_14': 'e9c1ff68e9e1ff70ea01ff78',
        'savefpr_14': 'd9ccff70d9ecff78da0cff80',
        'restfpr_14': 'c9ccff70c9ecff78ca0cff80',
        'savevmx_14': '3960fee07dcb61ce',
        'restvmx_14': '3960fee07dcb60ce',
        'savevmx_64': '3960fc00100b61cb',
        'restvmx_64': '3960fc00100b60cb',
    }.items():
        matches = [base+m.start() for m in re.finditer(re.escape(bytes.fromhex(pattern)), image)]
        helpers[name] = matches
    option_count = struct.unpack_from('>I', xex, 20)[0]
    options = dict(struct.iter_unpack('>II', xex[24:24+option_count*8]))
    imp = options[0x103ff]
    imp_size, strings_size, libraries_count = struct.unpack_from('>III', xex, imp)
    libnames = [s.decode('ascii') for s in xex[imp+12:imp+12+strings_size].split(b'\0') if s]
    pos = imp+12+strings_size
    imports = []
    for _ in range(libraries_count):
        size = struct.unpack_from('>I', xex, pos)[0]
        name_index, entries = struct.unpack_from('>HH', xex, pos+36)
        if pos+size > imp+imp_size or size < 40+entries*4 or name_index>=len(libnames):
            raise ValueError('Invalid XEX import library bounds')
        library = libnames[name_index]
        inc = (exports_root/(library.split('.')[0]+'_table.inc')).read_text(encoding="utf-8")
        names = {int(a,16):b for a,b in re.findall(r'XE_EXPORT\([^,]+,\s*0x([0-9a-fA-F]+),\s*(\w+)', inc)}
        for j in range(entries):
            address = struct.unpack_from('>I', xex, pos+40+j*4)[0]
            if not base <= address <= base+len(image)-4:
                raise ValueError('Import descriptor outside image')
            word = struct.unpack_from('>I', image, address-base)[0]
            ordinal = word & 0xffff
            kind = word >> 24
            if kind not in (0,1):
                raise ValueError(f'Unsupported import descriptor kind {kind}')
            imports.append(dict(library=library, address=address, ordinal=ordinal,
                                kind=kind, name=names.get(ordinal, f'ordinal_{ordinal:04x}')))
        pos += size
    cv = []
    for m in re.finditer(b'RSDS', image):
        pos = m.start()
        end = image.find(b'\0', pos+24)
        if 0 <= end-pos < 2048:
            cv.append(dict(guid=str(uuid.UUID(bytes_le=image[pos+4:pos+20])),
                           age=struct.unpack_from('<I', image, pos+20)[0],
                           path=image[pos+24:end].decode('ascii', errors='replace')))
    strings = []
    pattern = re.compile(rb'[\x20-\x7e]{8,}')
    terms = re.compile(r'renderware|rw::|rage|shader|\.hlsl|\.fx\b|\.cpp\b|\.pdb\b|RenderMesh|RenderWare|rw[A-Z]')
    for m in pattern.finditer(image):
        s = m.group().decode('ascii')
        if terms.search(s):
            strings.append(dict(address=base+m.start(), text=s))
    tls=dict(zip(('slot_count','raw_data_address','data_size','raw_data_size'),
                 struct.unpack_from('>4I',xex,options[0x20104])))
    if tls['raw_data_size']>tls['data_size'] or tls['raw_data_address']<base or tls['raw_data_address']+tls['raw_data_size']>base+len(image):
        raise ValueError('Invalid static TLS data range')
    return dict(machine=machine, image_base=base, entry_point=entry, image_size=image_size,
                tls=tls,stack_size=options[0x20200],
                pe_image_size=pe_image_size,
                timestamp=timestamp, sections=sections, pdata_count=len(functions),
                functions=functions, imports=imports, helpers=helpers, codeview=cv,
                engine_strings=strings)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-dir', type=Path, default=ROOT/'Simpsons Game, The (USA)')
    p.add_argument('--xextool', type=Path, default=Path('K:/XexTool_v6.3/xextool.exe'))
    p.add_argument('--exports', type=Path, default=ROOT/'third_party/XenonRecomp/XenonUtils/xbox')
    a=p.parse_args()
    original=a.game_dir.resolve()/'default.xex'
    if digest(original)!=GAME_SHA:
        raise ValueError('Unsupported game executable: hash differs from the identified US retail build')
    out=ROOT/'analysis'
    out.mkdir(exist_ok=True)
    # Always provide -o: XexTool otherwise changes the input for transformation flags.
    result=subprocess.run([str(a.xextool), '-b',str(out/'simpsons.pe'),'-i',str(out/'simpsons.idc'),
                          '-e','u','-c','b','-o',str(out/'simpsons.unencrypted.xex'),str(original)],
                          capture_output=True,text=True,check=True, timeout=60)
    if digest(original)!=GAME_SHA:
        raise RuntimeError('Original input changed during derivation')
    derived=(out/'simpsons.unencrypted.xex').read_bytes()
    options=dict(struct.iter_unpack('>II',derived[24:24+struct.unpack_from('>I',derived,20)[0]*8]))
    fmt=options[0x3ff]
    size,encryption,compression=struct.unpack_from('>IHH',derived,fmt)
    if encryption != 0 or compression != 1 or size < 16 or (size-8)%8:
        raise RuntimeError('Expected normalized unencrypted basic-compression XEX')
    cursor=struct.unpack_from('>I',derived,8)[0]
    mapped=bytearray()
    for pos in range(fmt+8,fmt+size,8):
        data_size,zero_size=struct.unpack_from('>II',derived,pos)
        if cursor+data_size>len(derived): raise RuntimeError('Truncated basic XEX block')
        mapped.extend(derived[cursor:cursor+data_size])
        mapped.extend(bytes(zero_size))
        cursor+=data_size
    if mapped!=(out/'simpsons.pe').read_bytes():
        raise RuntimeError('Generator input does not reproduce the independently extracted mapped PE')
    report=inspect_image((out/'simpsons.pe').read_bytes(),(out/'simpsons.unencrypted.xex').read_bytes(),a.exports)
    report['input_sha256']=GAME_SHA
    report['image_sha256']=digest(out/'simpsons.pe')
    report['derived_xex_sha256']=digest(out/'simpsons.unencrypted.xex')
    report['xextool_sha256']=digest(a.xextool)
    (out/'executable.json').write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k not in ('functions','imports','engine_strings')},indent=2))
    print(f'Import descriptors: {len(report["imports"])}; relevant strings: {len(report["engine_strings"])}')

if __name__=='__main__':
    main()
