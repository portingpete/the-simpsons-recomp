"""Pin original submesh primitive/count transport and SDK packet arithmetic.

Source evidence only. A factor-table entry is not an asset, raster, shader,
restart-policy or native lifecycle qualification.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_BYTES = 15466496
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
SPANS = (
    ("geometry_tag_dispatch", 0x826FED80, 0x826FEDE0, "dd226602f8c495a4a7cb270709477ca0cc57f71aafc12cd21279078c48f823bd"),
    ("geometry_header_setup", 0x8273B760, 0x8273B824, "376e4a99d88573e18fc66f2f5611e4f11545f5109a391ba0d38e5feca9656df5"),
    ("unmaterialed_static_submeshes", 0x826FF4C8, 0x826FF5C8, "e048ac745d1ae3b5a7aaa6eeeb4c375923ba89d813440b00cb61dcb736a0c51b"),
    ("unmaterialed_skin_submeshes", 0x82700318, 0x82700498, "3e1efbc035c7eebee005704aabf4a72f22a91c925a0efba848de6bf284c0cc0c"),
    ("immediate_static_submeshes", 0x82701220, 0x82701444, "c96ef65f8380fac1ee40fabd2a984168b0aaa632b920b9d0cb499e946730fa8e"),
    ("recorded_static_submeshes", 0x82701448, 0x82701634, "629da0eae626e35a340da1490b5e97a6372a6b261a2cddd5abe6ab40aa8d0e8e"),
    ("immediate_skin_submeshes", 0x82701638, 0x82701938, "910657f28f6a4d6dea1fa9b874703d38a7453768469c0d41e28e56f2cf03d57a"),
    ("character_shadow_submeshes", 0x82706378, 0x827064BC, "202eaec714212c14548ef3bd1cd831fac32d4e498810bfd045de566797a70fa6"),
    ("packaged_geometry_reader", 0x8282F618, 0x8282F788, "107bb606355759871f00919e7a5f60fe5a571af0a42cd00f3047500f43886c6b"),
    ("packaged_blob_relocation", 0x82831280, 0x828312CC, "3e4f115c6d0a9a1e5c8ac9e90d6613224cdb3dc62450dcce4bba89545c6075db"),
    ("packaged_record_visitor", 0x828311D0, 0x82831280, "db1df2e73ff2f26b9527d09e46816c44f90d0480d5f937ed64ba2406162c27ee"),
    ("sdk_index_draw", 0x8244D360, 0x8244D7B8, "995a205ed0aa3f1a09c998ea0fac3785ca228d38861c8b91303b5651a22db26c"),
    ("sdk_reset_enable", 0x8243B750, 0x8243B76C, "583a4aa4aa5649ff2ac42a96be286cc0155a6603b6303496c040418e726a8884"),
    ("sdk_reset_index", 0x8243B780, 0x8243B79C, "c13d053e39af582fe620145ab20097dea0912f52842a3a5067e4c19f16fceb0c"),
    # This is the inspected 0..13 prefix, not a claim about the full enum/table extent.
    ("primitive_factor_overlap_prefix", 0x821D3D30, 0x821D3DA0, "3b6088c2092b82321563f519917e5e76aa02e0b04b773bdeb7325b87ce08e99e"),
)
INSTRUCTIONS = {
    0x826FEDA8: (0x4803C9B9, "3C43A23D visitor invokes original geometry setup"),
    0x8273B784: (0x7F0B5040, "setup validates metadata version 00030002, not submesh primitive"),
    0x8273B7A8: (0x807F0000, "vertex header creation takes independently owned G+0 bytes"),
    0x8273B7D0: (0x807F0014, "index header creation takes independently owned G+14 bytes"),
    0x826FF574: (0x80EB0018, "unmaterialed static raw count from submesh+18"),
    0x826FF580: (0x808B000C, "unmaterialed static raw primitive from submesh+C"),
    0x826FF584: (0x4BD4DDDD, "submit unchanged static arguments to SDK"),
    0x82700460: (0x80FF0018, "unmaterialed skin raw count from submesh+18"),
    0x8270046C: (0x809F000C, "unmaterialed skin raw primitive from submesh+C"),
    0x82700470: (0x4BD4CEF1, "submit unchanged skin arguments to SDK"),
    0x827013A0: (0x80FF0018, "immediate static raw count from submesh+18"),
    0x827013A4: (0x80DF0014, "immediate static start index from submesh+14"),
    0x827013A8: (0x80BF0010, "immediate static signed base from submesh+10"),
    0x827013AC: (0x809F000C, "immediate static raw primitive from submesh+C"),
    0x827013B0: (0x4BD4BFB1, "submit unchanged immediate static arguments to SDK"),
    0x827015B0: (0x80FF0018, "recorded static raw count from submesh+18"),
    0x827015BC: (0x809F000C, "recorded static raw primitive from submesh+C"),
    0x827015C0: (0x4BD4BDA1, "submit unchanged recorded static arguments to SDK"),
    0x827018B0: (0x80FE0018, "immediate skin raw count from submesh+18"),
    0x827018BC: (0x809E000C, "immediate skin raw primitive from submesh+C"),
    0x827018C0: (0x4BD4BAA1, "submit unchanged immediate skin arguments to SDK"),
    0x82706444: (0x80FF0018, "shadow raw count from submesh+18"),
    0x82706450: (0x809F000C, "shadow raw primitive from submesh+C"),
    0x82706454: (0x4BD46F0D, "submit unchanged shadow arguments to SDK"),
    0x8244D370: (0x7C902378, "SDK preserves original primitive in r16"),
    0x8244D378: (0x7CD33378, "SDK preserves original start index in r19"),
    0x8244D37C: (0x7CF13B78, "SDK preserves raw r7 index count in r17"),
    0x8244D5A8: (0x3A4B3D30, "SDK selects original factor/overlap table"),
    0x8244D5B0: (0x561406BE, "packet primitive uses low six primitive bits"),
    0x8244D5DC: (0x7E388B78, "default emitted packet count equals raw remaining count"),
    0x8244D5E0: (0x2B11FFFF, "only raw counts above 65535 enter factor branch"),
    0x8244D5EC: (0x95FB0004, "SDK emits signed base independently to VGT_INDX_OFFSET"),
    0x8244D5F4: (0x560B1838, "factor-table indexing uses original primitive, not low-six-bit alias"),
    0x8244D600: (0x7D6B902E, "load primitive factor"),
    0x8244D604: (0x7D4A5B96, "divide 65535 by factor"),
    0x8244D608: (0x0CCB0000, "zero factor traps in large-count path"),
    0x8244D60C: (0x554A003C, "round quotient down to even"),
    0x8244D610: (0x7F0A59D6, "emitted raw index count equals rounded quotient times factor"),
    0x8244D618: (0x5707801E, "insert emitted raw count into packet high sixteen bits"),
    0x8244D620: (0x5669083C, "R16 start address advances two bytes per selected index"),
    0x8244D624: (0x7CFEA378, "combine count with low-six primitive code"),
    0x8244D63C: (0x7D294214, "add R16 selected start offset to index payload base"),
    0x8244D678: (0x566A103A, "R32 branch advances four bytes per selected index"),
    0x8244D694: (0x63DE0800, "R32 branch adds index format bit, not a count multiplier"),
    0x8244D788: (0x7D588851, "remaining count is old remaining minus emitted"),
    0x8244D79C: (0x7D6B482E, "load overlap indexed by original primitive"),
    0x8244D7A0: (0x7D2BC050, "start advance is emitted minus overlap"),
    0x8244D7A4: (0x7E2B5214, "next raw remaining count includes overlap"),
    0x8244D7A8: (0x7E699A14, "advance selected start by emitted minus overlap"),
}
FACTORS = ((0, 0), (1, 0), (2, 0), (1, 1), (3, 0), (1, 2), (1, 2),
           (0, 0), (3, 0), (0, 0), (0, 0), (0, 0), (0, 0), (4, 0))
XENIA_COMMIT = "95a5c3ee250f80c3b9d139658649d9ffb6db3eec"


def verify(image, identity=True):
    if identity and (len(image) != IMAGE_BYTES or hashlib.sha256(image).hexdigest() != IMAGE_SHA):
        raise ValueError("Original image identity changed")
    for name, begin, end, digest in SPANS:
        if hashlib.sha256(image[begin-BASE:end-BASE]).hexdigest() != digest:
            raise ValueError("Original primitive span changed: " + name)
    for at, (word, meaning) in INSTRUCTIONS.items():
        if struct.unpack_from(">I", image, at-BASE)[0] != word:
            raise ValueError("Original primitive instruction changed: " + meaning)
    actual = tuple(struct.unpack_from(">II", image, 0x821D3D30-BASE+8*p) for p in range(14))
    if actual != FACTORS:
        raise ValueError("Original primitive factor/overlap prefix changed")


def packets(primitive, count, start=0):
    if not 0 <= primitive < len(FACTORS) or not 0 <= count <= 0xFFFFFFFF or not 0 <= start <= 0xFFFFFFFF:
        raise ValueError("Outside inspected original scalar transport")
    factor, overlap = FACTORS[primitive]
    result = []
    while True:
        if count <= 65535:
            emitted = count
        else:
            if not factor:
                raise ValueError("Original large-count zero-factor trap")
            emitted = ((65535 // factor) & ~1) * factor
        initiator = (emitted << 16) | (primitive & 63)
        if initiator >> 16 != emitted or emitted > 65535:
            raise ValueError("Original packet count is not raw index words")
        result.append((emitted, start))
        remaining = count-emitted
        if not remaining:
            return result
        count = remaining+overlap
        start = (start+emitted-overlap) & 0xFFFFFFFF


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    mutated = bytearray(image)
    mutations = 0
    for _, begin, end, _ in SPANS:
        for at in range(begin-BASE, end-BASE):
            mutated[at] ^= 1
            try:
                verify(mutated, identity=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError(f"Changed original primitive byte admitted at {BASE+at:08X}")
            finally:
                mutated[at] ^= 1
    cases = []
    for primitive, (factor, overlap) in enumerate(FACTORS):
        for count in (0, 1, 2, 3, 4, 7, 65534, 65535, 65536, 131069):
            if not factor and count > 65535:
                try:
                    packets(primitive, count, 1)
                except ValueError:
                    cases.append(dict(primitive=primitive, count=count, large_count_zero_factor_trap=True))
                else:
                    raise ValueError("Original zero-factor large-count trap was lost")
                continue
            produced = packets(primitive, count, 1)
            if count <= 65535 and produced != [(count, 1)]:
                raise ValueError("Small raw count acquired a primitive factor multiplier")
            for before, after in zip(produced, produced[1:]):
                if before[0] != ((65535 // factor) & ~1) * factor or after[1] != (before[1]+before[0]-overlap) & 0xFFFFFFFF:
                    raise ValueError("Original primitive packet recurrence differs")
            cases.append(dict(primitive=primitive, count=count, factor=factor, overlap=overlap,
                              packets=[dict(count=n, start=s) for n, s in produced]))
    report = dict(schema=1, scope="Original serialized-field transport/SDK arithmetic only",
        image=dict(path="analysis/simpsons.pe", bytes=IMAGE_BYTES, base=f"{BASE:08X}", sha256=IMAGE_SHA),
        spans=[dict(name=n, begin=f"{b:08X}", exclusive_end=f"{e:08X}", bytes=e-b, sha256=h) for n,b,e,h in SPANS],
        instructions=[dict(address=f"{a:08X}", word=f"{w:08X}", meaning=m) for a,(w,m) in INSTRUCTIONS.items()],
        mutation_rejections=mutations, packet_arithmetic_cases=cases,
        fields=dict(row_bytes=36, primitive="+0C hex / 12 decimal / unsigned DWORD",
                    base_vertex="+10 hex / 16 decimal / signed DWORD",
                    start_index="+14 hex / 20 decimal / raw index elements",
                    count="+18 hex / 24 decimal / raw index elements"),
        setup="3C43A23D dispatch calls8273B760 for00030002 metadata. Setup creates headers from independent VB/IB byte extents and does not normalize or check submesh primitives.",
        draw="Six complete original submesh consumers load primitive/base/start/count unchanged. No primitive6 literal or count-to-triangle conversion occurs in these producers.",
        sdk="Raw r7 is used unchanged for counts<=65535. For larger counts: emitted=((65535/factor)&~1)*factor; next count=remaining+overlap; next start=start+emitted-overlap. Factor affects packet grouping only. R16 selected byte span is[base+2*start,base+2*(start+count)); R32 uses4. Owner span, signed effective vertex bounds and wrap are separate admission obligations.",
        restart="SDK draw transports selected indices and existing reset registers to hardware without CPU filtering. No new primitive may inherit strip-only FFFF skipping/reset/parity assumptions from the native R16 helper.",
        external_implementation_reference=dict(project="xenia-project/xenia", commit=XENIA_COMMIT,
            enum_url=f"https://github.com/xenia-project/xenia/blob/{XENIA_COMMIT}/src/xenia/gpu/xenos.h#L38-L84",
            enum_sha256="1224073721a11e332dab47e34555a4be6ca9a896ed842915db8e641f391e48c3",
            reset_url=f"https://github.com/xenia-project/xenia/blob/{XENIA_COMMIT}/src/xenia/gpu/primitive_processor.cc#L604-L630",
            reset_sha256="0106876f812ef9fc970b6f635e63ff91b5c45fe8922450a2d138e33c5e318f41",
            caveat="Xenos labels1/2/3/4/5/6/8/13 mean point-list/line-list/line-strip/triangle-list/fan/strip/rectangle-list/quad-list. This emulator filters reset on list topologies with an explicit assumption; it is not original hardware/serializer evidence."),
        native_mesh_primitive_admission=[6], native_lifecycle_verified=False,
        native_source_references=[dict(path=f"renderer/{name}_mesh.cpp",
            sha256=hashlib.sha256((ROOT / f"renderer/{name}_mesh.cpp").read_bytes()).hexdigest())
            for name in ("mono", "zprepass", "skin", "sky", "rigid", "shadow")],
        stock_alternate_primitive="Pending source-bound packaged census; no alternative asset credit granted by this report",
        full_valid_serializer_primitive_domain="unresolved; arbitrary raw DWORD transport and factor presence do not prove valid authored/raster combinations",
        evidence_limits=["Prefix0..13 is inspected; no full SDK enum/table-size claim follows from adjacent bytes or factor values.",
                         "Primitive low-six-bit packet encoding differs from unmasked large-count factor-table indexing; high-bit aliases are unqualified.",
                         "Zero factor traps only in large-count path; that is not proof of validity or invalidity of a small-count GPU primitive.",
                         "Stock alternatives need genuine archive/entry/relocation identity, exact selected spans, original shader/state setup, use and paired owner release before any native repair.",
                         "No native primitive, shader, pixel, mission encounter or GPU-retirement credit is granted by this source report."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to(ROOT / "build") or output == args.image.resolve():
            raise ValueError("Report output must be a separate workspace build artifact")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(json.dumps(dict(output=str(output), mutation_rejections=mutations,
                              packet_arithmetic_cases=len(cases), native_lifecycle_verified=False), sort_keys=True))
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
