"""Verify original ITXD loader branches and owner cleanup without native credit."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_BYTES = 15466496
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
# Complete original function extents, including the leaf no-op destructor.
SPANS = (
    ("standalone_relocate_reset", 0x826F2270, 0x826F22A4, "accfa2db3d7aa0ceea427058d55b138603a31371ed9682129ad18bb700e8d619"),
    ("generic_dictionary_loader", 0x826F24D8, 0x826F2660, "03d0fd6fef9120e064864f4a377b55e2c1b33cd355d900c992f31d59be3983cc"),
    ("named_copy_loader", 0x826F26B0, 0x826F2700, "22a1d74814acd63bdfa2827d4a0b9739a22759cd0ec8f7f1df9fc853319416d5"),
    ("group_create", 0x826F75B8, 0x826F7618, "46db1d5bbd396fa46ca7a524e66d45ca45447e0ab8ce7b7d6ddca93eeb26707b"),
    ("owner_attach", 0x826F7618, 0x826F76EC, "a14835f4f07934d1a2cf499d0ec007dac325ca899c3ceecffdbfbaf153b9385f"),
    ("last_owner_remove", 0x826F80C0, 0x826F8164, "83199c02c393c5c2849f7b91c28edcae47c74776043f97143b13e3aebec1f6d2"),
    ("group_release", 0x826F8168, 0x826F8234, "4e01a129bcb9a9f7bfb550670347c0bb739cbf9c5240ddda25177cd145bd8924"),
    ("index_publish_mode", 0x826F8AE8, 0x826F8B5C, "51a029b740b446cc5fd6fd38ba23759464e5b2cb0273a522fd64be28ce2a8c31"),
    ("extension_reset", 0x82736D38, 0x82736D4C, "55c1aa097b117cfadd883091af35022a6057988a3abb1d2f5c73df476665f476"),
    ("copied_destructor", 0x82736D50, 0x82736DEC, "772dbf19f32c7bc373e52a8ca5c21b8b20c1d22d78f014d4a3658f52433cc1da"),
    ("borrowed_destructor", 0x82736DF0, 0x82736DF4, "f332ea5b5437103cbb6f1508679da89eec9288ad775c96c439a17fccabe3de8e"),
    ("record_relocate", 0x82736EA8, 0x82736F54, "dd51e829a121d8fab6b5dd32f6272764041ea3ab7907e8ded8149d0413d3296e"),
    ("record_copy", 0x82736F58, 0x82737064, "3db58c0a57730425c9b903aeddce6ac116be3169e29fd4213b152e70c9b4761a"),
    ("texture_plugin_address", 0x827370E0, 0x827370F0, "fed44b85b1412ab4a1ce5097e80bead59725317f6363600d62dd74ee2e6935a4"),
    ("sdk_descriptor_address_patch", 0x82C20E80, 0x82C20EE8, "4070ece9d31e1a849f6f5dab949ebb0d7b674601d1edec524779f3264e3944aa"),
)
INSTRUCTIONS = {
    0x826F2280: (0x48044C29, "standalone wrapper relocates the caller-owned record"),
    0x826F2288: (0x48044AB1, "standalone wrapper resets the record extension"),
    0x826F24E4: (0x7CD93378, "retain original mode r6 in r25"),
    0x826F24EC: (0x573F063E, "loader considers only the low eight mode bits"),
    0x826F24FC: (0x388B6DF0, "nonzero low byte selects no-op group destructor"),
    0x826F2508: (0x388B6D50, "zero low byte selects copied group destructor"),
    0x826F250C: (0x480050AD, "create group with selected destructor"),
    0x826F2530: (0x419A008C, "zero mode selects copy/index-hit path"),
    0x826F255C: (0x4804494D, "borrowed branch directly relocates in-place record"),
    0x826F2564: (0x480447D5, "borrowed branch resets record extension"),
    0x826F2574: (0x7F26CB78, "borrowed publication retains original mode argument"),
    0x826F2584: (0x48006565, "borrowed branch publishes texture indices"),
    0x826F2590: (0x48005089, "borrowed branch attaches group owner"),
    0x826F2604: (0x48044955, "zero mode index miss copies record and payload"),
    0x826F260C: (0x4804489D, "copied branch relocates its new record"),
    0x826F2618: (0x7F26CB78, "copied publication retains original mode argument"),
    0x826F2634: (0x48004FE5, "copied record or existing index receives group owner"),
    0x826F26DC: (0x38C00000, "named loader supplies literal copy mode zero"),
    0x826F26E4: (0x4BFFFDF5, "named loader calls generic dictionary loader"),
    0x826F75D0: (0x3860000C, "group allocation is twelve bytes"),
    0x826F75EC: (0x93C30008, "group retains selected destructor at +8"),
    0x826F7628: (0x38600008, "group texture-owner node allocation is eight bytes"),
    0x826F76DC: (0x396B0001, "first group owner increments texture reference count"),
    0x826F8158: (0x4E800421, "last-owner path invokes retained group destructor"),
    0x826F8198: (0x815F0054, "group release reads retained texture reference count"),
    0x826F81A0: (0x394AFFFF, "group release decrements texture reference count"),
    0x826F81B4: (0x80630010, "first owner removal checks other owner chain"),
    0x826F81D4: (0x4BFFFEED, "last group owner invokes index/callback/destructor removal"),
    0x826F8AF8: (0x54CB063E, "publication also considers only low eight mode bits"),
    0x826F8B10: (0x616B0001, "borrowed mode sets texture plugin flag bit zero"),
    0x826F8B20: (0x556B003C, "copy mode clears texture plugin flag bit zero"),
    0x82736DA0: (0x4BD09499, "copied destructor obtains level-zero SDK lock output"),
    0x82736DBC: (0x816B0024, "copied destructor selects physical payload free slot +24"),
    0x82736DCC: (0x38A00100, "copied destructor frees the 256-byte metadata allocation"),
    0x82736DD8: (0x816B0004, "copied destructor selects metadata free slot +4"),
    0x82736DF0: (0x4E800020, "borrowed destructor returns without freeing record or payload"),
    0x82736F08: (0x814B0004, "relocation reads auxiliary raster-extension word +4"),
    0x82736F14: (0x7D4A2214, "nonzero auxiliary word receives the dictionary base"),
    0x82736F18: (0x914B0004, "relocation retains the nonzero relocated auxiliary word"),
    0x82736F30: (0x38A00000, "zero payload offset stays null during descriptor relocation"),
    0x82736F38: (0x484E9F49, "relocation patches existing SDK descriptor addresses"),
    0x82736F8C: (0x816A0014, "copy reads serialized payload offset"),
    0x82736F9C: (0x7FFBFB78, "copy preserves a zero payload offset as a null source"),
    0x82736FA4: (0x838A0010, "copy reads independent serialized payload byte extent"),
    0x82736FF4: (0x4843D11D, "copy transfers exactly 256 bytes of authored metadata"),
    0x82737020: (0x7F84E378, "physical allocation byte size is the serialized extent"),
    0x82737034: (0x816B0020, "copy selects physical payload allocator slot +20"),
    0x8273704C: (0x48305D35, "payload copy executes with serialized byte extent"),
    0x82C20EAC: (0x915F0020, "descriptor patch writes existing base address word"),
    0x82C20EB4: (0x2B030001, "descriptor patch compares existing mip-level count to one"),
    0x82C20ECC: (0x915F0030, "descriptor patch writes mip address only for multiple levels"),
}


def verify(image, identity=True):
    if identity and (len(image) != IMAGE_BYTES or hashlib.sha256(image).hexdigest() != IMAGE_SHA):
        raise ValueError("Original image identity changed")
    for name, begin, end, digest in SPANS:
        if hashlib.sha256(image[begin-BASE:end-BASE]).hexdigest() != digest:
            raise ValueError("Original ITXD span changed: " + name)
    for at, (expected, meaning) in INSTRUCTIONS.items():
        if struct.unpack_from(">I", image, at-BASE)[0] != expected:
            raise ValueError("Original ITXD instruction changed: " + meaning)


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
                raise ValueError(f"Changed original ITXD byte admitted at {BASE+at:08X}")
            finally:
                mutated[at] ^= 1
    modes = [dict(r6=f"{(upper | low):08X}", low_byte=low,
                  branch="copied" if low == 0 else "borrowed",
                  group_destructor="82736D50" if low == 0 else "82736DF0")
             for upper in (0, 0x100, 0xFFFFFF00) for low in range(256)]
    report = dict(schema=1, scope="Immutable original source only; no native execution credit",
        image=dict(path="analysis/simpsons.pe", bytes=IMAGE_BYTES, base=f"{BASE:08X}", sha256=IMAGE_SHA),
        spans=[dict(name=n, begin=f"{b:08X}", exclusive_end=f"{e:08X}", bytes=e-b, sha256=h)
               for n, b, e, h in SPANS],
        instructions=[dict(address=f"{at:08X}", word=f"{word:08X}", meaning=meaning)
                      for at, (word, meaning) in INSTRUCTIONS.items()],
        mutation_rejections=mutations, source_mode_cases=modes,
        normal_route="826F26B0 supplies literal r6=0 and calls 826F24D8. Its index-miss branch allocates/copies metadata and payload; existing index hits attach another group owner.",
        alternate_route="Generic 826F24D8 selects by low8(r6): every nonzero low byte directly relocates/resets the caller's in-place records, publishes the borrowed flag and installs no-op 82736DF0. It still owns group/owner-list nodes and reference accounting, but does not own metadata/payload allocations.",
        auxiliary="82736EA8 explicitly relocates a nonzero raster extension +4. Complete copied destructor 82736D50 does not inspect that field; it frees SDK level-zero payload and metadata only. The qualified native record maps this word to T+B0. Serialized auxiliary consumers/extent/ownership remain unresolved.",
        null_payload="Copy preserves zero payload offset as null but still calls payload allocation and byte-copy with the independent serialized extent. Relocation also preserves null before descriptor patching. These branches do not prove a valid zero-byte GPU texture or nullable positive-byte payload.",
        source_valid_generic_branch=True, encountered_alternate_gameplay_route=False,
        serializer_dimension_pitch_mip_domain="unresolved",
        native_lifecycle_verified=False,
        native_source_reference=dict(path="runtime/engine_itxd_textures.cpp",
            sha256=hashlib.sha256((ROOT / "runtime/engine_itxd_textures.cpp").read_bytes()).hexdigest()),
        evidence_limits=["No reviewed stock caller supplies nonzero mode; generic API source support is separate from encountered gameplay combinations.",
                         "Borrowed support needs resource-envelope ownership and source-buffer lifetime through all sampling and final group release; admitting nonzero mode into copied allocation/destruction policy would be incorrect.",
                         "Nonzero auxiliary relocation does not establish valid auxiliary serialization, GPU interpretation or independent allocator release.",
                         "Zero payload pointer/extent semantics and offline serializer output limits remain unresolved.",
                         "No native load/bind/sample/unbind/release or final GPU buffer/texture retirement is claimed."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to(ROOT / "build") or output == args.image.resolve():
            raise ValueError("Report output must be a separate workspace build artifact")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(json.dumps(dict(output=str(output), mutation_rejections=mutations,
                              source_mode_cases=len(modes), native_lifecycle_verified=False), sort_keys=True))
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
