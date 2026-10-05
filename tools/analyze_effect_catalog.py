"""Bounded, offline metadata catalog for the 25 original registration rows.

Only reads the pinned flat PE. Never relocates or executes an SDK object, decodes
shader instructions, or exports asset payloads. All offsets in JSON are numeric
body-relative byte offsets unless a field explicitly ends in _va. Run with -B.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
TABLE = 0x82CEFD20
TABLE_SHA = "434b529f6bbfebd448fd57400833d090add3c5b868bdd1fc42b503b5bbca1063"
NULL = 0xFFFFFFFF
SDK_SPANS = (
    (0x82C16E50, 0x60, "4df9c4d7047dd95430a9b9d1f5a12695dd3fd3bbf43a60d7f2cf95569f434eac", "leaf"),
    (0x82C17568, 0x6D8, "970346a3fad3b3bf6a770a8335f07b14587d9405a60ae3633a7023827c4d0ae9", "pdata"),
    (0x82C19498, 0xC0, "8847ae5cae6e6e031e823e87d5f383139fdfb2d0edbe23d29a373a7d25fe6946", "pdata"),
    (0x82C18E20, 0x2A8, "5e03dff3adf0460657c9fdf9a440a311a0cd542dcf74ac267f8a548e6d1c2682", "window"),
    (0x826B2958, 0xD0, "341d91279c0cd0da01118ef008f9ba2b21720dac3c155de20d91370a88185a88", "leaf"),
    (0x826B4828, 0x2A0, "9520aa7c5c6f0da4f13a25e65090c2ff63f3a4a5a2ded0ccee6c73b923640932", "pdata"),
    (0x823C7CA0, 0x74, "c26069310dd05f570647719c016d79d5e397bf0bdc2fb89845644eb5d203595e", "leaf"),
    (0x8245DF60, 0x38, "677d1fb0ae4152ac6f6d9998d60ead2847e925a7a7f10889bfdc121f210b1af6", "window: three code accessors"),
)
PROFILES = (
    ("fourtapblend", 0x820B8AA0, 0xCAC, "18f58d1e2c0b74fa86a73abe2dce583baf131583f328a044fe6cd2095378cee9"),
    ("littextured", 0x820D5730, 0x47B0, "38972d41a498c52988afc803454fb6830086e3ec8a3cac966ad2a3705ee63fd9"),
    ("particles", 0x820B9750, 0x1220, "313533556039f794c9e705a53e95005e5d05cfff40e62d091c8a56384c4798a3"),
    ("quad", 0x820BA970, 0x5BDC, "b34fd55e3d5abef878c775718455c3147387ce1bd4c1b6e32cdc0e8942355a30"),
    ("shadows", 0x820C0550, 0xFA60, "71fbbfdbc2027a8407f1cb12d73612d4dd6bda87253f381f73b200ea02d4fde5"),
    ("skinned_lit", 0x820FE8F0, 0x8510, "4a06ddf6be80e5a8eeb0d9b397357f4a42369d4d5eecdfa385e2bdf364a5cf29"),
    ("skinned_lit_tint_color", 0x82140AB0, 0x8630, "4686034fee5f5e963f43c0892e35565bd574cdba93fc7b9ea78f5fef684411cc"),
    ("skinned_bruise", 0x82110C70, 0xA300, "60210d069f2cf5a863b77fbea2af8c8b6f3caf6ba05decb8faf96c5c015bcddc"),
    ("skinned_spec", 0x82106E00, 0x9E70, "d2f49dcb59ae37c37f0bdfe763ccfffacc67e0af1d8809b2d847560d28ea6a1f"),
    ("skinned_unlit", 0x820F9350, 0x55A0, "aee4d143a0be467b081e5871b02706790d00b77d789c24cf1ea25a0a4eb36c8c"),
    ("unlittextured", 0x820CFFB0, 0x5780, "49af618ef58d554f57015a7ce9d3325f086b0774b9042664ae8c63bb09c5c1cf"),
    ("terrain", 0x820D9EE0, 0x4BB0, "f1866e81bb1592bc1deb1318c881f6bd544339a99ae2347d52869fc048d81b93"),
    ("nrmmapnoskin", 0x820DEA90, 0x4080, "609ed1decb49f89e5ac275a879e4feed46f9e9b35a240f3d7f17467b790e8ca1"),
    ("road", 0x820E2B10, 0x47F0, "8b4ae25cd2fd5d78f66168e11b61bd5b505f889a03db3e4efc1f2f8c979822e8"),
    ("base_detail", 0x82124790, 0x4760, "1a50a5b15f2d8b8ed02843ccb75ab5e00dcf753f757eb7e2d3a5d04e4eef172a"),
    ("sky", 0x820E7300, 0x28D0, "0c5c4c6bd8b2ac3a98f29b548be9fe1d962010fd2acb04cd69a8e61b824934c8"),
    ("simple", 0x820E9BD0, 0x66A0, "d905239e5f8ca1de6b0747d559e37313a3948b3e87fd3b8db9b8b754b89d1a61"),
    ("simplelit", 0x820F0270, 0x90E0, "e53cd0785f14f3099e29812caca0edb05e45efe01deaa414997d78a0f76e6431"),
    ("water", 0x8211AF70, 0x4510, "4417b04e6fa8e1711565a5595eb51ebe3a30f80bb270b8748aa60dc47a4e3f56"),
    ("mono", 0x8211F480, 0x5310, "24c7dc906da91b4b89cf0f59dd034c9584005dda68205d13753455cb9c3c68ca"),
    ("skinned_bruisenospec", 0x82128EF0, 0x89E0, "37694f3b725f208d1a50480ce8d8a9f0cd1cdb91a6acae981b6dce457bfd4ce7"),
    ("standardworld", 0x82136260, 0x46C0, "68cf412656afb1b97ab2f0643c3b0b0f3eccf5e369d2bfccdbf1322beaf581cc"),
    ("dualtexture", 0x821318D0, 0x4990, "31308f08fb73a7a8d13e5981b147fed6c49fe04494bfa418ee707d856d246b4a"),
    ("carnrmmap", 0x8213A920, 0x6190, "004406a1cf7af65c515886e18346169122349b5fc6a586c0a7757f2b54f486d5"),
    ("zprepass", 0x821490E0, 0x5330, "5468a2f63b578bf497431aeede9522730b6a46b10aa65f1e77970289ce264c6b"),
)


class CatalogError(ValueError):
    """Failures distinguish corrupt bounds, unknown framing, and changed identity."""
    def __init__(self, kind, message):
        self.kind = kind
        super().__init__(f"{kind}: {message}")


def require(ok, message, kind="malformed"):
    if not ok:
        raise CatalogError(kind, message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def hx(n):
    return f"0x{n:08X}"


def align(n, a=16):
    return (n + a - 1) & -a


class View:
    def __init__(self, raw, va=0):
        self.raw, self.va = raw, va
        self.spans = []

    def take(self, at, size, label="span", alignment=1):
        require(at >= 0 and size >= 0 and at <= len(self.raw) and size <= len(self.raw)-at,
                f"{label} [{at:#x},+{size:#x}) outside {len(self.raw):#x}", "bounds")
        require(at % alignment == 0, f"unaligned {label}", "bounds")
        return self.raw[at:at+size]

    def u32(self, at):
        return struct.unpack(">I", self.take(at, 4, "word", 4))[0]

    def u16(self, at):
        return struct.unpack(">H", self.take(at, 2, "halfword", 2))[0]

    def words(self, at, count):
        require(0 <= count <= len(self.raw)//4, "word count", "bounds")
        return list(struct.unpack(f">{count}I", self.take(at, count*4, "words", 4)))

    def span(self, at, size, label, alignment=1):
        raw = self.take(at, size, label, alignment)
        result = dict(label=label, offset=at, va=hx(self.va+at), bytes=size, sha256=sha(raw))
        self.spans.append(result)
        return result

    def string(self, at, end=None):
        end = len(self.raw) if end is None else end
        require(0 <= at < end <= len(self.raw), "string bounds", "bounds")
        raw = self.take(at, min(256, end-at), "string")
        n = raw.find(b"\0")
        require(0 < n < 256 and all(32 <= c < 127 for c in raw[:n]), "bounded ASCII string")
        return raw[:n].decode("ascii")

    def link(self, at, label, nullable=True, end=False):
        val = self.u32(at)
        if val == NULL and nullable:
            return None
        self.take(val, 0 if end else 1, label)
        return val


def validate_image(data):
    require(len(data) == IMAGE_SIZE and sha(data) == IMAGE_SHA,
            "original flat PE size/SHA256 mismatch", "identity")


def shader(v, at, size, stage):
    raw = v.take(at, size, "shader", 4)
    s = View(raw, v.va+at)
    h = s.words(0, 9)
    require(h[0] == (0x102A1101 if stage == "vertex" else 0x102A1100), "shader stage tag", "unrecognized_framing")
    require(h[1] >= 36 and h[1] % 4 == 0 and h[1] <= size and h[2] == size-h[1], "shader header/payload envelope")
    for off in h[3:7]:
        require(off == 0 or (36 <= off < h[1] and off % 4 == 0), "shader metadata offset", "bounds")
    require(h[7:9] == [0, 0], "shader reserved header words", "unrecognized_framing")
    require(h[6] and h[6]+8 <= h[1], "shader code metadata extent", "bounds")
    prefix, code = s.words(h[6], 2)
    require(prefix % 4 == 0 and prefix <= h[2] and code == h[2]-prefix and code >= 24 and code % 12 == 0,
            "shader prefix/code envelope")
    r = v.span(at, size, f"{stage} shader")
    r.update(stage=stage, header_bytes=h[1], payload_bytes=h[2], prefix_bytes=prefix, code_bytes=code,
             metadata_offsets=dict(debug=h[3], reflection=h[4], constants=h[5], code=h[6]),
             header_sha256=sha(raw[:h[1]]), prefix_sha256=sha(raw[h[1]:h[1]+prefix]),
             code_sha256=sha(raw[h[1]+prefix:]), instruction_decoding="not performed")
    return r


def resource_arrays(v):
    arrays, shaders, states = [], [], {"scalar": {}, "sampler": {}}
    for kind, h in (("vertex", 0x230), ("pixel", 0x23C), ("scalar", 0x248), ("sampler", 0x254)):
        at, count, size = v.words(h, 3)
        require(1 <= count <= 16384, f"{kind} resource count", "bounds")
        ar = v.span(at, size, kind+" resource array", 4)
        ar.update(count_including_sentinel=count, entries=[])
        cursor, end = at, at+size
        for i in range(count):
            if kind in ("vertex", "pixel"):
                require(cursor+8 <= end, "shader entry header exceeds array", "bounds")
                handle, n = v.words(cursor, 2)
                require(handle == 0, "serialized shader handle", "unrecognized_framing")
                require(cursor+8+n <= end, "shader entry exceeds array", "bounds")
                if i == 0:
                    require(n == 0, "shader sentinel size")
                    ent = dict(index=0, offset=cursor, bytes=8, shader=None)
                else:
                    require(n > 0, "empty non-sentinel shader")
                    sr = shader(v, cursor+8, n, kind)
                    sr.update(array_index=i, entry_offset=cursor)
                    shaders.append(sr)
                    ent = dict(index=i, offset=cursor, bytes=8+n, shader=sr["sha256"])
                cursor += 8+n
            else:
                base = 0x1C if kind == "scalar" else 0x8C
                require(cursor+base <= end, "state header exceeds array", "bounds")
                counts = v.words(cursor+base-12, 3)
                total = sum(counts)
                n = align(base+4+8*total)
                require(cursor+n <= end, "state count/stride exceeds array", "bounds")
                require(counts[:2] == [0, 0], "parameter-driven state categories need separate qualification", "unrecognized_framing")
                records = []
                for j in range(total):
                    off = cursor+base+j*8
                    if kind == "scalar":
                        sid, value = v.words(off, 2)
                        require(0x24 <= sid <= 0x198 and sid % 4 == 0, "scalar SDK ID", "unrecognized_framing")
                        row = dict(sdk_id=sid, value=value, default_application_selector=sid//4-9)
                    else:
                        stage, sid, value = struct.unpack(">HHI", v.take(off, 8))
                        require(stage < 32 and sid <= 0x7C and sid % 4 == 0, "sampler SDK stage/ID", "unrecognized_framing")
                        row = dict(stage=stage, sdk_id=sid, value=value, default_application_selector=sid//4+1)
                    row.update(offset=off, category="literal")
                    records.append(row)
                if i == 0:
                    require(v.take(cursor, n) == bytes(n), "nonzero state sentinel")
                require(v.take(cursor+base+8*total, n-base-8*total) == bytes(n-base-8*total),
                        "unknown state block padding", "unrecognized_framing")
                ent = v.span(cursor, n, kind+" state block", 16)
                ent.update(index=i, category_counts=dict(integer_parameter=counts[0], float_parameter=counts[1], literal=counts[2]),
                           records=records, bitmap_words=[hx(w) for w in v.words(cursor, (base-12)//4)])
                states[kind][cursor] = ent
                cursor += n
            ar["entries"].append(ent)
        require(cursor == end, f"{kind} array trailing bytes / count mismatch")
        arrays.append(ar)
    return arrays, shaders, states


def contexts(v, arrays, copy_prefix):
    at, count, size = v.words(0x224, 3)
    require(at == copy_prefix, "copy-prefix/context start disagreement")
    ar = v.span(at, size, "contexts", 16)
    require(1 <= count <= 16384, "context count", "bounds")
    stride = 0x58+64*(v.u32(0x120)+v.u32(0x124))
    for h in (0x130, 0x134):
        if v.u32(h):
            stride = align(stride)+16*v.u32(h)
    stride = align(stride)
    cursor, end, result = at, at+size, {}
    shader_entries = {a["label"].split()[0]: {e["offset"]: e for e in a["entries"]} for a in arrays[:2]}
    for i in range(count):
        require(cursor+0x58 <= end, "context header exceeds array", "bounds")
        h = v.words(cursor, 22)
        n = stride+h[21]
        require(cursor+n <= end, "context stride exceeds array", "bounds")
        require(h[20] == cursor+n, "context end link / stride mismatch")
        require(h[21] == 0, "extra context extension", "unrecognized_framing")
        links = []
        for j in range(18):
            p = h[j]
            if p != NULL:
                require(cursor+0x58 <= p < cursor+n, "context inner link outside context", "bounds")
            links.append(None if p == NULL else p)
        # 16 bookkeeping links, then private/shared vector-cache links. Empty
        # bookkeeping slices share their end address; they are not null objects.
        inner = cursor+0x58
        for group, field in ((0, 0x120), (8, 0x124)):
            width = 8*v.u32(field)
            for j in range(8):
                require(h[group+j] == inner+j*width, "context bookkeeping link geometry")
                v.take(h[group+j], width, "context bookkeeping slice")
            inner += 8*width
        for j, field in ((16, 0x130), (17, 0x134)):
            count_words = v.u32(field)
            if count_words:
                inner = align(inner-cursor)+cursor
                require(h[j] == inner, "context vector-cache link geometry")
                v.take(inner, count_words*16, "context vector-cache slice")
                inner += count_words*16
            else:
                require(h[j] == NULL, "empty context vector-cache link")
        require(align(inner-cursor)+cursor == cursor+n, "context inner extent")
        associated = {}
        for j, stage in ((18, "vertex"), (19, "pixel")):
            require(h[j] in shader_entries[stage], "context shader link not an entry boundary", "bounds")
            associated[stage] = dict(entry_offset=h[j], shader_sha256=shader_entries[stage][h[j]]["shader"])
        row = v.span(cursor, n, "context", 16)
        row.update(index=i, end_link=h[20], extension_bytes=h[21], inner_links=links, shaders=associated)
        result[cursor] = row
        cursor += n
    require(cursor == end, "context count/totalBytes mismatch")
    return ar, result


def techniques(v, ctx, states):
    at, count = v.u32(0x200), v.u32(0x20C)
    pass_at, pass_count = v.u32(0x210), v.u32(0x220)
    require(0 < count <= 16383 and 0 < pass_count <= 16383, "technique/pass count", "bounds")
    v.span(at, 4*count, "technique pointer array", 4)
    v.span(pass_at, 20*pass_count, "pass array", 4)
    passes = {}
    for i in range(pass_count):
        p = pass_at+20*i
        name, unknown, context, scalar, sampler = v.words(p, 5)
        require(context in ctx, "pass context link is not a context boundary", "bounds")
        require(scalar in states["scalar"] and sampler in states["sampler"], "pass state link is not a block boundary", "bounds")
        passes[p] = dict(index=i, offset=p, handle=hx((i << 18) | 0x3FFFE), name=v.string(name),
                         name_offset=name, unclassified_word_4=unknown, context_offset=context,
                         scalar_block_offset=scalar, sampler_block_offset=sampler,
                         scalar_states=states["scalar"][scalar]["records"], sampler_states=states["sampler"][sampler]["records"],
                         shaders=ctx[context]["shaders"])
    result, seen = [], set()
    for i in range(count):
        off = v.u32(at+4*i)
        name, n, x, y = v.words(off, 4)
        require(0 < n <= pass_count, "technique pass count", "bounds")
        v.span(off, 16+4*n, "technique", 4)
        refs = v.words(off+16, n)
        require(all(p in passes for p in refs), "technique pass link is not a pass boundary", "bounds")
        seen.update(refs)
        result.append(dict(index=i, offset=off, name=v.string(name), name_offset=name, handle=hx((i << 18) | 0x3FFFC),
                           unclassified_words_8_C=[x, y], passes=[passes[p] for p in refs]))
    require(seen == set(passes), "unreferenced pass records", "unrecognized_framing")
    return result


def parameters(v):
    """823C7B20/826B2A28 and 823C7D18: 8-byte recursive descriptor trees.

    +130/+134 count leaves, NOT allocated 16-byte storage slots. Each leaf's
    low halfword indexes the actual default block, with matrix rows padded16.
    Shared tables/defaults/names have one additional pointer-cell indirection.
    """
    spaces = {}
    for shared, space in ((False, "private"), (True, "shared")):
        def target(private_field, shared_field):
            field = shared_field if shared else private_field
            off = v.u32(field)
            if shared:
                v.span(off, 4, space+" pointer cell", 4)
                off = v.u32(off)
            return off

        count = v.u32(0x114 if shared else 0x110)
        slots = v.u32(0x11C if shared else 0x118)
        leaves = v.u32(0x134 if shared else 0x130)
        size = v.u32(0x13C if shared else 0x138)
        desc = target(0x108, 0x10C)
        values = target(0x128, 0x12C)
        names = target(0x288, 0x290)
        name_map = target(0x298, 0x29C)
        name_bytes = v.u32(0x294 if shared else 0x28C)
        result = dict(named_count=count, descriptor_slots_including_sentinel=slots, leaf_count=leaves,
                      default_bytes=size, descriptor_offset=desc, default_offset=values,
                      packed_names_offset=names, packed_names_bytes=name_bytes,
                      per_descriptor_names_offset=name_map, descriptors=[], named_parameters=[])
        spaces[space] = result
        if not slots:
            require(count == leaves == size == name_bytes == 0, "empty shared namespace counts")
            require(shared and desc == values == names == name_map == NULL, "empty shared namespace targets")
            continue
        require(0 < count < slots <= 16383 and 0 < leaves < slots, "parameter descriptor/name/leaf count", "bounds")
        result["descriptor_span"] = v.span(desc, 8*slots, space+" descriptors", 4)
        result["default_span"] = v.span(values, size, space+" defaults", 16)
        v.span(name_map, slots*4, space+" per-descriptor name links", 4)
        v.span(names, name_bytes, space+" packed parameter names")
        require(v.words(desc, 2) == [0, 0], "parameter descriptor sentinel")
        require(size % 16 == 0, "default storage alignment")
        rows, leaf_indices = [], []
        prior = 0
        if shared and v.u32(0x264):
            v.span(v.u32(0x284), slots*2, "shared parameter annotation counts", 2)
        for j in range(slots):
            word0, word1 = v.words(desc+8*j, 2)
            kind = word0 & 3
            require(kind in (0, 1, 2), "unknown descriptor kind", "unrecognized_framing")
            np = v.u32(name_map+4*j)
            name = None if np == NULL else v.string(np, names+name_bytes)
            if name is not None:
                require(np >= names, "descriptor name outside its name block", "bounds")
            row = dict(index=j, offset=desc+8*j, words=[hx(word0), hx(word1)], kind=kind,
                       name_offset=None if np == NULL else np, name=name,
                       descriptor_span=1 if kind == 0 else word1 & 0xFFFF)
            rows.append(row)
            if not j:
                require(np == NULL, "descriptor sentinel name")
                continue
            row.update(handle=hx((j << 18) | (prior << 1) | int(shared)),
                       preceding_leaf_count=prior, logical_bytes=4*(word1 >> 16),
                       sdk_reflection_flags=((word0 >> 16) | int(shared)) & 0x1F,
                       annotation_count=(v.u16(v.u32(0x284)+2*j) if shared else word0 >> 21) if v.u32(0x264) else 0)
            if kind == 0:
                cls = (word0 >> 2) & 3
                row.update(storage_slot=word1 & 0xFFFF, storage_byte_offset=16*(word1 & 0xFFFF),
                           sdk_reflection_class=cls, sdk_reflection_type=((cls+1)&4)+((word0>>4)&7),
                           rows=1+((word0>>7)&3), columns=1+((word0>>9)&3))
                prior += 1
                leaf_indices.append(j)
            else:
                row.update(child_count=(word0 >> 2) & 0x3FFF, sdk_reflection_class=5,
                           sdk_reflection_type=9, rows=1, columns=1)
        require(prior == leaves, "descriptor leaf count mismatch")
        for k, j in enumerate(leaf_indices):
            row = rows[j]
            start = row["storage_byte_offset"]
            end = rows[leaf_indices[k+1]]["storage_byte_offset"] if k+1 < len(leaf_indices) else size
            require(0 <= start < end <= size, "descriptor default slot/interval", "bounds")
            require(k != 0 or start == 0, "default storage prefix gap", "unrecognized_framing")
            if row["sdk_reflection_class"] != 3:
                require(end-start == 16*row["rows"], "numeric default row stride", "unrecognized_framing")
                require(row["logical_bytes"] == 4*row["rows"]*row["columns"], "numeric descriptor logical extent")
            else:
                require(row["logical_bytes"] == 0, "object logical extent", "unrecognized_framing")
            default = v.span(values+start, end-start, space+" leaf default", 16)
            default["words"] = [hx(w) for w in v.words(values+start, (end-start)//4)]
            default["extent_authority"] = ("16 bytes per numeric row" if row["sdk_reflection_class"] != 3 else
                                           "observed interval to next leaf/block end; not a universal object-size rule")
            row["default"] = default

        def walk(j, limit, depth=0):
            require(depth <= 32, "descriptor nesting depth", "bounds")
            require(1 <= j < limit <= slots, "descriptor child index", "bounds")
            row = rows[j]
            end = j+row["descriptor_span"]
            require(j < end <= limit, "descriptor subtree stride", "bounds")
            if row["kind"]:
                require(0 < row["child_count"] < row["descriptor_span"], "descriptor child count/stride", "bounds")
                cursor, children = j+1, []
                for _ in range(row["child_count"]):
                    children.append(cursor)
                    cursor = walk(cursor, end, depth+1)
                require(cursor == end, "descriptor children do not consume subtree stride")
                row["children"] = children
                require(row["logical_bytes"] == sum(rows[k]["logical_bytes"] for k in children),
                        "container logical extent disagrees with children")
            return end

        cursor, name_at, named = 1, names, []
        for _ in range(count):
            require(cursor < slots, "named descriptor count", "bounds")
            name = v.string(name_at, names+name_bytes)
            require(name == rows[cursor]["name"], "packed/per-descriptor name mismatch")
            named.append(dict(name=name, descriptor_index=cursor, handle=rows[cursor]["handle"]))
            name_at += len(name)+1
            cursor = walk(cursor, slots)
        require(cursor == slots, "top-level names do not consume descriptor table")
        require(len({p["name"] for p in named}) == len(named), "duplicate top-level parameter names", "unrecognized_framing")
        # The packed block also contains structure-member names after root names.
        result.update(descriptors=rows, named_parameters=named, top_level_names_bytes=name_at-names,
                      storage_bytes_minus_16_times_leaf_count=size-16*leaves,
                      handle_scope="serialized local shared namespace; merged pool handles need remapping" if shared else "private effect")
    return spaces


def auxiliary_bounds(v):
    """Check every root pointer relocated by 82C17568, including auxiliary data.

    Auxiliary targets with no proven element shape stay explicit opaque regions;
    a checked target is not a claim that its entire internal grammar is decoded.
    """
    fields = (0x108, 0x10C, 0x128, 0x12C, 0x200, 0x210, 0x224, 0x230,
              0x23C, 0x248, 0x254, 0x260, 0x26C, 0x274, 0x278, 0x27C,
              0x280, 0x284, 0x288, 0x290, 0x298, 0x29C, 0x2A0, 0x2A4, 0x2A8)
    links = []
    for field in fields:
        off = v.link(field, f"header pointer {field:#x}")
        links.append(dict(field_offset=field, target_offset=off))
    # Relocator 82C17BE0..C28 proves the +264 counted array of +2A0 links.
    count, at = v.u32(0x264), v.u32(0x2A0)
    require(count <= 16383, "auxiliary link count", "bounds")
    if count:
        # Original +2A0 arrays are sometimes only halfword-aligned (row0 AAE).
        # 82C17BF4..C14 nevertheless traverses 4-byte serialized words.
        v.span(at, count*4, "auxiliary relocated pointer array", 2)
        for i in range(count):
            target = struct.unpack(">I", v.take(at+4*i, 4, "packed auxiliary link"))[0]
            if target != NULL:
                v.take(target, 1, "auxiliary target")
    else:
        require(at == NULL, "empty auxiliary pointer array", "unrecognized_framing")
    # +270 is an explicit byte extent. Its payload is auxiliary, not decoded.
    opaque = []
    at, size = v.u32(0x26C), v.u32(0x270)
    if size:
        opaque.append(v.span(at, size, "opaque auxiliary byte block", 4))
    else:
        require(at == NULL, "empty auxiliary byte block", "unrecognized_framing")
    return dict(relocated_header_links=links, opaque_explicit_spans=opaque,
                caveat="Auxiliary annotations/reflection lookup internals are not semantically decoded; only known spans and relocated targets are bounded.")


def opaque_gaps(v):
    """Hash every remaining byte interval; never export the interval's bytes."""
    merged = []
    for start, end in sorted((s["offset"], s["offset"]+s["bytes"]) for s in v.spans):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    gaps, last = [], 0
    for start, end in merged+[[len(v.raw), len(v.raw)]]:
        if start > last:
            raw = v.take(last, start-last)
            gaps.append(dict(offset=last, va=hx(v.va+last), bytes=start-last, sha256=sha(raw),
                             all_zero=not any(raw), status="opaque_uninterpreted_bytes"))
        last = end
    return gaps


