#!/usr/bin/env python3
"""Source-only submesh/material count, index and owner audit.

The optional inventory independently reopens immutable archives. It extends the
frozen v2 reader without changing that reader or granting native lifetime credit.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
CENSUS_SHA = "13e81e770f5d96048067efcf77339fdf2fd0c56a9f20bcb7f5c7e6cfef166291"
READER_SHA = "176bd013bc070bec59c3be5a5a8fd63bf9926fa1d763d4045c477957ba61b1d9"
U32 = 0xFFFFFFFF
SPANS = (
    ("unmaterialed_static", 0x826FF4C8, 0x826FF5C8, "e048ac745d1ae3b5a7aaa6eeeb4c375923ba89d813440b00cb61dcb736a0c51b"),
    ("unmaterialed_skin", 0x82700318, 0x82700498, "3e1efbc035c7eebee005704aabf4a72f22a91c925a0efba848de6bf284c0cc0c"),
    ("immediate_static", 0x82701220, 0x82701444, "c96ef65f8380fac1ee40fabd2a984168b0aaa632b920b9d0cb499e946730fa8e"),
    ("recorded_static", 0x82701448, 0x82701634, "629da0eae626e35a340da1490b5e97a6372a6b261a2cddd5abe6ab40aa8d0e8e"),
    ("immediate_skin", 0x82701638, 0x82701938, "910657f28f6a4d6dea1fa9b874703d38a7453768469c0d41e28e56f2cf03d57a"),
    ("character_shadow", 0x82706378, 0x827064BC, "202eaec714212c14548ef3bd1cd831fac32d4e498810bfd045de566797a70fa6"),
    ("geometry_setup", 0x8273B760, 0x8273B824, "376e4a99d88573e18fc66f2f5611e4f11545f5109a391ba0d38e5feca9656df5"),
    ("compiled_material_setup", 0x8273B668, 0x8273B75C, "1fd33a289c69f0e60421f6d6d6ab83cbfbd81b8f6058a25fd78bc2961c48f2b6"),
    ("material_view", 0x82831C10, 0x82831C18, "fab91cd52eeb64feaaba001f6898f1d230704b8e6d1b10bb40d46f6fdadba847"),
    ("material_lookup", 0x82831C18, 0x82831C40, "d4db76eb2cf48b9b6c4155d59ac0336feb118bffd97b2fa84a6d10c5998f7c1a"),
    ("material_count", 0x82831C40, 0x82831C4C, "a87e1bc973f416947b39f1157688a952864041d514761c011a40327cfc3fcccb"),
    ("compiled_material_relocate", 0x82831EF0, 0x82831FE0, "1f012798ae8b5302ccd48225bbb755c093a6430a1abebb66ba25362775436396"),
    ("compiled_material_serialize", 0x82831FE0, 0x828320D8, "a331befb690cf1dff0b17d352bc238950de9fae0122f7697f1d1844865cb788e"),
    ("compiled_material_parameter_cleanup", 0x82831E18, 0x82831EF0, "55aebbaedbefeb8ff8ebd6e4a81d8db7746e62cdebb74a27b36a31530f654681"),
    ("material_callbacks", 0x82700498, 0x827005E8, "0bee7e9dd1fd9d47740b978ac8ca92b67c6b16ac5532159b71b3176181342acf"),
    ("first_pool_allocate", 0x8282F4E8, 0x8282F558, "583b071d5120f376a5baa12262fdccf3316ceda8d2f25994f7e77cf4fee4f996"),
    ("second_pool_allocate", 0x8282F558, 0x8282F5C8, "583b071d5120f376a5baa12262fdccf3316ceda8d2f25994f7e77cf4fee4f996"),
    ("pool_read", 0x8282F618, 0x8282F788, "107bb606355759871f00919e7a5f60fe5a571af0a42cd00f3047500f43886c6b"),
    ("pool_relocate", 0x82831280, 0x828312CC, "3e4f115c6d0a9a1e5c8ac9e90d6613224cdb3dc62450dcce4bba89545c6075db"),
    ("pool_visit", 0x828311D0, 0x82831280, "db1df2e73ff2f26b9527d09e46816c44f90d0480d5f937ed64ba2406162c27ee"),
    ("pool_release", 0x8282F878, 0x8282F8F4, "ca329cec2b3f5ae3617531625aa3606bfd37d9f6c2207135500d5273d98c7135"),
    ("geometry_stream", 0x823D0980, 0x823D0CC0, "e58e24f546acb1da0f2b9b267ae3fcd256d7e6ba305bd0b9331c20265d0fbc35"),
    ("rw_material_list_stream", 0x823D38D8, 0x823D3AD4, "dcae40f552ad93a955fafef80d23715653b69b1adffa40d0c6950ffffedd1470"),
    ("rw_material_list_reserve", 0x823D34A0, 0x823D3554, "dd8f242dfbb70983169905a7a702b6e6fdb590b71ca7a2d4a8842c1a3a7c0333"),
    ("rw_material_list_append", 0x823D3558, 0x823D3650, "a7490bcc5b01eab6549ae4c04ad67aff8a3e7367de37e7a71667e251624d8b29"),
    ("rw_material_list_release", 0x823D32C0, 0x823D3338, "1d829a0837ddb20cc031441442f46d97d9e408e06648a3133b9be992d11081b9"),
    ("rw_geometry_release", 0x823D0568, 0x823D0618, "56dec89e3e84ea7903cf45dc1ba9cea231faea3be331f7b3ec2566d99f890b95"),
    ("material_plugin_register", 0x82727AF8, 0x82727B8C, "3ef54f2017ed6c0f29389d623ac3bad3419bf38baadd0facd89f208422e355b3"),
    ("rw_material_register_forward", 0x823DCE60, 0x823DCE84, "65707de1cff1625909d48fdeb435297be0f0e0ae5fd0b9a6d8c8e5150f33ca1c"),
    ("rw_plugin_register", 0x823FB098, 0x823FB328, "85dffd32dde537a9e9d4d48c51de37217f737ea9a028bf7538b45d09043a9314"),
    ("rw_material_stream", 0x823DD288, 0x823DD424, "6fee9504a618863a50e8801b28674950e87440af403a45b9b57f0f0989c9bfed"),
    ("rw_material_create", 0x823DCD40, 0x823DCE00, "6721ab2d432d207e1a0b932714189b76520f481a47b053e22c7dc6d62e5e93e6"),
)
PINS = {
    0x82701258: (0x2F1F0000, "subset start is signed; negative becomes zero"),
    0x82701268: (0x2F1E0000, "subset length is signed; negative selects metadata count"),
    0x82701270: (0x81790010, "metadata submesh count is a full DWORD"),
    0x82701280: (0x7F0B4840, "clamp end against metadata count unsigned"),
    0x8270128C: (0x7F0A5840, "test start against clamped end unsigned"),
    0x827012AC: (0x5575103A, "row offset is36 times selected start, after9 times start"),
    0x827012C0: (0x815F0000, "row0 is a full DWORD RwMaterial list selector"),
    0x827012C4: (0x554A103A, "RwMaterial pointer selector scales by4"),
    0x827014A8: (0x7F0B4840, "recorded static unsigned end clamp"),
    0x82701694: (0x7F1E5840, "immediate skin unsigned end clamp"),
    0x82701734: (0x839E0004, "row+4 independently selects compiled material collection"),
    0x82700368: (0x7F1D5840, "unmaterialed skin unsigned end clamp"),
    0x826FF514: (0x7F0B4840, "unmaterialed static unsigned end clamp"),
    0x827063E0: (0x2F0B0000, "character-shadow full loop is signed-positive"),
    0x82831C20: (0x7F045040, "compiled material index compared unsigned with full count"),
    0x82831C30: (0x39440003, "pointer slot adds three header DWORDs"),
    0x82831C34: (0x554A103A, "compiled slot lookup is4*(index+3)"),
    0x82831C44: (0x806B0000, "compiled count returns full DWORD"),
    0x8273B7FC: (0x807D0034, "setup takes compiled material collection from metadata+34hex"),
    0x8273B814: (0x4BFFFE55, "setup traverses materials through8273B668"),
    0x8273B748: (0x7F191840, "compiled material setup loop uses unsigned full count"),
    0x8273B734: (0x7F1C5840, "compiled material parameter loop uses unsigned full count"),
    0x82831FD0: (0x7F1F5840, "compiled material relocation loop uses unsigned full count"),
    0x823D3940: (0x2F040000, "serialized RwMaterial list count tested signed"),
    0x823D3978: (0x5543103A, "RwMaterial list index-map allocation uses4*DWORD count"),
    0x823D39D0: (0x2F0B0000, "RwMaterial stream list iteration requires signed-positive count"),
    0x823D3A60: (0x7F1A5800, "RwMaterial stream list loop compares signed count"),
    0x823D34B8: (0x7F0BE800, "RwMaterial list reserve compares signed capacity"),
    0x823D34C8: (0x57BF103A, "RwMaterial list reserve allocates4*requested count"),
    0x823D3574: (0x7F0A5800, "list growth compares signed capacity and count"),
    0x823D358C: (0x396A0014, "list growth adds20 elements rather than a65535 cap"),
    0x823D3634: (0xB17E0018, "per-material reference count is16-bit, separate from list count"),
    0x823D3644: (0x917F0004, "published RwMaterial list count is full DWORD"),
    0x82727B28: (0x38600028, "original EA13 material plugin requests40 bytes"),
    0x82727B30: (0x4BCB5331, "plugin registration through823DCE60"),
    0x82727B3C: (0x906BD814, "82D6D814 retains plugin offset, not owner allocation base"),
    0x827004E8: (0x556A8EFE, "callback first parameter comes from a5-bit original field"),
    0x827004EC: (0x556B677E, "callback count comes from a3-bit original field"),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify(image, identity=True):
    if identity and sha(image) != IMAGE_SHA:
        raise ValueError("Original image identity changed")
    for name, lo, hi, digest in SPANS:
        if sha(image[lo-BASE:hi-BASE]) != digest:
            raise ValueError("Original submesh/material span changed: " + name)
    for at, (word, meaning) in PINS.items():
        if struct.unpack_from(">I", image, at-BASE)[0] != word:
            raise ValueError("Original instruction changed: " + meaning)


def extent(base, count, stride, prefix=0, owner_end=U32, signed=False):
    """Conservative no-wrap owner model, not the complete original domain."""
    if any(type(v) is not int for v in (base, count, stride, prefix, owner_end)):
        raise ValueError("Extent inputs must be integers")
    if not 0 <= base <= U32 or not 0 <= count <= (0x7FFFFFFF if signed else U32):
        raise ValueError("Unqualified count/base signedness")
    size = prefix + count*stride
    if stride <= 0 or prefix < 0 or size > U32 or base+size > U32 or base+size > owner_end:
        raise ValueError("Wrapped or truncated complete owner span")
    return size


def subset(count, start, length):
    """Bounded original subset model; wrapped argument sums stay unqualified."""
    extent(0, count, 36)
    if not -(1 << 31) <= start < (1 << 31) or not -(1 << 31) <= length < (1 << 31):
        raise ValueError("Outside signed original subset arguments")
    first = max(start, 0)
    end = count if length < 0 else first+length
    if end > U32:
        raise ValueError("Wrapped subset argument sum")
    end = min(end, count)
    return (first, end) if first < end else (first, first)


def slot(base, index, count, prefix, owner_end):
    if type(index) is not int or not 0 <= index < count:
        raise ValueError("Selected material index exceeds original unsigned count")
    extent(base, count, 4, prefix, owner_end)
    return base+prefix+4*index


def model_cases():
    cases = []
    for count in (1, 19, 65535, 65536, 65537, 119304647):
        size = extent(0x1000, count, 36) if count < 119304647 else extent(0, count, 36)
        cases.append(dict(kind="submesh_owner", count=count, bytes=size,
                          native_65535_guard_admits=count <= 65535,
                          selected_last_row=subset(count, count-1, 1),
                          full_loop=subset(count, -1, -1)))
    for count in (1, 24, 65535, 65536, 65537):
        size = extent(0x1000, count, 4, prefix=12)
        cases.append(dict(kind="compiled_material_owner", count=count, bytes=size,
                          native_65535_guard_admits=count <= 65535,
                          last_slot_offset=slot(0x1000, count-1, count, 12, 0x1000+size)-0x1000))
    for index in (0, 23, 65535, 65536):
        count = index+1
        size = extent(0x1000, count, 4, signed=True)
        cases.append(dict(kind="rw_material_pointer_owner", index=index, count=count,
                          bytes=size, selected_offset=4*index,
                          native_65536_guard_admits=index < 65536,
                          reference_policy="Distinct original material objects avoid16-bit per-object alias overflow"))
    negatives = [(0x1000, 65536, 36, 0, 0x1000+36*65536-1, False),
                 (0x1000, 65536, 4, 12, 0x1000+12+4*65536-1, False),
                 (U32-3, 1, 4, 0, U32, False), (0, U32//36+1, 36, 0, U32, False),
                 (0, (U32-12)//4+1, 4, 12, U32, False),
                 (0, 0x80000000, 4, 0, U32, True), (0, -1, 36, 0, U32, False)]
    for args in negatives:
        try:
            extent(*args)
        except ValueError:
            pass
        else:
            raise ValueError("Malformed extent model admitted")
    slot_negatives = [(0x1000, 65536, 65536, 12, U32),
                     (0x1000, -1, 65536, 12, U32),
                     (0x1000, 0, 0, 12, U32),
                     (0x1000, 65535, 65536, 12, 0x1000+12+4*65536-1)]
    for args in slot_negatives:
        try:
            slot(*args)
        except ValueError:
            pass
        else:
            raise ValueError("Malformed material slot model admitted")
    return cases, len(negatives)+len(slot_negatives)


def compiled_fields(packaged, pools, relocations, record, fields):
    """Follow only declared EA33 relocations; preserve all local failures."""
    first, at = pools[0], record["offset"]
    result = dict(raw_pointer=f"{packaged.word(first, at+0x34):08X}", count=None,
                  status="unqualified", materials=[], failures=[])
    try:
        pool, start, data = packaged.resolve(pools, relocations, at+0x34, 12)
        if pool != 0:
            raise ValueError("Compiled collection nested relocations in second pool remain unqualified")
        count = packaged.word(data, 0)
        size = extent(0, count, 4, prefix=12, owner_end=len(first)-start)
        whole = packaged.span(first, start, size)
        result.update(status="source_pinned_fields", pool=pool, offset=start, count=count,
                      bytes=size, sha256=sha(whole), owner_bytes=len(first), owner_sha256=sha(first))
        for i in range(count):
            material = dict(index=i, status="unqualified")
            try:
                mp, ma, mh = packaged.resolve(pools, relocations, start+12+4*i, 24)
                if mp != 0:
                    raise ValueError("Compiled material nested relocations in second pool remain unqualified")
                parameters = packaged.word(mh, 4)
                material.update(status="source_pinned_fields", pool=mp, offset=ma,
                                header_sha256=sha(mh), parameter_count=parameters,
                                raw_parameter_pointer=f"{packaged.word(mh, 20):08X}")
                if parameters:
                    pp, pa, ps = packaged.resolve(pools, relocations, ma+20, parameters*12)
                    material.update(parameter_pool=pp, parameter_offset=pa,
                                    parameter_bytes=len(ps), parameter_sha256=sha(ps))
            except ValueError as exc:
                material.update(status="offline_qualification_failure", reason=str(exc))
                result["failures"].append(dict(material=i, reason=str(exc)))
            result["materials"].append(material)
        for i, draw in enumerate(fields["submeshes"]):
            index = int(draw["material_selector"], 16)
            if index >= count:
                result["failures"].append(dict(submesh=i, reason="Compiled collection index exceeds original count",
                                               collection_index=index, count=count))
    except ValueError as exc:
        result.update(status="offline_qualification_failure", reason=str(exc))
        result["failures"].append(dict(reason=str(exc)))
    return result


def rw_lists(packaged, payload):
    result, failures = [], []
    def visit(lo, hi, parent):
        for c in packaged.chunk_rows(payload, lo, hi):
            if parent == 8 and c["kind"] == 1:
                item = dict(chunk_offset=c["offset"], status="unqualified")
                try:
                    count = packaged.word(payload, c["body"], "<")
                    signed_count = count if count < 0x80000000 else count-(1 << 32)
                    if signed_count < 0:
                        raise ValueError("Negative RwMaterialList stream count remains unqualified")
                    data = packaged.span(payload, c["body"]+4, 4*count)
                    if 4+4*count != c["bytes"]:
                        raise ValueError("RwMaterialList struct tail remains unqualified")
                    indices = [x[0] for x in struct.iter_unpack("<i", data)]
                    item.update(status="source_pinned_fields", count=count, signed_count=signed_count,
                                map_bytes=len(data), map_sha256=sha(data),
                                new_material_slots=sum(x < 0 for x in indices),
                                alias_slots=sum(x >= 0 for x in indices), index_map=indices)
                    bad = [dict(slot=i, alias=j) for i,j in enumerate(indices) if j >= i]
                    if bad:
                        item["unqualified_aliases"] = bad
                except ValueError as exc:
                    item.update(status="offline_qualification_failure", reason=str(exc));failures.append(item)
                result.append(item)
            elif c["kind"] in (16, 26, 15, 8, 7, 6, 20, 3):
                visit(c["body"], c["end"], c["kind"])
    visit(0, len(payload), None)
    return result, failures


def inventory(census_path, assets_path, asset_root):
    import audit_packaged_mesh_vfx as packaged
    census_bytes = census_path.read_bytes()
    if sha(census_bytes) != CENSUS_SHA or sha(Path(packaged.__file__).read_bytes()) != READER_SHA:
        raise ValueError("Frozen packaged census or reader identity changed")
    census = json.loads(census_bytes)
    assets_raw = assets_path.read_bytes()
    if sha(assets_raw) != census["catalog_sha256"]:
        raise ValueError("Original catalog identity changed")
    assets = json.loads(assets_raw)
    for f in assets["files"]:
        for e in f.get("inspection", {}).get("entries", []):
            e["chunks"] = [c for c in e.get("chunks", []) if c.get("type_name") == "EARS_MESH"]
    prior_record, prior_mesh = packaged.mesh_record, packaged.parse_mesh
    def extra_record(pools, relocations, record):
        fields = prior_record(pools, relocations, record)
        if fields.get("status") == "source_pinned_fields":
            fields["compiled_collection"] = compiled_fields(packaged, pools, relocations, record, fields)
        return fields
    def extra_mesh(payload):
        fields = prior_mesh(payload)
        try:
            fields["rw_material_lists"], fields["rw_material_list_failures"] = rw_lists(packaged, payload)
        except ValueError as exc:
            fields["rw_material_list_failures"] = [dict(reason=str(exc))]
        return fields
    packaged.mesh_record, packaged.parse_mesh = extra_record, extra_mesh
    try:
        rows, archive_failures, base_summary = packaged.audit(assets, asset_root.resolve())
    finally:
        packaged.mesh_record, packaged.parse_mesh = prior_record, prior_mesh
    subcounts, selectors, indices, compiled_counts, param_counts, rw_counts = (Counter() for _ in range(6))
    records, failures, material_lists, maxima = [], [], [], {}
    identity = ("source", "archive_sha256", "entry", "decoded_sha256", "payload_sha256", "payload_decoded_offset", "name")
    for asset in rows:
        aid = {k:asset[k] for k in identity}
        for m in asset["parameters"].get("rw_material_lists", []):
            row = dict(aid, **m);material_lists.append(row)
            if m.get("count") is not None:
                rw_counts[m["count"]] += 1
            if m.get("status") != "source_pinned_fields" or m.get("unqualified_aliases"):
                failures.append(dict(aid, kind="rw_material_list", detail=m))
        for failure in asset["parameters"].get("rw_material_list_failures", []):
            failures.append(dict(aid, kind="rw_material_list_parse", detail=failure))
        for geo in asset["parameters"].get("geometries", []):
            for native in geo.get("native_records", []):
                for rec in native.get("records", []):
                    f = rec.get("fields")
                    if not f or f.get("status") != "source_pinned_fields":
                        continue
                    collection = f["compiled_collection"]
                    draws = [dict(row=i, rw_material_list_selector=d["material_index"],
                                  compiled_collection_index=int(d["material_selector"], 16))
                             for i,d in enumerate(f["submeshes"])]
                    subcounts[f["submesh_count"]] += 1
                    selectors.update(d["rw_material_list_selector"] for d in draws)
                    indices.update(d["compiled_collection_index"] for d in draws)
                    if collection["count"] is not None:
                        compiled_counts[collection["count"]] += 1
                    param_counts.update(m["parameter_count"] for m in collection["materials"] if "parameter_count" in m)
                    row = dict(aid, native_chunk_offset=native["chunk_offset"], metadata_offset=rec["offset"],
                               first_owner_bytes=native["first_bytes"], second_owner_bytes=native["second_bytes"],
                               submesh_count=f["submesh_count"], submesh_pool=f["submesh_pool"],
                               submesh_offset=f["submesh_offset"], submeshes=draws, compiled_collection=collection,
                               rw_material_list_relation="Same packaged geometry ordering is observed, but runtime object/record pairing is not granted by this report",
                               native_lifecycle_verified=False, encountered_gameplay=False)
                    records.append(row)
                    if collection["failures"]:
                        failures.append(dict(aid, kind="compiled_collection", metadata_offset=rec["offset"],
                                             detail=collection["failures"]))
                    for key, value in (("submesh_count", f["submesh_count"]),
                                       ("rw_material_list_selector", max((d["rw_material_list_selector"] for d in draws), default=0)),
                                       ("compiled_collection_index", max((d["compiled_collection_index"] for d in draws), default=0)),
                                       ("compiled_collection_count", collection["count"])):
                        if value is not None and (key not in maxima or value > maxima[key]["value"]):
                            maxima[key] = dict(aid, metadata_offset=rec["offset"], value=value)
    return dict(census=dict(path=str(census_path), sha256=CENSUS_SHA, reader_sha256=READER_SHA),
                catalog_sha256=sha(assets_raw), archive_failures=archive_failures,
                base_summary=base_summary, local_failures=failures, rows=records, rw_material_lists=material_lists,
                summary=dict(named_mesh_occurrences=len(rows), records=len(records), submeshes=sum(subcounts[k]*k for k in subcounts),
                    compiled_materials=sum(compiled_counts[k]*k for k in compiled_counts), rw_material_lists=len(material_lists),
                    submesh_counts=dict(sorted(subcounts.items())), rw_material_list_selectors=dict(sorted(selectors.items())),
                    compiled_collection_indices=dict(sorted(indices.items())), compiled_collection_counts=dict(sorted(compiled_counts.items())),
                    compiled_parameter_counts=dict(sorted(param_counts.items())), rw_material_list_counts=dict(sorted(rw_counts.items())),
                    local_qualification_failures=len(failures), stock_beyond_65535_submeshes=sum(v for k,v in subcounts.items() if k > 65535),
                    stock_beyond_65535_compiled_materials=sum(v for k,v in compiled_counts.items() if k > 65535),
                    stock_rw_material_selector_at_least65536=sum(v for k,v in selectors.items() if k >= 65536)), maxima=maxima)


def native_candidates():
    lines = (ROOT/"runtime/engine_effects.cpp").read_text(encoding="utf-8").splitlines()
    out = []
    for number, line in enumerate(lines, 1):
        if ("65535" in line or "65536" in line or "Static shadow submesh diagnostic" in line) and any(k in line for k in ("count", "Count", "index", "material")):
            out.append(dict(line=number, expression=line.strip(), disposition="untriaged"))
    for item in out:
        e = item["expression"]
        item["disposition"] = ("diagnostic_snapshot_cap" if "diagnostic" in e or "capture" in e.lower() else
            "compound_callback_guard_count_component_only" if "&&" in e else "source_unsupported16bit_cap")
    return dict(path="runtime/engine_effects.cpp", sha256=sha((ROOT/"runtime/engine_effects.cpp").read_bytes()),
                scope="Read-time source only; no current built-source equivalence asserted", checks=out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT/"analysis/simpsons.pe")
    parser.add_argument("--census", type=Path, default=ROOT/"build/restrictive-check-audit/packaged-mesh-vfx-source-complete-v2-20261002.json")
    parser.add_argument("--assets", type=Path, default=ROOT/"analysis/assets.json")
    parser.add_argument("--asset-root", type=Path, default=ROOT/"Simpsons Game, The (USA)")
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    image = args.image.read_bytes();verify(image)
    altered, mutations = bytearray(image), 0
    for _, lo, hi, _ in SPANS:
        for at in range(lo-BASE, hi-BASE):
            altered[at] ^= 1
            try:
                verify(altered, identity=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError(f"Mutated original byte admitted at{at+BASE:08X}")
            finally:
                altered[at] ^= 1
    models, negatives = model_cases()
    report = dict(schema=1, scope="Source-pinned original transport/owner fields and conservative arithmetic models only",
        image=dict(base=f"{BASE:08X}", sha256=IMAGE_SHA, bytes=len(image)),
        tool_sha256=sha(Path(__file__).read_bytes()),
        spans=[dict(function=n, start=f"{lo:08X}", exclusive_end=f"{hi:08X}", sha256=h) for n,lo,hi,h in SPANS],
        instructions=[dict(address=f"{a:08X}", word=f"{w:08X}", meaning=m) for a,(w,m) in PINS.items()],
        span_byte_mutation_rejections=mutations, arithmetic_model_cases=models, malformed_model_rejections=negatives,
        native_source=native_candidates(),
        established=dict(submesh_count="Metadata+10hex DWORD; five consumers unsigned subset/end, character shadow signed-positive",
            row0="Full DWORD selector into RwGeometry+24hex material-pointer array; headers are each pointer plus plugin offset82D6D814",
            row1="Full unsigned DWORD compiled collection index;82831C18 rejects index>=collection count",
            compiled_count="Metadata+34hex relocated collection DWORD0; pointer list begins+Chex; unsigned setup/relocate/serialize/parameter cleanup loops",
            rw_material_count="Original stream count and runtime reserve/growth/list cleanup are signed DWORD; pointer bytes4*count",
            ownership="EA33 first/second pool allocations own serialized submesh and compiled tables; RwMaterial list owns an independent allocated pointer table and reference-counted objects",
            true_small_fields="Per-RwMaterial reference count is16-bit; compiled callback start/count are5/3-bit fields. Neither is a16-bit table count"),
        actionable=dict(removal_candidates=["Replace count<=65535 with checked36*count no-wrap and complete readable owner span, keeping each original signedness/subset rule",
                "Replace materialCount<=65535 with checked12+4*count and complete collection owner, retaining unsigned index<count and nested material/parameter validation",
                "Replace row0 index<65536 with checked4*selector lookup inside actual RwMaterial list owner/count, then valid plugin span",
                "In compound callback guards replace only the count cap; retain exact row offset/alignment/caller/cache associations",
                "Keep diagnostic snapshot byte policy distinct from runtime validity; do not let a diagnostic cap introduce a new rejection"],
            qualifying_prerequisites=["Actual original allocator/stream publication, valid shader/material continuations and paired release must be tested before admission changes",
                "Complete declared byte owners and no-wrap addresses remain mandatory; full UINT32 count domain is not proven",
                "Distinct materials or bounded per-object aliases must avoid16-bit reference-count overflow"]),
        next_original_cases=[dict(kind="submesh_count", count=65536, bytes=36*65536,
                use="Selected final row through original start65535/count1 and separately original default all-row loop; valid skipped rows isolate actual draws",
                release="Original declaration/FX/cache cleanup and original EA33 pools, never guest clearing"),
            dict(kind="compiled_collection_count", count=65536, bytes=12+4*65536,
                use="Original setup8273B760→8273B668 and last compiled slot through82831C18 with valid material continuation",
                release="Original parameter cleanup where actually owned, then original containing pool lifetime"),
            dict(kind="rw_material_selector", index=65536, count=65537, bytes=4*65537,
                use="Original RwMaterialList stream/reserve/append with valid objects and actual row0 last-slot lookup, keeping row1 independent",
                release="Original list material-reference retirement and pointer-array free before geometry destruction")],
        unknown=["Offline authoring tool's maximum and schema constraints are not available in these runtime consumers",
            "Concrete first/second pool allocator maximum is not established here; successful bounded owners cannot imply all32-bit extents",
            "Runtime object/compiled-record/serialized RwMaterialList pairing needs original callback identity, not matching offline order alone",
            "No stock beyond-limit asset or native original beyond-limit create/use/release is credited by this source/model report"],
        native_lifecycle_verified=False, encountered_gameplay=False)
    if args.inventory:
        report["packaged_inventory"] = inventory(args.census, args.assets, args.asset_root)
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/"build") or output.is_relative_to(args.asset_root.resolve()) or output.exists():
        raise ValueError("Use a new report filename under workspace build, outside original assets")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, separators=(",", ":"), sort_keys=True)+"\n", encoding="utf-8")
    print(json.dumps(dict(output=str(output), span_byte_mutation_rejections=mutations,
        arithmetic_model_cases=len(models), malformed_model_rejections=negatives,
        inventory_summary=report.get("packaged_inventory", {}).get("summary"),
        native_lifecycle_verified=False), sort_keys=True))


if __name__ == "__main__":
    main()
