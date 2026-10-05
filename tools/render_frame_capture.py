"""Encode native RGB10A2 presented-frame readbacks as RGB16 PNG previews.

The raw capture remains authoritative. RGB codes map linearly onto 16-bit PNG
codes; the display ignores alpha, so the preview omits alpha too. No brightness,
gamma, crop, resampling, denoising, overlays or invented pixels are applied.
"""
from pathlib import Path
import argparse, hashlib, json, re, struct, zlib
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--frame', help='Render one exact native-frame-N or native-movie-target-N stem')
    a=p.parse_args()
    if not a.directory.is_dir():
        raise SystemExit(f"capture directory not found: {a.directory}")
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raws=sorted([*a.directory.glob('native-frame-*.rgb10a2'),*a.directory.glob('native-movie-target-*.rgb10a2')])
    if a.frame:
        if not re.fullmatch(r'native-(?:frame|movie-target)-\d+', a.frame):
            raise SystemExit('Invalid exact native capture stem')
        raws=[raw for raw in raws if raw.stem==a.frame]
    if not raws:
        raise SystemExit(f"no captures found in {a.directory}")
    for raw in raws:
        try:
            meta=json.loads(raw.with_suffix('.json').read_text(encoding="utf-8"));data=raw.read_bytes();w,h=meta['width'],meta['height']
        except (OSError, ValueError, KeyError) as exc:
            raise SystemExit(f"Unreadable capture {raw}: {exc}")
        if meta.get('format')!='R10G10B10A2_UNORM_LE' or len(data)!=w*h*4:raise ValueError('Invalid native capture')
        if not 1<=w<=4096 or not 1<=h<=4096:raise ValueError('Invalid capture dimensions')
        rows=bytearray();nonblack=0;max_rgb=0
        for y in range(h):
            rows.append(0)
            for (word,) in struct.iter_unpack('<I',data[y*w*4:(y+1)*w*4]):
                rgb=[(word>>shift)&1023 for shift in (0,10,20)]
                nonblack+=any(rgb);max_rgb=max(max_rgb,*rgb)
                rows.extend(struct.pack('>HHH',*((v*65535+511)//1023 for v in rgb)))
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,16,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b'')
        out=raw.with_suffix('.png');out.write_bytes(png)
        report={'raw_sha256':hashlib.sha256(data).hexdigest(),'png_sha256':hashlib.sha256(png).hexdigest(),
                'nonblack_pixels':nonblack,'maximum_rgb_code':max_rgb,'conversion':'RGB10 codes linearly mapped to RGB16; display-ignored alpha omitted'}
        raw.with_suffix('.preview.json').write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8");print(out,report)


if __name__=='__main__':
    main()