def inspect_blob(blob, va=0):
    envelope = View(blob, va)
    magic, n, prefix = envelope.words(0, 3)
    require(magic == 0xA3D70141, "FX magic/version", "unrecognized_framing")
    require(n >= 0x310 and n+12 == len(blob) and 0x310 <= prefix <= n, "exact FX envelope length/copy prefix")
    v = View(envelope.take(12, n), va+12)
    arrays, shaders, states = resource_arrays(v)
    context_array, ctx = contexts(v, arrays, prefix)
    tech = techniques(v, ctx, states)
    params = parameters(v)
    auxiliary = auxiliary_bounds(v)
    counts = Counter()
    for t in tech:
        for p in t["passes"]:
            counts["scalar"] += len(p["scalar_states"])
            counts["sampler"] += len(p["sampler_states"])
    cache_scalar = sum(len(t["passes"][0]["scalar_states"]) for t in tech)
    cache_sampler = sum(len(t["passes"][0]["sampler_states"]) for t in tech)
    return dict(blob_va=hx(va), body_va=hx(va+12), bytes=len(blob), sha256=sha(blob),
                body_bytes=n, copy_prefix_bytes=prefix, resource_arrays=arrays, contexts=list(ctx.values()),
                context_array=context_array, shaders=shaders, techniques=tech, parameters=params, auxiliary_bounds=auxiliary,
                state_profile="literal_only", pass_state_counts=dict(counts),
                engine_cache=dict(technique_rows=len(tech), pass_selection="first pass of each technique (826B4828)",
                                  scalar_rows=cache_scalar, sampler_rows=cache_sampler,
                                  bytes=24*len(tech)+12*cache_scalar+16*cache_sampler,
                                  saved_previous_values="unwritten by reflection"),
                has_shared_parameters=v.u32(0x114) > 0,
                pool_lifetime_requirement="lease required if created against a nonnull root, including zero shared-name profiles",
                checked_spans=v.spans,
                opaque_uninterpreted_body_intervals=opaque_gaps(v),
                unrecognized_framing=[], render_readiness="not_assessed")


