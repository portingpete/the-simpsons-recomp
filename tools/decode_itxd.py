#!/usr/bin/env python3
"""CPU-only base-level decode of the exact verified frontend_global.itxd sample.

Stdlib resource addressing, BC1/BC2 component decoding and deterministic PNG.
No command processor, GPU API, renderer, mip generation or alpha unpremultiply.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import stat
import struct
import sys
import zlib

sys.dont_write_bytecode = True
import inspect_itxd

SCHEMA = "simpsons_frontend_base_pixels_v1"
ROOT = Path(__file__).resolve().parents[1]
OUTPUT_ROOT = ROOT / "build/decoded-textures"
MAX_AXIS = 4096
COLOR_RULE = "RexGlue RGBA8 conversion: RGB565 bit replication; integer thirds/halves truncated"
REFERENCES = [
    {"path": "K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/util.cpp",
     "sha256": "c16fb6f11fbfe4327d8e648353b8f10f661d5f3b7520ef831cfc67ce1569b81d",
     "lines": "119-167, 290-330, 424-436", "use": "Base layout and tiled block byte-address equation."},
    {"path": "K:/Simpsons/RexGlueCurrent/include/rex/graphics/pipeline/texture/util.h",
     "sha256": "d370dded22778c3154eebe06ae2f4fcb7175e8a8e777012744c16391928db81b",
     "lines": "47-88, 210-233", "use": "Base versus packed mip storage; byte-block tiling caveats."},
    {"path": "K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/conversion.cpp",
     "sha256": "235adeffb14d0f53fb520e876cc886aabd6de631c287bb33ce821d45c29e69df",
     "lines": "88-134", "use": "Corroborating row/column decomposition of untile addresses."},
    {"path": "K:/Simpsons/RexGlueCurrent/src/graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt1_rgba8_cs.h",
     "sha256": "b50168196f25bcddc16a896702b33937fdc12a97d8ad13e036708620f5640025",
     "lines": "127-174, 227-280", "use": "Readable reference assembly: endian, RGB565 expansion and truncating BC1 interpolation; mathematical evidence only."},
    {"path": "K:/Simpsons/RexGlueCurrent/src/graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt3_rgba8_cs.h",
     "sha256": "bb4df185be6cd4654372724d3a9f2222256404f614e9050785a49bde0048b65b",
     "lines": "143-184", "use": "Readable reference assembly: BC2 four-color interpolation and independent explicit alpha; mathematical evidence only."},
    {"url": "https://learn.microsoft.com/en-us/windows/win32/direct3d9/opaque-and-1-bit-alpha-textures",
     "use": "RGB565 endpoints, selector order and BC1 modes; documented +1 rounding differs from the selected RexGlue arithmetic."},
    {"url": "https://learn.microsoft.com/en-us/windows/win32/direct3d9/textures-with-alpha-channels",
     "use": "BC2 explicit alpha order; four-color mode; identical DXT2/3 component decode."},
    {"url": "https://github.com/leeao/Noesis-Plugins/blob/master/Textures/inc_xbox360_untile.py",
     "use": "Inverse address-to-coordinate equations used by independent-direction test fixtures."},
]
LIMITATIONS = [
    "Only the exact SHA256-verified sample and base level zero are accepted by the dictionary decoder.",
    "RGB/A are decoded stored components. DXT2 versus DXT3 alpha association is not inferred or corrected.",
    "PNG stores these component bytes without gamma conversion, color profile, filtering or resampling.",
    "The documented integer RGB8 reconstruction is not a claim of Xbox GPU sampling/framebuffer bit identity.",
    "Non-base mips, packed base levels, other formats/endian modes/swizzles and runtime relocation are unsupported.",
]


class DecodeError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise DecodeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align32(value):
    return (value + 31) & ~31


def tiled_offset_2d(x, y, pitch_blocks, bytes_per_block):
    """Logical block coordinate -> resource-relative byte address (not a pixel index).

    Algebraic Xenos 2D address permutation, corroborated by RexGlue util.cpp
    424-436. Restricted to the three block sizes actually needed here.
    """
    require(all(type(v) is int for v in (x, y, pitch_blocks, bytes_per_block)), "integer layout fields required")
    require(bytes_per_block in (1, 8, 16), "unsupported block byte size")
    require(0 < pitch_blocks <= MAX_AXIS and pitch_blocks % 32 == 0,
            "pitch must be positive and aligned to 32 blocks")
    require(0 <= x < pitch_blocks and 0 <= y < MAX_AXIS, "block coordinate out of range")
    log = bytes_per_block.bit_length() - 1
    macro = ((x >> 5) + (y >> 5) * (pitch_blocks >> 5)) << (log + 7)
    micro = ((x & 7) + ((y & 14) << 2)) << log
    mixed = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4)
    return (((mixed & ~511) << 3) + ((y & 16) << 7) + ((mixed & 448) << 2)
            + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (mixed & 63))


def untile_blocks(storage, width_blocks, height_blocks, pitch_blocks, bytes_per_block, endian):
    """Read only addressed blocks; padding never becomes output pixels."""
    require(all(type(v) is int for v in (width_blocks, height_blocks, pitch_blocks, bytes_per_block)),
            "integer layout fields required")
    require(0 < width_blocks <= pitch_blocks <= MAX_AXIS and 0 < height_blocks <= MAX_AXIS,
            "invalid block rectangle")
    require((bytes_per_block, endian) in ((1, "none"), (8, "8in16"), (16, "8in16")),
            "unsupported block size/endian combination")
    require(width_blocks * height_blocks * bytes_per_block <= 16 * 1024 * 1024,
            "surface exceeds decoder budget")
    linear, addresses = bytearray(), set()
    max_end = 0
    for y in range(height_blocks):
        for x in range(width_blocks):
            offset = tiled_offset_2d(x, y, pitch_blocks, bytes_per_block)
            require(offset % bytes_per_block == 0 and offset not in addresses,
                    "unaligned or duplicate tiled block address")
            require(offset + bytes_per_block <= len(storage), "tiled block outside base allocation")
            addresses.add(offset)
            max_end = max(max_end, offset + bytes_per_block)
            block = storage[offset:offset + bytes_per_block]
            # All supported blocks start on an even byte boundary. Endian mode
            # 8in16 exchanges adjacent byte lanes before little-endian BC reads.
            linear.extend(block if endian == "none" else bytes(block[i ^ 1] for i in range(bytes_per_block)))
    return bytes(linear), {"addressed_blocks": len(addresses), "addressed_bytes": len(linear),
                           "maximum_addressed_end": max_end, "addresses_unique": True}


def rgb565(value):
    r, g, b = value >> 11, (value >> 5) & 63, value & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def decode_bc_block(block, format_id):
    """Return sixteen RGBA tuples in top-left-first row order from LE BC bytes."""
    require(format_id in (18, 19), "unsupported BC format")
    require(len(block) == (8 if format_id == 18 else 16), "truncated or oversized BC block")
    c0, c1, indices = struct.unpack("<HHI", block[-8:])
    a, b = rgb565(c0), rgb565(c1)
    palette = [(*a, 255), (*b, 255)]
    if c0 > c1 or format_id == 19:
        palette += [tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                    tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
    else:
        palette += [tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
    alpha = int.from_bytes(block[:8], "little") if format_id == 19 else None
    return [palette[(indices >> (2 * i)) & 3] if alpha is None else
            (*palette[(indices >> (2 * i)) & 3][:3], ((alpha >> (4 * i)) & 15) * 17)
            for i in range(16)]


def decode_linear(linear, width, height, format_id):
    require(type(width) is int and type(height) is int and 0 < width <= MAX_AXIS
            and 0 < height <= MAX_AXIS and width * height <= 1024 * 1024, "invalid pixel dimensions/budget")
    require(format_id in (2, 18, 19), "unsupported texture format")
    if format_id == 2:
        require(len(linear) == width * height, "L8 byte count mismatch")
        return bytes(channel for value in linear for channel in (value, value, value, 255))
    size = 8 if format_id == 18 else 16
    bw, bh = (width + 3) // 4, (height + 3) // 4
    require(len(linear) == bw * bh * size, "BC byte count mismatch")
    rgba = bytearray(width * height * 4)
    for by in range(bh):
        for bx in range(bw):
            start = (by * bw + bx) * size
            pixels = decode_bc_block(linear[start:start + size], format_id)
            for y in range(min(4, height - by * 4)):
                for x in range(min(4, width - bx * 4)):
                    out = ((by * 4 + y) * width + bx * 4 + x) * 4
                    rgba[out:out + 4] = bytes(pixels[y * 4 + x])
    return bytes(rgba)


def decode_base(data, texture, mip=0):
    require(mip == 0, "only base level zero is verified")
    d, p = texture["descriptor"], texture["payload"]
    require(d["type"] == "texture" and d["dimension"] == "2D" and d["tiled"],
            "only tiled 2D resources are verified")
    fmt, width, height = d["format"]["id"], d["width"], d["height"]
    require(fmt in (2, 18, 19), "unsupported texture format")
    require(d["channel_swizzle"] == (list("XXX1") if fmt == 2 else list("XYZW")),
            "unsupported channel swizzle")
    require(d["component_signs"] == ["unsigned"] * 4 and d["numeric_format_bit"] == 0,
            "unsupported numeric format")
    require(d["base_address_field_bytes"] == 0 and d["mip_min_level"] == 0,
            "nonzero base address or mip minimum is outside the original resource profile")
    require(0 < width <= 256 and 0 < height <= 256 and not width & (width - 1)
            and not height & (height - 1), "base dimensions outside verified profile")
    require(not d["packed_mips"] or min(width, height) > 16, "packed base-level layout is unverified")
    block, size = (1, 1) if fmt == 2 else (4, 8 if fmt == 18 else 16)
    pitch_texels = d["base_pitch_texels"]
    require(pitch_texels >= width and pitch_texels % (32 * block) == 0, "invalid base pitch")
    bw, bh, pitch = (width + block - 1) // block, (height + block - 1) // block, pitch_texels // block
    base_size = pitch * align32(bh) * size
    require(0 <= p["offset"] and base_size <= p["size"] and p["offset"] + p["size"] <= len(data),
            "base allocation outside resource")
    require(d["mip_address_field_bytes"] == (base_size if d["packed_mips"] else 0),
            "mip-address field disagrees with verified base allocation boundary")
    storage = data[p["offset"]:p["offset"] + base_size]
    linear, addressing = untile_blocks(storage, bw, bh, pitch, size, d["endianness"])
    rgba = decode_linear(linear, width, height, fmt)
    counts = Counter(rgba[i + 3] for i in range(0, len(rgba), 4))
    return rgba, {
        "name": texture["name"], "index": texture["index"], "mip_level": 0,
        "width": width, "height": height, "format_id": fmt,
        "format_name": {2: "L8", 18: "BC1_DXT1", 19: "BC2_DXT2_3"}[fmt],
        "descriptor_words_be_hex": d["words_be_hex"], "record_offset": texture["record_offset"],
        "resource_payload_offset": p["offset"], "resource_payload_size": p["size"],
        "resource_payload_sha256": p["sha256"], "base_allocation_prefix_bytes": base_size,
        "base_pitch_texels": pitch_texels, "base_pitch_blocks": pitch,
        "logical_blocks": [bw, bh], "block_bytes": size, "tiled": True,
        "packed_base": False, "block_origin": [0, 0], "endianness": d["endianness"],
        "channel_swizzle": d["channel_swizzle"], "addressing": addressing,
        "linear_base_sha256": digest(linear), "rgba_sha256": digest(rgba), "rgba_bytes": len(rgba),
        "alpha_histogram": {str(k): v for k, v in sorted(counts.items())},
        "alpha_association": "unspecified_stored_components" if fmt == 19 else "opaque_in_this_sample",
        "rgb_not_unpremultiplied": True,
    }


def png_rgba(width, height, rgba):
    """Deterministic RGBA8 PNG, filter 0 and stored DEFLATE blocks.

    No compression-version-dependent heuristics, gamma tags or pixel alteration.
    """
    require(0 < width <= MAX_AXIS and 0 < height <= MAX_AXIS and len(rgba) == width * height * 4,
            "invalid PNG dimensions/byte count")
    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    compressed = bytearray(b"\x78\x01")
    for start in range(0, len(raw), 65535):
        part = raw[start:start + 65535]
        compressed += bytes([int(start + len(part) == len(raw))])
        compressed += struct.pack("<HH", len(part), len(part) ^ 65535) + part
    compressed += struct.pack(">I", zlib.adler32(raw) & 0xffffffff)
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", bytes(compressed)) + chunk(b"IEND", b""))


def inspection_sheet(frames):
    """Separate display artifact; source PNG pixels are never composited or scaled."""
    require(len(frames) == 15, "inspection sheet requires fifteen frames")
    cell, tile, margin, columns = 272, 256, 8, 5
    width, height = columns * cell, 3 * cell
    preview = bytearray(bytes((40, 40, 40, 255)) * (width * height))
    placements = []
    for i, (name, w, h, rgba) in enumerate(frames):
        require(0 < w <= tile and 0 < h <= tile and tile % w == tile % h == 0
                and len(rgba) == w * h * 4, "invalid inspection frame")
        left, top = (i % columns) * cell + margin, (i // columns) * cell + margin
        for y in range(tile):
            for x in range(tile):
                src = ((y * h // tile) * w + x * w // tile) * 4
                alpha = rgba[src + 3]
                gray = 64 if (x // 16 + y // 16) & 1 else 112
                dst = ((top + y) * width + left + x) * 4
                preview[dst:dst + 4] = bytes((rgba[src + c] * alpha + gray * (255 - alpha) + 127) // 255
                                           for c in range(3)) + b"\xff"
        placements.append({"index": i, "source_png": name, "preview_box": [left, top, tile, tile]})
    return png_rgba(width, height, preview), {
        "width": width, "height": height, "placements": placements,
        "purpose": "inspection only; nearest-neighbor enlargement and synthetic checkerboard; not a source texture",
        "compositing": "conventional straight-alpha display assumption only; asset RGB/alpha association stays unmodified",
    }


def build_outputs(data, input_path, source_str=None, mip=0):
    require(mip == 0, "only base level zero is verified")
    inventory = inspect_itxd.parse_itxd(data)
    require(inventory["input"]["matches_verified_sample"], "only the exact verified ITXD SHA256 is supported")
    require(inventory["summary"]["texture_count"] == 15, "expected fifteen original textures")
    manifest = {
        "manifest_schema": SCHEMA, "input": {**inventory["input"], "path": input_path.resolve().as_posix()},
        "producer": {"path": Path(__file__).resolve().as_posix(), "sha256": digest(Path(__file__).read_bytes())},
        "inspector_sha256": digest(Path(inspect_itxd.__file__).read_bytes()),
        "original_resource": (inspect_itxd.verify_original(data, source_str) if source_str else
                              {"verified_byte_identical": False, "reason": "--source-str not supplied; exact sample SHA256 verified"}),
        "decode_contract": {"layout": "Xenos tiled 2D, logical compression-block coordinates, base origin (0,0)",
                            "color_rule": COLOR_RULE, "png": "RGBA8; filter 0; stored DEFLATE; no color/gamma tags",
                            "rgb_alpha": "stored components; no premultiply/unpremultiply or gamma conversion"},
        "reference_snapshots": REFERENCES + inventory["reference_snapshots"],
        "limitations": LIMITATIONS, "textures": [],
    }
    outputs, frames = {}, []
    for texture in inventory["textures"]:
        require(re.fullmatch(r"[A-Za-z0-9_]+", texture["name"]), "unsafe texture output name")
        rgba, entry = decode_base(data, texture)
        name = f'{entry["index"]:02d}_{entry["name"]}.base.png'
        png = png_rgba(entry["width"], entry["height"], rgba)
        entry.update({"png": name, "png_sha256": digest(png), "png_bytes": len(png)})
        outputs[name] = png
        manifest["textures"].append(entry)
        frames.append((name, entry["width"], entry["height"], rgba))
    manifest["summary"] = {"decoded_base_levels": len(outputs), "decoded_pixels": sum(
        t["width"] * t["height"] for t in manifest["textures"]),
        "formats": inventory["summary"]["format_counts"], "rejected_levels": "all nonzero levels"}
    preview, preview_info = inspection_sheet(frames)
    outputs["inspection-sheet.png"] = preview
    manifest["inspection_preview"] = {**preview_info, "png": "inspection-sheet.png",
                                       "png_sha256": digest(preview), "png_bytes": len(preview)}
    outputs["manifest.json"] = (json.dumps(manifest, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    return outputs


def checked_output_dir(path):
    target = path.resolve()
    require(target == OUTPUT_ROOT.resolve() or OUTPUT_ROOT.resolve() in target.parents,
            "output directory must stay under build/decoded-textures")
    for component in (path.absolute(), *path.absolute().parents):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode) and not (
            getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)),
            "output path contains a link/reparse point")
    require(not target.exists() or target.is_dir(), "output path is not a directory")
    return target


def publish_outputs(directory, outputs, verify=False):
    """Complete all decoding before writing; existing files are never replaced."""
    target = checked_output_dir(directory)
    if verify:
        require(target.is_dir(), "verification output directory does not exist")
        for name, content in outputs.items():
            p = target / name
            require(p.is_file() and not p.is_symlink() and p.read_bytes() == content,
                    f"output verification mismatch: {name}")
        return
    require(not target.exists() or not any(target.iterdir()), "output directory must be empty; use --verify for replay")
    target.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        with (target / name).open("xb") as stream:
            stream.write(content)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--source-str", type=Path, help="read-only original-resource equality verification")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT_ROOT)
    parser.add_argument("--mip", type=int, default=0, help="only 0 is supported")
    parser.add_argument("--verify", action="store_true", help="decode again and compare existing outputs without writes")
    args = parser.parse_args(argv)
    try:
        checked_output_dir(args.output_dir)
        require(args.input.stat().st_size <= inspect_itxd.MAX_INPUT_BYTES, "input exceeds decoder budget")
        outputs = build_outputs(args.input.read_bytes(), args.input, args.source_str, args.mip)
        publish_outputs(args.output_dir, outputs, args.verify)
    except (OSError, ValueError) as error:
        print(f"decode_itxd: {error}", file=sys.stderr)
        return 2
    print(f'{"Verified" if args.verify else "Wrote"} 15 base-level PNGs, inspection-sheet.png and manifest.json in {args.output_dir}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
