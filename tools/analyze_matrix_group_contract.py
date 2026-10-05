"""Source-only original matrix-group transport and bounded-owner audit.

A group count is a count of byte pairs, not a shader matrix count. Optional
--inventory rereads the original packaged mesh owners without executing them.
"""
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
IMAGE_BYTES = 15466496
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
CENSUS_SHA = "13e81e770f5d96048067efcf77339fdf2fd0c56a9f20bcb7f5c7e6cfef166291"
SPANS = (
    ("select_ranges", 0x826FE710, 0x826FE7C8, "1ebf52aef355be0fda09c2ceb73b7746a9ffed9d21abcc1da4b60c4fda229547"),
    ("compose_palette", 0x826FE7C8, 0x826FEA70, "64fa3ae63fad56b4676236d360cfb5f942e363a4dd9ae9e77abaa7aed2351de9"),
    ("immediate_skin", 0x82701638, 0x82701938, "910657f28f6a4d6dea1fa9b874703d38a7453768469c0d41e28e56f2cf03d57a"),
    ("unmaterialed_skin", 0x82700318, 0x82700498, "3e1efbc035c7eebee005704aabf4a72f22a91c925a0efba848de6bf284c0cc0c"),
    ("character_parent", 0x82707138, 0x827071D4, "e58cabb839f79fcaf9e3f0be77eaddaa7cb30092b8081aab3af6fe51cce4a9c0"),
    ("character_submeshes", 0x82706378, 0x827064BC, "202eaec714212c14548ef3bd1cd831fac32d4e498810bfd045de566797a70fa6"),
    ("geometry_setup", 0x8273B760, 0x8273B824, "376e4a99d88573e18fc66f2f5611e4f11545f5109a391ba0d38e5feca9656df5"),
    ("first_pool_allocate", 0x8282F4E8, 0x8282F558, "583b071d5120f376a5baa12262fdccf3316ceda8d2f25994f7e77cf4fee4f996"),
    ("second_pool_allocate", 0x8282F558, 0x8282F5C8, "583b071d5120f376a5baa12262fdccf3316ceda8d2f25994f7e77cf4fee4f996"),
    ("blob_reader", 0x8282F618, 0x8282F788, "107bb606355759871f00919e7a5f60fe5a571af0a42cd00f3047500f43886c6b"),
    ("blob_release", 0x8282F878, 0x8282F8F4, "ca329cec2b3f5ae3617531625aa3606bfd37d9f6c2207135500d5273d98c7135"),
    ("blob_relocation", 0x82831280, 0x828312CC, "3e4f115c6d0a9a1e5c8ac9e90d6613224cdb3dc62450dcce4bba89545c6075db"),
    ("blob_visit", 0x828311D0, 0x82831280, "db1df2e73ff2f26b9527d09e46816c44f90d0480d5f937ed64ba2406162c27ee"),
    ("group_scratch_flag", 0x82D64070, 0x82D64080, "374708fff7719dd5979ec875d56cd2286f6d3cf7ec317a3b25632aab28ec37bb"),
    ("compose_scratch_flag", 0x82D68040, 0x82D68050, "374708fff7719dd5979ec875d56cd2286f6d3cf7ec317a3b25632aab28ec37bb"),
    ("shadow_matrix_descriptor", 0x820C096C, 0x820C0B74, "5a2c3d7d793dd06d3f264d8f13e63952af3e10d507c1fd8129158fd44838793d"),
    ("skin_matrix_descriptor", 0x82006794, 0x8200699C, "c74eafc90b7c9fdb80d925ea12f83c82c2890bcc19a0d341dde2946fcf250041"),
    ("textured_matrix_descriptor", 0x8200FFEC, 0x820101F4, "e5f356bbdc2698be1c6a347fb960e3ece9a0658ef26cb8c6b1bb0af18a2e55c6"),
    ("original_memcpy", 0x82A3CD80, 0x82A3D1D8, "db72136123ed7e6a3c64f746ce20049f15df73160b3bda61cb048cf1b7d46c33"),
    ("unmaterialed_matrix_upload", 0x826FD060, 0x826FDBDC, "5d089b6e7a55c087aad7161e6c3d289fbd3636ef3a9f09deee0bdeafa4df1cf5"),
    ("skin_shadow_matrix_upload", 0x826FDBE0, 0x826FE504, "d1268c1f0de6ab476f16e0a17e42d4705175d9a9bf5c07ea7c2a2dfa956b9bfa"),
    ("paired_declaration_cache_release", 0x82700A78, 0x82700B40, "aa2ed0b27ceaf7c680ed2c4cd7b9f15ce0930d17013c7ca38a389df933f48bde"),
    ("paired_effect_table_release", 0x82701118, 0x827011D0, "823287100629776aca4ec8e277d3016dcadef8d52f34a1830879c265c4cec94b"),
    ("paired_effect_manager_release", 0x826B7600, 0x826B7650, "d71eccd1f9d490d292d03204ece9af3425bb8e80c547aad1d72ba1c4feea03fc"),
)
INSTRUCTIONS = {
    0x826FE728: (0x816A4070, "select helper accesses the flag after64 scratch matrices"),
    0x826FE740: (0x2F030001, "exactly one range aliases its composed source"),
    0x826FE748: (0x89640001, "one-range selected matrix count is unsigned byte length"),
    0x826FE750: (0x89640000, "one-range first matrix is unsigned byte"),
    0x826FE754: (0x556B303E, "one-range first matrix is multiplied by64 bytes"),
    0x826FE76C: (0x2F030000, "other group counts use a signed-positive test"),
    0x826FE770: (0x3B8B3070, "multi-range destination is82D63070"),
    0x826FE780: (0x895FFFFF, "multi-range first matrix is unsigned byte"),
    0x826FE788: (0x893F0000, "multi-range length is unsigned byte, including zero"),
    0x826FE790: (0x5525303E, "memcpy extent is length times64"),
    0x826FE79C: (0x4833E5E5, "copy each selected range through original memcpy"),
    0x826FE7A8: (0x3BFF0002, "group owner advances two bytes per range"),
    0x826FE7AC: (0x7FABEA14, "selected matrix count sums range lengths"),
    0x826FE7BC: (0x93BA0000, "return total selected matrix count separately"),
    0x826FE7E4: (0x816A8040, "compose helper accesses flag at82D68040"),
    0x826FE870: (0x3BAB4080, "composed destination begins82D64080"),
    0x826FE89C: (0x7D48E0AE, "authored joint remapping uses unsigned byte elements"),
    0x826FE8A8: (0x7F08F800, "composition traverses requested authored entry count"),
    0x826FE96C: (0x396B0040, "composition advances64 bytes per matrix"),
    0x827016A4: (0x80B70028, "skin composer takes remapping owner from metadata+28hex"),
    0x827016A8: (0x80970024, "skin composer takes authored count from metadata+24hex"),
    0x827016AC: (0x4BFFD11D, "compose original matrices before submesh selection"),
    0x827017EC: (0x809E0020, "skin range pointer from submesh+32decimal"),
    0x827017F4: (0x807E001C, "skin range count from submesh+28decimal"),
    0x82701800: (0x4BFFCF11, "invoke original grouping helper without truncating group count"),
    0x82701814: (0x80C10050, "upload the returned selected matrix count"),
    0x8270181C: (0x4BFFC3C5, "transpose selected matrices through826FDBE0"),
    0x827018C0: (0x4BD4BAA1, "draw uses original submesh, independent of group count"),
    0x82700424: (0x807F001C, "unmaterialed range count has same full word transport"),
    0x8270042C: (0x4BFFE2E5, "unmaterialed helper invokes same range selection"),
    0x82700438: (0x80C10050, "unmaterialed upload consumes returned count"),
    0x82700440: (0x4BFFCC21, "unmaterialed transpose through826FD060"),
    0x82700470: (0x4BD4CEF1, "unmaterialed draw follows palette commit"),
    0x82706418: (0x807F001C, "character shadow transports full range count"),
    0x82706420: (0x4BFF82F1, "character shadow selects through same helper"),
    0x8270642C: (0x80C10050, "character shadow uploads returned count"),
    0x82706454: (0x4BD46F0D, "character shadow draw follows selected palette commit"),
    0x8282F66C: (0x80610054, "first pool byte extent is a serialized word"),
    0x8282F6F8: (0x48001B89, "reader relocates byte-pair owner with all other pointer fields"),
    0x8282F8B0: (0x4E800421, "pool release frees first owner via original allocator"),
    0x8282F8D4: (0x4E800421, "pool release independently frees second owner"),
    0x828312A0: (0x7D2A182E, "load raw relative pointer from declared first-pool relocation field"),
    0x828312A8: (0x7D291A14, "selector0 adds first-pool base"),
    0x828312B0: (0x7D292214, "nonzero selector adds second-pool base"),
    0x828312B8: (0x7D2A192E, "publish relocated range pointer in original field"),
    0x82A3CE18: (0x2B060000, "zero-byte memcpy has zero tail length"),
    0x82A3CE34: (0x419A0020, "aligned zero-byte copy jumps over all data loads/stores"),
    0x82A3CE54: (0xE861FFF8, "zero-byte copy restores its destination return value"),
    0x82700AB8: (0x4BD40C51, "paired declaration cleanup releases cached SDK declarations"),
    0x82700AF0: (0x4BF9B421, "paired declaration cleanup frees original cache owners"),
    0x826FDBF8: (0x7CC80034, "array setter distinguishes zero request from a supplied count"),
    0x826FDC34: (0x390AFFFF, "nonzero supplied count chooses all-one dynamic-count mask"),
    0x826FDC3C: (0x7D1E3038, "array setter retains nonzero requested matrix count"),
    0x826FDC78: (0x7FA84078, "zero request instead selects reflected child count"),
    0x826FDC88: (0x7D6BF214, "array setter combines selected reflected/dynamic count"),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify(image, identity=True):
    if identity and (len(image) != IMAGE_BYTES or sha(image) != IMAGE_SHA):
        raise ValueError("Original matrix image identity changed")
    for name, begin, end, digest in SPANS:
        if sha(image[begin-BASE:end-BASE]) != digest:
            raise ValueError("Original matrix span changed: " + name)
    for at, (expected, meaning) in INSTRUCTIONS.items():
        if struct.unpack_from(">I", image, at-BASE)[0] != expected:
            raise ValueError("Original matrix instruction changed: " + meaning)
    for begin, first in ((0x820C096C, 0x14), (0x82006794, 0x18), (0x8200FFEC, 0x19)):
        if struct.unpack_from(">II", image, begin-BASE) != (0x00200102, 0x03000041):
            raise ValueError("Original64-matrix descriptor changed")
        for i in range(64):
            if struct.unpack_from(">II", image, begin-BASE+8*(i+1)) != (0x5B0, 0xC0000+first+4*i):
                raise ValueError("Original matrix child descriptor changed")


def selected_matrices(bones, groups, owner):
    """Model only safe selected spans, not all original inputs or native calls."""
    if not 65 <= bones <= 255 or not 1 <= groups <= 0x7FFFFFFF:
        raise ValueError("Outside bounded composed/grouped source model")
    if 2*groups > len(owner):
        raise ValueError("Truncated byte-pair owner")
    selected = []
    for first, length in struct.iter_unpack("BB", owner[:2*groups]):
        if first+length > bones or len(selected)+length > 64:
            raise ValueError("Selected composed/shader extent exceeded")
        selected.extend(range(first, first+length))
    if not selected:
        raise ValueError("Empty selected shader palette remains outside this proof")
    return selected


def model_cases():
    cases = []
    for groups in (1, 2, 3, 64, 65, 66, 129, 257):
        owner = bytes((63, 2)) + bytes((65, 0))*(groups-1)
        selected = selected_matrices(65, groups, owner)
        if selected != [63, 64]:
            raise ValueError("Zero-length ranges changed the selected palette")
        cases.append(dict(bones=65, groups=groups, owner_bytes=len(owner),
                          owner_sha256=sha(owner), matrices=selected,
                          copied_bytes=0 if groups == 1 else 128,
                          returned_source="82D65040" if groups == 1 else "82D63070",
                          current_native_group_guard_admits=groups <= 64))
    negatives = [
        (65, 65, bytes((63, 2))+bytes((65, 0))*63+b"\x41"),
        (65, 1, bytes((64, 2))),
        (65, 2, bytes((0, 64, 0, 1))),
        (65, 0x80000000, bytes((63, 2))),
        (65, 0, b""),
        (65, 1, bytes((65, 0))),
    ]
    for args in negatives:
        try:
            selected_matrices(*args)
        except ValueError:
            pass
        else:
            raise ValueError("Malformed/unqualified matrix model input admitted")
    return cases, len(negatives)


def inventory(census_path, assets_path, asset_root):
    """Independent archive/decode/owner reads, with no guessed pointer fixups."""
    import audit_packaged_mesh_vfx as packaged
    raw_census = census_path.read_bytes()
    if sha(raw_census) != CENSUS_SHA:
        raise ValueError("Authoritative packaged census identity changed")
    census = json.loads(raw_census)
    if sha(Path(packaged.__file__).read_bytes()) != census["tool_sha256"]:
        raise ValueError("Source-qualified packaged reader changed")
    assets_raw = assets_path.read_bytes()
    if sha(assets_raw) != census["catalog_sha256"]:
        raise ValueError("Original catalog identity changed")
    assets = json.loads(assets_raw)
    for f in assets["files"]:
        for e in f.get("inspection", {}).get("entries", []):
            e["chunks"] = [c for c in e.get("chunks", []) if c.get("type_name") == "EARS_MESH"]
    prior = packaged.mesh_record
    def matrix_fields(pools, relocations, record):
        result = prior(pools, relocations, record)
        if result.get("status") != "source_pinned_fields":
            return result
        first, at = pools[0], record["offset"]
        bones = packaged.word(first, at+36)
        palette = dict(authored_count=bones, raw_map_word=f"{packaged.word(first,at+40):08X}",
                       map=None, ranges=[], failures=[])
        if bones:
            try:
                pool, offset, data = packaged.resolve(pools, relocations, at+40, bones)
                palette["map"] = dict(pool=pool, offset=offset, bytes=len(data), sha256=sha(data),
                                      minimum=min(data), maximum=max(data))
            except ValueError as exc:
                palette["failures"].append(str(exc))
        sp = result["submesh_pool"]
        for i in range(result["submesh_count"]):
            row = result["submesh_offset"]+36*i
            groups = packaged.word(pools[sp], row+28)
            r = dict(row=i, groups=groups, raw_pointer=f"{packaged.word(pools[sp],row+32):08X}", pairs=None)
            if groups:
                try:
                    if sp != 0:
                        raise ValueError("Second-pool submesh pointer fixup remains unqualified")
                    pool, offset, data = packaged.resolve(pools, relocations, row+32, 2*groups)
                    pairs = list(struct.iter_unpack("BB", data))
                    r.update(pool=pool, offset=offset, bytes=len(data), sha256=sha(data),
                             pairs=pairs, selected_matrix_count=sum(length for _, length in pairs))
                except ValueError as exc:
                    r["failure"] = str(exc)
                    palette["failures"].append(str(exc))
            palette["ranges"].append(r)
        result["matrix_palette"] = palette
        return result
    packaged.mesh_record = matrix_fields
    try:
        rows, failures, summary = packaged.audit(assets, asset_root.resolve())
    finally:
        packaged.mesh_record = prior
    if failures or summary["qualification_failures"] or summary["geometry"] != 5533 or summary["native_geometry_records"] != 25990:
        raise ValueError("Independent packaged matrix inventory is incomplete")
    bones, groups, submeshes, indices = Counter(), Counter(), Counter(), Counter()
    records, palette_failures = [], []
    for asset in rows:
        for geo in asset["parameters"].get("geometries", []):
            for pool in geo["native_records"]:
                for record in pool["records"]:
                    fields = record.get("fields")
                    if not fields or "matrix_palette" not in fields:
                        continue
                    palette = fields["matrix_palette"]
                    bones[palette["authored_count"]] += 1
                    submeshes[fields["submesh_count"]] += 1
                    groups.update(r["groups"] for r in palette["ranges"])
                    indices.update(r["material_index"] for r in fields["submeshes"])
                    row = {k:asset[k] for k in ("source", "archive_sha256", "entry", "decoded_sha256", "payload_sha256", "payload_decoded_offset", "name")}
                    row.update(record_offset=record["offset"], metadata_offset=fields["metadata_offset"],
                               matrix_palette=palette, native_lifecycle_verified=False, encountered_gameplay=False)
                    records.append(row)
                    if palette["failures"]:
                        palette_failures.append(row)
    return dict(census_path=str(census_path), census_sha256=CENSUS_SHA,
                packaged_reader_sha256=sha(Path(packaged.__file__).read_bytes()),
                catalog_sha256=sha(assets_raw), rows=records,
                summary=dict(records=len(records), named_mesh_occurrences=len(rows),
                             authored_counts=dict(sorted(bones.items())), group_counts=dict(sorted(groups.items())),
                             submesh_counts=dict(sorted(submeshes.items())), material_indices=dict(sorted(indices.items())),
                             palette_qualification_failures=len(palette_failures)),
                evidence_limits=["Serialized initial fields only; runtime fixups and actual shader/material selection are separate.",
                                 "Stock extrema are observations, never full producer admission bounds.",
                                 "Zero group pointers are preserved without dereference; no zero-owner semantic widening."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--census", type=Path, default=ROOT / "build/restrictive-check-audit/packaged-mesh-vfx-source-complete-v2-20261002.json")
    parser.add_argument("--assets", type=Path, default=ROOT / "analysis/assets.json")
    parser.add_argument("--asset-root", type=Path, default=ROOT / "Simpsons Game, The (USA)")
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    mutated, mutations = bytearray(image), 0
    for _, begin, end, _ in SPANS:
        for at in range(begin-BASE, end-BASE):
            mutated[at] ^= 1
            try:
                verify(mutated, identity=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError(f"Changed original matrix byte admitted at{at+BASE:08X}")
            finally:
                mutated[at] ^= 1
    cases, negatives = model_cases()
    report = dict(schema=1, scope="Original matrix-group source/model; native and gameplay evidence separate",
        image=dict(path="analysis/simpsons.pe", bytes=IMAGE_BYTES, base=f"{BASE:08X}", sha256=IMAGE_SHA),
        spans=[dict(name=n, begin=f"{lo:08X}", exclusive_end=f"{hi:08X}", bytes=hi-lo, sha256=h) for n,lo,hi,h in SPANS],
        instructions=[dict(address=f"{a:08X}", word=f"{w:08X}", meaning=m) for a,(w,m) in INSTRUCTIONS.items()],
        tool_sha256=sha(Path(__file__).read_bytes()), mutation_rejections=mutations,
        model_cases=cases, malformed_or_unqualified_model_rejections=negatives,
        candidate=dict(groups=65, table_bytes=130, bones=65, returned_count=2, copied_bytes=128,
                       classification="Source-bounded candidate for whole-original lifecycle regression; unexecuted",
                       original_helper="826FE710", original_skin_upload="8270181C", original_skin_draw="827018C0"),
        native_source_reference=dict(path="runtime/engine_effects.cpp", sha256=sha((ROOT/"runtime/engine_effects.cpp").read_bytes()),
            guards=["boneGroupCount:groups<=64", "skinBoneGroupCount:groups<=64", "boneGroupRows:count<=65535", "boneGroupRows:material_index<65536"]),
        bounds=dict(shader_matrices=64, grouped_scratch_bytes=0x82D64070-0x82D63070,
                    composed_bytes_before_next_flag=0x82D68040-0x82D64080,
                    composed_matrices_before_next_flag=(0x82D68040-0x82D64080)//64,
                    group_transport="DWORD;1 aliases; other signed-positive values iterate byte pairs",
                    full_authoring_domain="Unresolved; no stock maximum is treated as a valid-range proof"),
        array_upload=dict(nonzero_requested_count=2, original_selected_count=2,
                          zero_requested_count="Original826FDBE0 uses reflected array extent64; no empty-palette admission follows from helper return0"),
        native_lifecycle_verified=False, encountered_gameplay=False,
        ownership="Relocated range bytes belong to original native geometry pools.826FE710 aliases composed global scratch for1 group and copies into separate global scratch otherwise.8282F878 frees the two pool owners; it does not individually free a range table.",
        fixture_source_references=[dict(path=p, sha256=sha((ROOT/p).read_bytes())) for p in (
            "tests/test_skin_pass.cpp", "tests/test_shadow_camera_pass.cpp", "tests/test_mono_pass.cpp",
            "tests/effect_draw_cleanup_helpers.h")],
        planned_lifecycle=["Original allocator/pool reader and relocation publish a130-byte range owner at submesh+32decimal and group count65 at+28decimal.",
                           "Full dispatcher composes65 matrices through genuine byte-remapped inverse-bind/pose owners; helper returns2 with128 copied bytes.",
                           "Original selected-array upload, commit and paired opaque/alpha draws must match the equivalent1/3-group pixels and preserve nonvolatile ABI.",
                           "Reject truncated owner, out-of-palette range, selected total65, changed range identity and stale use before unsafe composition/copy.",
                           "End draw/camera, invoke82700A78 declaration cleanup, paired82701118 FX table cleanup and826B7600 manager release, then release the original range/pool owner.",
                           "Backend immutable upload cache retirement remains its own separately measured scope."],
        evidence_limits=["Arithmetic model does not execute original PPC, upload constants or draw pixels.",
                         "Scratch adjacency proves a conservative overwrite boundary, not an authoring-tool type declaration.",
                         "Empty/all-zero palettes and signed-negative group counts remain unqualified.",
                         "Count/material-index caps need independent owner extent and no-wrap proof; raw serialized DWORD fields alone do not qualify all values."])
    if args.inventory:
        report["packaged_inventory"] = inventory(args.census, args.assets, args.asset_root)
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/"build") or output.is_relative_to(args.asset_root.resolve()):
        raise ValueError("Report must stay in workspace build artifacts outside original assets")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(dict(output=str(output), mutation_rejections=mutations,
                         source_model_cases=len(cases), native_lifecycle_verified=False,
                         inventory_summary=report.get("packaged_inventory",{}).get("summary"))))


if __name__ == "__main__":
    main()