def inspect_profile(blob, index):
    name, va, size, digest = PROFILES[index]
    require(len(blob) == size, f"{name} pinned envelope length", "identity")
    result = inspect_blob(blob, va)
    require(result["sha256"] == digest, f"{name} envelope SHA256", "identity")
    return result


def shared_pool_catalog(rows):
    """Compare local serialized namespaces; do not simulate an SDK pool merge."""
    groups, by_name = {}, {}
    for row in rows:
        s = row["parameters"]["shared"]
        key = (s["named_count"], s.get("descriptor_span", {}).get("sha256"), s.get("default_span", {}).get("sha256"))
        groups.setdefault(key, dict(named_count=key[0], descriptor_sha256=key[1], default_sha256=key[2], rows=[]))["rows"].append(row["index"])
        for p in s["named_parameters"]:
            d = s["descriptors"][p["descriptor_index"]]
            identity = (d["words"], d["default"]["sha256"])
            if p["name"] not in by_name:
                by_name[p["name"]] = dict(name=p["name"], serialized_handle=p["handle"],
                                         descriptor_words=d["words"], default_sha256=d["default"]["sha256"],
                                         default_bytes=d["default"]["bytes"], rows=[])
            known = by_name[p["name"]]
            require((known["descriptor_words"], known["default_sha256"]) == identity and known["serialized_handle"] == p["handle"],
                    "conflicting shared local descriptor/default identity", "unrecognized_framing")
            known["rows"].append(row["index"])
    return dict(local_profiles=list(groups.values()), unique_named_parameters=list(by_name.values()),
                unique_name_count=len(by_name), serialized_name_descriptor_default_conflicts=[],
                conclusion="The four-name profile matches the first four parameters of the eleven-name profile exactly.",
                live_pool_merge="Not executed; live preexisting values, compatibility, handles and ownership still require native integration.",
                lifetime="All 25 owners need a lease when created against a nonnull root, including the two no-shared-name profiles.")


