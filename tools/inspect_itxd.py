#!/usr/bin/env python3
"""Inspect the observed frontend_global ITXD profile; never decode/render pixels.

Stdlib only. Offsets refer to the uncompressed resource payload, not the STR.
The profile is empirical, not a specification for all EARS_ITXD resources.
See docs/texture-format.md for evidence, reference snapshots and limitations.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import sys

sys.dont_write_bytecode = True

SCHEMA = "simpsons_frontend_itxd_v1"
SAMPLE_SHA256 = "3e4909a83d0dc05698c43567aea90ae0838b5c21b03ecfc326d30431ad4c033f"
PREFIX = bytes.fromhex("757a0003000000010000ea2f005800200600000000000000")
HEADER_SIZE, RECORD_SIZE, SENTINEL = 0x30, 0x100, 0x18
MAX_INPUT_BYTES = 16 * 1024 * 1024  # Inspector budget, not a format limit.
FORMATS = {
    2: {"name": "k_8", "block_width": 1, "block_height": 1,
        "bytes_per_block": 1, "bits_per_texel": 8},
    18: {"name": "k_DXT1", "block_width": 4, "block_height": 4,
         "bytes_per_block": 8, "bits_per_texel": 4},
    19: {"name": "k_DXT2_3", "block_width": 4, "block_height": 4,
         "bytes_per_block": 16, "bits_per_texel": 8},
}
REFERENCES = [
    {"path": "K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h",
     "sha256": "7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227",
     "lines": "96-137, 192-197, 444-490, 1008-1017, 1093-1098, 1167-1267",
     "use": "Descriptor bitfields and enum names; read-only reference snapshot."},
    {"path": "K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/info_formats.cpp",
     "sha256": "36438c5c337d41c62de6c50b4b57971b2d583eaa055f8eff261411de82c31e3c",
     "lines": "20-43", "use": "Block dimensions and bits per texel."},
    {"path": "K:/DarkRecomp/renderer/engine/texture_mip_layout.h",
     "sha256": "7574fec962b9a299b5aab009b83e5f6da8759a904a5aefe7dcd6bc7149db95f2",
     "lines": "9-27, 41-89",
     "use": "Corroborating resource+28 descriptor placement and layout caveats only; "
            "no layout implementation imported or ported."},
    {"path": "K:/DarkRecomp/refs/UnleashedRecomp/UnleashedRecomp/gpu/video.h",
     "sha256": "e0ae8a86ba7fef474732ec889d5027c0654baa5592d3e167efc3d32e92d7f970",
     "lines": "119-135", "use": "Exact scalar 0x28000102 is named D3DFMT_L8."},
    {"path": "K:/DarkRecomp/refs/UnleashedRecomp/UnleashedRecomp/gpu/video.cpp",
     "sha256": "82616eed7512bec74d06155ef26712dcf35edae4b4795bee55cfd82601b9b420",
     "lines": "3086-3088, 3127-3129", "use": "L8 reference maps to R8_UNORM with RRR1 selection."},
]
HYPOTHESES = [
    "Link-node anchors may be eight bytes after each native texture object's start; inspection windows do not establish C++ object boundaries or a second linked list.",
    "Record+0x48 resembles a RenderWare filter byte followed by U/V address nibbles; enum meanings are not proven.",
    "Record+0xbc resembles a packed D3DFORMAT: shifts 0/6/8/9/17/18 match format/endian/tiled/sign/number/swizzle; constant fields provide weak independent evidence.",
    "Mip-address fields may be allocation-relative offsets patched during loading; no runtime relocation code was verified.",
]
LIMITATIONS = [
    "Only the observed header/record profile is accepted; other valid ITXD variants may be rejected.",
    "Opaque words are preserved. Passing structural checks does not authenticate modified payload bytes.",
    "Tiled addressing/untile, endian conversion, block decompression and pixel correctness are untested and unimplemented.",
    "Channel swizzle is decoded from the descriptor; it is distinct from spatial tiling/swizzling.",
    "k_DXT2_3 does not distinguish DXT2 versus DXT3 alpha premultiplication; DXT1 alpha usage is unverified.",
    "Mip min/max are stored sampler limits, not proof of authored mip count; individual mip spans are not parsed.",
    "Zero base-address fields and small mip-address fields suggest relocation, but runtime fixups are unverified.",
    "Stored descriptor sampler fields need not be the final runtime sampler; engine sampler-word semantics remain hypotheses.",
]


class ITXDError(ValueError):
    """Malformed data or a variant outside the explicitly supported profile."""


def require(condition, message):
    if not condition:
        raise ITXDError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, offset):
    require(0 <= offset <= len(data) - 4, f"truncated word at 0x{offset:x}")
    return struct.unpack_from(">I", data, offset)[0]


def hx(value):
    return f"0x{value:08x}"


def descriptor(words):
    """Read six stored resource words; no command buffers, guest state or GPU API."""
    a, b, c, d, e, f = words
    fmt = b & 63
    require(fmt in FORMATS, f"unsupported texture format {fmt}")
    require(a & 3 == 2, "descriptor is not a texture")
    require((f >> 9) & 3 == 1 and not b & 0x400 and c >> 26 == 0,
            "only non-stacked 2D descriptors are supported")
    require(a >> 31 == 1, "linear texture is outside the observed tiled profile")
    width, height = (c & 8191) + 1, ((c >> 13) & 8191) + 1
    pitch = ((a >> 22) & 511) * 32
    block = FORMATS[fmt]["block_width"]
    require(pitch >= width and pitch % (32 * block) == 0,
            "invalid tiled base pitch for block width")
    swizzle = [(d >> (1 + 3 * i)) & 7 for i in range(4)]
    require(all(v <= 5 for v in swizzle), "invalid channel swizzle selector")
    lo, hi = (e >> 2) & 15, (e >> 6) & 15
    require(lo <= hi < max(width, height).bit_length(), "invalid mip min/max limits")
    # Explicit observed-profile guards: these are not universal hardware rules.
    require(a & 0x003fffff == 2, "unsupported sign/clamp/control descriptor fields")
    require(b in (0x02, 0x52, 0x53), "unsupported endian/control/base-address fields")
    require(d in (0x0d10, 0x1400), "unsupported swizzle/sampler/control fields")
    require(e & ~0x3c0 == 0, "unsupported mip minimum/LOD/control fields")
    require(f & 0x7ff == 0x200, "unsupported dimension/border/control fields")
    packed, mip_address = bool(f & 0x800), f & 0xfffff000
    require(packed == bool(hi) and bool(mip_address) == bool(hi),
            "packed-mip/address fields disagree with the observed mip profile")
    expected_swizzle = [0, 0, 0, 5] if fmt == 2 else [0, 1, 2, 3]
    require(swizzle == expected_swizzle, "channel swizzle disagrees with format profile")
    return {
        "evidence": "reference_bitfields_cross_checked_with_repeated_resource_fields",
        "words_be_hex": [hx(v) for v in words],
        "type": "texture", "dimension": "2D", "width": width, "height": height,
        "format": {"id": fmt, **FORMATS[fmt]},
        "tiled": True, "base_pitch_texels": pitch,
        "base_pitch_blocks": pitch // block,
        "base_row_bytes_before_tiling": pitch // block * FORMATS[fmt]["bytes_per_block"],
        "endianness": "none" if fmt == 2 else "8in16",
        "channel_swizzle": ["XYZW01"[v] for v in swizzle],
        "component_signs": ["unsigned"] * 4, "numeric_format_bit": d & 1,
        "base_address_field_bytes": b & 0xfffff000,
        "mip_address_field_bytes": mip_address, "packed_mips": packed,
        "mip_min_level": lo, "mip_max_level": hi,
        "stored_sampler": {"clamp_xyz": ["repeat"] * 3,
                           "mag_filter": "point", "min_filter": "point",
                           "mip_filter": "point", "aniso_filter": "disabled",
                           "lod_bias": 0},
    }


def parse_itxd(data):
    """Return a deterministic byte-derived inventory or raise ITXDError."""
    require(HEADER_SIZE <= len(data) <= MAX_INPUT_BYTES, "input length outside inspector budget")
    require(data[:0x18] == PREFIX, "unsupported ITXD header signature/profile")
    require(data[0x20:0x28] == bytes(8) and u32(data, 0x2c) == 0,
            "unsupported dictionary header control words")
    first, last, raster_first = (u32(data, x) for x in (0x18, 0x1c, 0x28))
    require(first == HEADER_SIZE and raster_first == first + 0x70,
            "invalid first texture/raster links")
    require(last >= first and (last - first) % RECORD_SIZE == 0
            and last + RECORD_SIZE <= len(data), "last record outside metadata bounds")
    records, visited, names = [], set(), set()
    offset, previous = first, SENTINEL
    while offset != SENTINEL:
        require(offset not in visited, "cycle in texture list")
        require(offset == first + len(records) * RECORD_SIZE and offset <= last,
                "non-contiguous or out-of-bounds record link")
        visited.add(offset)
        word = lambda rel: u32(data, offset + rel)
        following = word(0)
        require(word(4) == previous, "texture list backlink mismatch")
        require(word(0x70) == offset + 0x70 and word(0xa4) == offset + 0xc4,
                "nested resource/self pointer mismatch")
        require(word(0xf8) == (following + 0x70 if following != SENTINEL else 0),
                "next nested reference disagrees with texture list")
        name_bytes = data[offset + 8:offset + 0x48]
        end = name_bytes.find(b"\0")
        require(0 < end < 64, "empty or unterminated texture name")
        require(all(32 <= c <= 126 for c in name_bytes[:end])
                and not any(name_bytes[end:]), "invalid ASCII name or nonzero name padding")
        name = name_bytes[:end].decode("ascii")
        require(name not in names, "duplicate texture name")
        names.add(name)
        for rel in (0x58, 0x5c, 0x60, 0x64, 0x68, 0x6c, 0x74, 0x78,
                    0x88, 0x8c, 0x94, 0xa8, 0xb0, 0xc0, 0xcc, 0xd0, 0xd4, 0xfc):
            require(word(rel) == 0, f"unsupported profile word {name}+0x{rel:x}")
        for rel, value in ((0x4c, 1), (0xc4, 3), (0xc8, 1), (0xd8, 0xffff0000), (0xdc, 0xffff0000)):
            require(word(rel) == value, f"unsupported profile word {name}+0x{rel:x}")
        fetch = descriptor(tuple(word(rel) for rel in range(0xe0, 0xf8, 4)))
        width, height, fmt = fetch["width"], fetch["height"], fetch["format"]
        require(word(0x7c) == word(0x98) == width and word(0x80) == word(0x9c) == height,
                "resource dimensions disagree with descriptor/repeated dimensions")
        require(word(0x84) == (8 if fmt["id"] == 2 else 16), "unsupported raster depth word")
        expected_format = {2: 0x28000102, 18: 0x1a200152, 19: 0x1a200153}[fmt["id"]]
        require(word(0xbc) == expected_format, "resource format word disagrees with descriptor")
        max_mip = fetch["mip_max_level"]
        require(word(0xac) & 0xff == max_mip, "mip byte disagrees with descriptor maximum")
        require(word(0x90) in (0x04000000, 0x04000002, 0x04000003,
                              0x04000081, 0x04000082, 0x04000083),
                "unsupported raster flags profile")
        require(bool(word(0x90) & 0x80) == bool(max_mip), "raster mip flag correlation mismatch")
        sampler = word(0x48)
        require(sampler in (0x1102, 0x3302, 0x1106), "unsupported engine sampler word")
        require((sampler & 0xff == 6) == bool(max_mip), "sampler/mip profile mismatch")
        start, size = word(0xb8), word(0xb4)
        require(start % 4096 == size % 4096 == 0 and size > 0
                and start >= last + RECORD_SIZE and start + size <= len(data),
                "payload extent is unaligned, empty or out of bounds")
        logical = ((width + fmt["block_width"] - 1) // fmt["block_width"]
                   * ((height + fmt["block_height"] - 1) // fmt["block_height"])
                   * fmt["bytes_per_block"])
        require(logical <= size, "payload smaller than logical base-level blocks")
        require(fetch["mip_address_field_bytes"] < size,
                "mip address field exceeds observed resource allocation")
        records.append({
            "index": len(records), "name": name, "record_offset": offset,
            "inspection_window_size": RECORD_SIZE, "record_sha256": sha256(data[offset:offset + RECORD_SIZE]),
            "raw_record_hex": data[offset:offset + RECORD_SIZE].hex(),
            "links": {"next": following, "previous": previous,
                      "nested_self": word(0x70), "resource": word(0xa4), "word_f8": word(0xf8)},
            "dimensions": {"width": width, "height": height,
                           "evidence": "three_agreeing_encodings_at_7c_98_e8"},
            "raster_depth_word": word(0x84), "raster_flags_raw": hx(word(0x90)),
            "resource_format_word_raw": hx(word(0xbc)),
            "sampler_candidate": {"raw": hx(sampler), "low_byte": sampler & 255,
                                  "bits_8_11": (sampler >> 8) & 15, "bits_12_15": (sampler >> 12) & 15,
                                  "evidence": "observed_bits_only; filter/address semantics unverified"},
            "header_bytes_ac_af": list(data[offset + 0xac:offset + 0xb0]),
            "opaque_words": {hx(rel): hx(word(rel)) for rel in (0x50, 0x54, 0xa0, 0xac)},
            "descriptor_offset": offset + 0xe0, "descriptor": fetch,
            "payload": {"offset": start, "size": size, "end_offset": start + size,
                        "sha256": sha256(data[start:start + size]),
                        "logical_base_level_bytes": logical,
                        "evidence": "offset_size_pairs_form_exact_contiguous_partition; "
                                    "logical_size_uses_reference_block_geometry"},
        })
        previous, offset = offset, following
    require(previous == last, "last record link disagrees with dictionary tail")
    metadata_end = last + RECORD_SIZE
    payload_start = (metadata_end + 4095) & ~4095
    require(not any(data[metadata_end:payload_start]), "nonzero metadata alignment padding")
    cursor = payload_start
    for item in records:
        require(item["payload"]["offset"] == cursor, "payload overlap, gap or order mismatch")
        cursor = item["payload"]["end_offset"]
    require(cursor == len(data), "unaccounted trailing bytes")
    return {
        "report_schema": SCHEMA,
        "input": {"size": len(data), "sha256": sha256(data),
                  "matches_verified_sample": sha256(data) == SAMPLE_SHA256},
        "dictionary": {"prefix_hex": data[:HEADER_SIZE].hex(), "byte_order": "big",
                       "prefix_bytes_before_first_link": HEADER_SIZE, "record_stride": RECORD_SIZE,
                       "record_anchor": "forward_back_link_node; native object boundaries unverified",
                       "list_sentinel": SENTINEL, "first_record": first, "last_record": last,
                       "word_28": raster_first, "inspected_metadata_end": metadata_end,
                       "padding_bytes": payload_start - metadata_end, "payload_start": payload_start,
                       "count_evidence": "traversed linked records; no count field assumed"},
        "summary": {"texture_count": len(records), "payload_bytes": len(data) - payload_start,
                    "format_counts": dict(sorted(Counter(x["descriptor"]["format"]["name"] for x in records).items())),
                    "dimension_counts": dict(sorted(Counter(f'{x["dimensions"]["width"]}x{x["dimensions"]["height"]}' for x in records).items())),
                    "tiled_count": len(records),
                    "packed_mip_count": sum(x["descriptor"]["packed_mips"] for x in records)},
        "textures": records, "reference_snapshots": REFERENCES,
        "hypotheses": HYPOTHESES, "limitations": LIMITATIONS,
    }


def verify_original(data, source):
    """Optionally compare against the existing read-only STR/RefPack parser."""
    import inspect_assets
    from inspect_assets import decode_entry, resource_chunks, stoc_entries

    require(source.stat().st_size <= MAX_INPUT_BYTES, "source STR exceeds inspector budget")
    raw = source.read_bytes()
    entries = stoc_entries(raw)["entries"]
    require(len(entries) > 1, "original STR has no entry 1")
    entry = entries[1]
    decoded, consumed = decode_entry(raw, entry)
    require(len(decoded) == entry["decoded_size"] and 0 < consumed <= entry["stored_size"],
            "original STR decoded length or compressed span mismatch")
    matches = [r for r in resource_chunks(decoded) if r.get("name") == "frontend_global.itxd"]
    require(len(matches) == 1, "original STR must contain one frontend_global.itxd in entry 1")
    resource = matches[0]
    start, size = resource["payload_decoded_offset"], resource["payload_size"]
    require(resource["type_name"] == "EARS_ITXD" and decoded[start:start + size] == data,
            "ITXD input is not byte-identical to the original STR resource")
    return {"verified_byte_identical": True, "source_path": source.resolve().as_posix(),
            "source_size": len(raw), "source_sha256": sha256(raw), "entry_index": 1,
            "entry_file_offset": entry["file_offset"], "entry_stored_size": entry["stored_size"],
            "entry_decoded_size": entry["decoded_size"], "refpack_consumed_bytes": consumed,
            "payload_decoded_offset": start, "payload_size": size,
            "resource_name": resource["name"], "resource_type": resource["type_name"],
            "authored_source_path": resource["source_path"],
            "str_parser_sha256": sha256(Path(inspect_assets.__file__).read_bytes())}


def json_text(report):
    return json.dumps(report, sort_keys=True, indent=2, ensure_ascii=True, allow_nan=False) + "\n"


def write_report(path, text, protected):
    """New output only: aliases, existing outputs and source directories fail closed."""
    target = path.resolve()
    for source in protected:
        require(target != source.resolve() and not (
            path.exists() and source.exists() and os.path.samefile(path, source)),
            "report output aliases an input or the inspector")
    workspace = Path(__file__).resolve().parents[1]
    for root in (workspace / "Simpsons Game, The (USA)", Path("K:/Simpsons"), Path("K:/DarkRecomp")):
        require(not target.is_relative_to(root.resolve()), "report output is inside a read-only source tree")
    for component in (path.absolute(), *path.absolute().parents):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode) and not (
            getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)),
            "report path contains a link/reparse point")
    require(path.suffix.lower() == ".json", "report output must have a .json extension")
    # Exclusive creation also rejects hard links and existing report files.
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="extracted frontend_global.itxd (read-only)")
    parser.add_argument("--source-str", type=Path, help="optionally reverify entry 1 against original frontend_global.str")
    parser.add_argument("--output", type=Path, help="new JSON path; omit for stdout; never overwrites")
    args = parser.parse_args(argv)
    try:
        require(args.input.stat().st_size <= MAX_INPUT_BYTES, "input exceeds inspector budget")
        data = args.input.read_bytes()
        report = parse_itxd(data)
        report["input"]["path"] = args.input.resolve().as_posix()
        report["producer"] = {"path": Path(__file__).resolve().as_posix(),
                              "sha256": sha256(Path(__file__).read_bytes())}
        report["original_resource"] = (verify_original(data, args.source_str) if args.source_str else
                                       {"verified_byte_identical": False, "reason": "--source-str not supplied"})
        text = json_text(report)
        if args.output:
            protected = [args.input, Path(__file__)] + ([args.source_str] if args.source_str else [])
            write_report(args.output, text, protected)
        else:
            # Match file output byte-for-byte, including on Windows (no CRLF translation).
            if hasattr(sys.stdout, "buffer"):
                sys.stdout.buffer.write(text.encode("utf-8"))
            else:
                sys.stdout.write(text)
    except (OSError, ValueError) as error:
        print(f"inspect_itxd: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
