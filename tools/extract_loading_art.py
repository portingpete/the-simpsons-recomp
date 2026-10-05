"""Extract and component-decode the two original embedded loading textures.

Offline assets only: this neither renders a game frame nor executes GPU commands.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

sys.dont_write_bytecode = True
from decode_itxd import decode_bc_block, png_rgba

ROOT = Path(__file__).resolve().parents[1]
IMAGE_HASH = '6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0'
STREAM_HASH = '74ddb211e9ebf1025dd39aa5632ce2cfbabdb67fffa1eecfd6be5ca4a5d364dd'
STREAM_OFFSET, STREAM_SIZE, STAMP = 0x15f820, 0x20150, 0x1c02002d


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def chunks(data, start, end):
    require(0 <= start <= end <= len(data), 'Invalid chunk bounds')
    while start < end:
        require(start + 12 <= end, 'Truncated chunk header')
        kind, size, stamp = struct.unpack_from('<III', data, start)
        require(stamp == STAMP, 'Unsupported chunk version')
        stop = start + 12 + size
        require(stop <= end, 'Chunk escapes its parent')
        yield kind, start + 12, stop
        start = stop


def parse_dictionary(data):
    root = list(chunks(data, 0, len(data)))
    require(len(root) == 1 and root[0][0] == 0x16, 'Expected one texture dictionary')
    children = list(chunks(data, root[0][1], root[0][2]))
    require([c[0] for c in children] == [1, 0x15, 0x15, 3], 'Unsupported dictionary structure')
    _, begin, end = children[0]
    require(data[begin:end] == b'\x02\x00\x09\x00', 'Unsupported texture count/platform')
    require(children[-1][1] == children[-1][2], 'Unknown dictionary extension')
    textures = []
    for _, begin, end in children[1:3]:
        native = list(chunks(data, begin, end))
        require([c[0] for c in native] == [1, 3], 'Unsupported native texture structure')
        _, header, stop = native[0]
        require(stop - header >= 92, 'Truncated native texture header')
        platform, sampler, name, mask, raster, fmt, width, height, depth, levels, kind, flags = struct.unpack_from('>II32s32sIIHHBBBB', data, header)
        require(platform == 9 and fmt == 0x1a200154, 'Unsupported native format')
        require((width, height, depth, levels, kind, flags) == (256, 256, 16, 1, 4, 9), 'Unsupported raster layout')
        require(sampler == 0x1102 and raster == 0x300 and mask == bytes(32), 'Unsupported raster/sampler profile')
        require(name in (b'frame1' + bytes(26), b'frame2' + bytes(26)), 'Unexpected loading texture name')
        name = name.rstrip(b'\0').decode('ascii')
        size, = struct.unpack_from('<I', data, header + 88)
        require(size == 65536 and header + 92 + size == stop, 'Native level byte count mismatch')
        _, ext_begin, ext_end = native[1]
        extension = list(chunks(data, ext_begin, ext_end))
        require(len(extension) == 1 and extension[0][0] == 0xea2f and extension[0][2] - extension[0][1] == 8, 'Unsupported native extension')
        textures.append(dict(name=name, width=width, height=height, platform=platform,
                             format=f'0x{fmt:08x}', sampler=f'0x{sampler:08x}',
                             raster_format=f'0x{raster:08x}', depth_field=depth,
                             mip_count=levels, type_field=kind, flags_field=flags,
                             header_offset=header, payload_offset=header+92,
                             extension_hex=data[ext_begin:ext_end].hex(),
                             payload=data[header+92:stop]))
    require({t['name'] for t in textures} == {'frame1', 'frame2'}, 'Duplicate loading texture')
    return textures


def bc3_block(block):
    require(len(block) == 16, 'BC3 block must be 16 bytes')
    a, b = block[:2]
    alphas = [a, b]
    # Match the inspected RexGlue RGBA8 conversion's integer division, without
    # the rounding bias shown in Microsoft's illustrative BC3 pseudocode.
    if a > b:
        alphas += [((7-i)*a + i*b)//7 for i in range(1, 7)]
    else:
        alphas += [((5-i)*a + i*b)//5 for i in range(1, 5)] + [0, 255]
    selectors = int.from_bytes(block[2:8], 'little')
    colors = decode_bc_block(bytes(8) + block[8:], 19)  # Always four-color mode.
    return [(*colors[i][:3], alphas[(selectors >> (i*3)) & 7]) for i in range(16)]


def decode_texture(texture):
    require((texture['width'], texture['height']) == (256, 256), 'Unsupported base dimensions')
    storage = texture['payload']
    require(len(storage) == 65536, 'Unexpected serialized base size')
    # Serialized RenderWare native texture data is linear block rows. The
    # native D3DFORMAT describes runtime storage, not this stream byte ordering.
    linear = bytes(storage[i ^ 1] for i in range(len(storage)))
    addressing = dict(block_rows=64, blocks_per_row=64, block_bytes=16,
                      serialized_layout='linear block rows', endian='8in16')
    rgba = bytearray(256 * 256 * 4)
    for by in range(64):
        for bx in range(64):
            offset = (by*64+bx)*16
            pixels = bc3_block(linear[offset:offset+16])
            for y in range(4):
                for x in range(4):
                    out = ((by*4+y)*256+bx*4+x)*4
                    rgba[out:out+4] = bytes(pixels[y*4+x])
    return bytes(rgba), linear, addressing


def artifacts(image):
    require(len(image) == 15466496 and digest(image) == IMAGE_HASH, 'Original image identity mismatch')
    stream = image[STREAM_OFFSET:STREAM_OFFSET+STREAM_SIZE]
    require(digest(stream) == STREAM_HASH, 'Embedded stream identity mismatch')
    output = {'loading.txd': stream}
    report = dict(schema='simpsons_embedded_loading_assets_v1', image_sha256=IMAGE_HASH,
                  stream_va='0x8215f820', stream_size=STREAM_SIZE, stream_sha256=STREAM_HASH,
                  provenance='Original loader 0x82862A28 selects this stream and names frame1/frame2.',
                  decoder_sha256=digest(Path(__file__).read_bytes()),
                  shared_decoder_sha256=digest((ROOT/'tools/decode_itxd.py').read_bytes()),
                  layout='Serialized linear BC3/DXT4_5, 8in16 endian, 64x64 blocks, RGBA channels.',
                  limits=['Component reconstruction, not a captured game frame.',
                          'Linear serialization is established by structural/visual decoding; original stream-loader code corroboration remains pending.',
                          'The native D3DFORMAT is tiled; stream storage is distinct from runtime relocation/storage.',
                          'Integer RGB/alpha interpolation matches inspected reference conversion; exact console filtering/rounding is unverified.',
                          'Stored RGB and alpha are preserved without inferring premultiplication or changing gamma.'], textures=[])
    for texture in parse_dictionary(stream):
        rgba, linear, addressing = decode_texture(texture)
        filename = texture['name'] + '.png'
        output[filename] = png_rgba(256, 256, rgba)
        row = {k: v for k, v in texture.items() if k != 'payload'}
        row.update(payload_sha256=digest(texture['payload']), linear_sha256=digest(linear),
                   rgba_sha256=digest(rgba), png=filename, png_sha256=digest(output[filename]), addressing=addressing)
        report['textures'].append(row)
    output['manifest.json'] = (json.dumps(report, indent=2, sort_keys=True)+'\n').encode()
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    output = ROOT/'build/loading-art'
    files = artifacts((ROOT/'analysis/simpsons.pe').read_bytes())
    if args.verify:
        for name, data in files.items():
            require((output/name).read_bytes() == data, f'Reproduction differs: {name}')
        print(f'Verified {len(files)} original loading artifacts byte-for-byte')
    else:
        require(not output.exists(), 'Output already exists; use --verify')
        output.mkdir(parents=True)
        for name, data in files.items():
            with (output/name).open('xb') as target:
                target.write(data)
        print(f'Extracted two original loading textures and stream to {output}')


if __name__ == '__main__':
    main()