def catalog(data):
    validate_image(data)
    image = View(data, BASE)
    evidence = []
    for va, n, digest, kind in SDK_SPANS:
        require(sha(image.take(va-BASE, n)) == digest, "original SDK evidence span", "identity")
        evidence.append(dict(va=hx(va), bytes=n, sha256=digest, extent_kind=kind))
    table = image.span(TABLE-BASE, 25*16, "registration table", 4)
    require(table["sha256"] == TABLE_SHA, "registration table SHA256", "identity")
    rows, unique = [], {}
    for i, (name, va, size, digest) in enumerate(PROFILES):
        at = TABLE-BASE+16*i
        b, n, out, callback = image.words(at, 4)
        require((b, image.string(n-BASE), out) == (va, name, 0), "registration row/name mismatch", "identity")
        raw = image.take(va-BASE, size, "FX envelope")
        row = inspect_profile(raw, i)
        row.update(index=i, name=name, row_va=hx(TABLE+16*i), row_sha256=sha(image.take(at, 16)),
                   name_va=hx(n), name_bytes=len(name)+1, name_sha256=sha(image.take(n-BASE, len(name)+1)),
                   output_initial=out, typed_callback_va=hx(callback))
        rows.append(row)
        for s in row["shaders"]:
            key = (s["stage"], s["bytes"], s["sha256"])
            if key not in unique:
                unique[key] = dict(stage=s["stage"], bytes=s["bytes"], sha256=s["sha256"], occurrences=[])
            unique[key]["occurrences"].append(dict(row=i, name=name, va=s["va"], entry_offset=s["entry_offset"]))
    physical = sorted((int(r["blob_va"], 16), r["bytes"], r["index"]) for r in rows)
    for (a, n, i), (next_a, _, next_i) in zip(physical, physical[1:]):
        require(a+n <= next_a, "overlapping registered FX envelopes", "bounds")
        rows[i]["physical_next_row"] = next_i
        rows[i]["gap_to_next_blob_bytes"] = next_a-a-n
    return dict(schema=1, source=dict(path="analysis/simpsons.pe", base=hx(BASE), bytes=len(data), sha256=sha(data)),
                sdk_evidence_spans=evidence,
                registration_table=table, rows=rows, unique_shaders=list(unique.values()),
                shared_pool=shared_pool_catalog(rows),
                summary=dict(rows=len(rows), blob_bytes=sum(r["bytes"] for r in rows),
                             techniques=sum(len(r["techniques"]) for r in rows),
                             passes=sum(len(t["passes"]) for r in rows for t in r["techniques"]),
                             shader_occurrences=sum(len(r["shaders"]) for r in rows), unique_shaders=len(unique),
                             unique_shaders_by_stage=dict(Counter(s["stage"] for s in unique.values())),
                             shader_identity_definition="stage + exact full record length + SHA256; sentinel entries excluded",
                             private_named_parameters=sum(r["parameters"]["private"]["named_count"] for r in rows),
                             shared_named_parameter_occurrences=sum(r["parameters"]["shared"]["named_count"] for r in rows),
                             scalar_state_pass_references=sum(r["pass_state_counts"]["scalar"] for r in rows),
                             sampler_state_pass_references=sum(r["pass_state_counts"]["sampler"] for r in rows),
                             literal_only_rows=[r["index"] for r in rows], default_resolvable_parameter_state_rows=[],
                             rows_with_shared_parameters=[r["index"] for r in rows if r["has_shared_parameters"]]),
                qualification="Offline metadata only; no SDK execution, shader instruction decoding, compilation, binding or rendering.")


def disassembly_evidence(data, source):
    """Optional original-word-checked bounded CPU disassembly, never shaders."""
    import analyze_poststart_integration as original
    _, pdata = original.layout(data)
    exe = ROOT/"build/generator-ninja/SimpsonsDisasm.exe"
    rows, assembly = [], []
    for a, n, digest, kind in SDK_SPANS:
        if kind == "pdata":
            require(pdata[a][0] == n, "SDK .pdata extent mismatch")
        output = subprocess.check_output([str(exe), str(source), hx(BASE), hx(a), str(n//4)], text=True)
        decoded = original.checked_decode(data, a, n, output)
        rows.append(dict(va=hx(a), bytes=n, sha256=digest, extent_kind=kind,
                         pdata_va=hx(pdata[a][1]) if kind == "pdata" else None, instructions=decoded))
        assembly.append(f"{hx(a)} {kind}, {n} bytes\n{output.strip()}\n")
    out = ROOT/"build/effect-catalog"
    for name in ("resource-code-evidence.json", "resource-code-disassembly.txt"):
        require((out/name).resolve() != source.resolve(), "refuse source overwrite")
    out.mkdir(parents=True, exist_ok=True)
    report = dict(image_sha256=sha(data), disassembler_sha256=sha(exe.read_bytes()), spans=rows,
                  qualification="Bounded original CPU code, verified against original words; not a shader disassembly or full shared-pool merge proof.")
    (out/"resource-code-evidence.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    (out/"resource-code-disassembly.txt").write_text("\n".join(assembly), encoding="utf-8")


def selftests(data):
    blobs = [data[a-BASE:a-BASE+n] for _, a, n, _ in PROFILES]

    class Tests(unittest.TestCase):
        def bad(self, offset, value, pattern, row=0):
            b = bytearray(blobs[row])
            struct.pack_into(">I", b, 12+offset, value)
            with self.assertRaisesRegex(CatalogError, pattern):
                inspect_blob(b)

        def test_all_profiles(self):
            result = catalog(data)
            self.assertEqual(len(result["rows"]), 25)
            self.assertEqual(result["rows"][0]["engine_cache"]["bytes"], 200)
            self.assertEqual(result["rows"][1]["name"], "littextured")
            self.assertEqual(result["rows"][1]["engine_cache"]["bytes"], 312)
            self.assertEqual(result["summary"]["unique_shaders_by_stage"], dict(vertex=68, pixel=63))

        def test_image_identity(self):
            for bad in (data[:-1], data[:100]+bytes([data[100]^1])+data[101:]):
                with self.assertRaisesRegex(CatalogError, "identity"):
                    validate_image(bad)

        def test_envelope(self):
            for bad in (blobs[0][:11], blobs[0][:-1], blobs[0]+b"\0"):
                with self.assertRaises(CatalogError):
                    inspect_blob(bad)
            self.bad(-12, 0, "unrecognized_framing")
            self.bad(-8, NULL, "envelope")
            self.bad(-4, 0, "envelope")

        def test_technique_and_pass_links(self):
            self.bad(0x200, NULL, "bounds")
            self.bad(0x20C, NULL, "count")
            self.bad(0x220, NULL, "count")
            self.bad(0x434, 0x43C, "pass link")
            self.bad(0x440, 0xBB4, "context link")
            self.bad(0x444, 0x864, "state link")

        def test_resource_counts_strides(self):
            self.bad(0x234, NULL, "count")
            self.bad(0x234, 1, "trailing")
            self.bad(0x458, NULL, "exceeds array")
            self.bad(0x24C, 3, "exceeds array")
            self.bad(0x878, NULL, "stride")
            self.bad(0x258, 3, "exceeds array")
            self.bad(0x9D8, NULL, "stride")

        def test_unknown_state_category_distinct(self):
            for at in (0x870, 0x874):
                b = bytearray(blobs[0])
                struct.pack_into(">I", b, 12+at, 1)
                struct.pack_into(">I", b, 12+0x878, 7)
                with self.assertRaisesRegex(CatalogError, "unrecognized_framing"):
                    inspect_blob(b)

        def test_shader_bounds_and_stage(self):
            self.bad(0x45C, 0x102A1100, "stage tag")
            self.bad(0x460, NULL, "header/payload")
            self.bad(0x45C+0x18, 0x110, "metadata extent")
            self.bad(0x45C+0xB8, 4, "prefix/code")

        def test_context_count_links_and_stride(self):
            self.bad(0x228, NULL, "count")
            self.bad(0xC00, 0xCA4, "end link")
            self.bad(0xBF8, 0x458, "entry boundary")
            self.bad(0xAC0, 0xCA0, "inner link")

        def test_descriptor_count_stride_children(self):
            self.bad(0x118, NULL, "count")
            self.bad(0x110, 3, "count|ASCII")
            self.bad(0x39C, 0x40000, "subtree stride")
            self.bad(0x39C, 0x40006, "subtree stride|consume")
            self.bad(0x398, 0x00200016, "child count")

        def test_nested_descriptor_stride_is_not_leaf_array(self):
            r = inspect_blob(blobs[1])
            d = r["parameters"]["private"]["descriptors"]
            self.assertEqual(d[10]["children"], [11, 17, 23, 29])
            self.assertEqual([d[i]["handle"] for i in (11, 17, 23, 29)],
                             ["0x002C0010", "0x0044001A", "0x005C0024", "0x0074002E"])
            self.bad(0x390+8*11+4, 0x00110002, "child count|subtree stride", row=1)

        def test_matrix_storage_is_not_leaf_count(self):
            d = inspect_blob(blobs[4])["parameters"]["private"]["descriptors"]
            self.assertEqual(d[16]["descriptor_span"], 65)
            self.assertEqual(d[17]["logical_bytes"], 48)
            self.assertEqual(d[17]["default"]["bytes"], 64)
            self.assertEqual(d[18]["storage_slot"]-d[17]["storage_slot"], 4)
            self.bad(0x130, 6, "context|leaf count")

        def test_default_storage_bounds_and_identity(self):
            self.bad(0x3A4, 0x0001FFFF, "default slot")
            self.bad(0x128, NULL, "bounds")
            self.bad(0x138, 64, "default slot")
            changed = bytearray(blobs[0])
            changed[12+0x3D0] ^= 1
            # A changed finite numeric value is valid structure, wrong identity.
            inspect_blob(changed)
            with self.assertRaisesRegex(CatalogError, "identity"):
                inspect_profile(changed, 0)

        def test_shared_indirection_and_name_bounds(self):
            self.bad(0x380, NULL, "bounds", row=1)
            self.bad(0x384, NULL, "bounds", row=1)
            self.bad(0x388, NULL, "bounds", row=1)
            self.bad(0x38C, NULL, "bounds", row=1)
            self.bad(0x288, NULL, "bounds")
            self.bad(0x28C, 4, "bounds|ASCII")

        def test_unknown_descriptor_kind_and_opaque_change(self):
            self.bad(0x398, 0x00200013, "unrecognized_framing")
            changed = bytearray(blobs[0])
            changed[12+0x310] ^= 1
            with self.assertRaisesRegex(CatalogError, "identity"):
                inspect_profile(changed, 0)

        def test_packed_auxiliary_links(self):
            r = inspect_blob(blobs[0])
            self.assertEqual(next(s for s in r["checked_spans"] if s["label"] == "auxiliary relocated pointer array")["offset"], 0xAAE)
            changed = bytearray(blobs[0])
            struct.pack_into(">I", changed, 12+0xAAE, 0xCA0)
            with self.assertRaisesRegex(CatalogError, "bounds"):
                inspect_blob(changed)

        def test_every_profile_truncation_at_resource_boundaries(self):
            for blob in blobs:
                r = inspect_blob(blob)
                for ar in r["resource_arrays"]+[r["context_array"]]:
                    n = 12+ar["offset"]+ar["bytes"]-1
                    changed = bytearray(blob[:n])
                    struct.pack_into(">I", changed, 4, len(changed)-12)
                    with self.assertRaises(CatalogError):
                        inspect_blob(changed)

        def test_shared_profiles_match_without_inventing_live_pool(self):
            rows = catalog(data)["rows"]
            p = shared_pool_catalog(rows)
            self.assertEqual(sorted(g["named_count"] for g in p["local_profiles"]), [0, 4, 11])
            self.assertEqual(p["unique_name_count"], 11)
            for i in (0, 3):
                self.assertFalse(rows[i]["has_shared_parameters"])
                self.assertIn("lease required", rows[i]["pool_lifetime_requirement"])

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    require(result.wasSuccessful(), "self-tests failed")
    return result.testsRun


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", type=Path, default=ROOT/"analysis/simpsons.pe")
    ap.add_argument("--check", action="store_true", help="reproduce and compare catalog; write nothing")
    ap.add_argument("--disassemble", action="store_true", help="also emit bounded original CPU instruction evidence in build/effect-catalog")
    args = ap.parse_args()
    require(not (args.check and args.disassemble), "--check is read-only; use --disassemble separately")
    data = args.image.read_bytes()
    validate_image(data)
    tests = selftests(data)
    result = catalog(data)
    result["self_test_cases_passed"] = tests
    text = json.dumps(result, indent=2, sort_keys=True)+"\n"
    path = ROOT/"analysis/native-effect-catalog.json"
    require(path.resolve() != args.image.resolve(), "refuse source overwrite")
    if args.check:
        require(path.read_text(encoding="utf-8") == text, "catalog differs from regenerated output", "identity")
    else:
        path.write_text(text, encoding="utf-8")
    if args.disassemble:
        disassembly_evidence(data, args.image)
    print(json.dumps(result["summary"], sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (CatalogError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
