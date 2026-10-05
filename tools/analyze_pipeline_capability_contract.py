"""Pin original pipeline capability transport and its allocation/lifetime ABI.

Source-only evidence: original bytes, argument arithmetic and mutation rejection.
Native create/use/readback/release evidence must come from separate executed tests.
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
# Complete recovered function extents, plus the actual allocator and release tables.
SPANS = (
    ("original_pipeline_initializer", 0x82416C58, 0x82416E48, "def981db18c3198025bb726ef6961c909b973aed12c53528d318d7f2b63f4864"),
    ("sdk_index_constructor", 0x82441A08, 0x82441AB4, "13684f8c5ba02c97363561f12f5f603c5e83dcb6a0d168729dd20a3ed05ec59e"),
    ("allocation_wrapper", 0x8238E880, 0x8238EAFC, "c3b6eda3e18d83f45dcd9cea1c924cd6c6f2d987c23e1b166d3c2d3f9948d67c"),
    ("allocation_sdk_fallback", 0x824315E0, 0x82431674, "c0b65324bf1c7a20b24c2db2b2f7395bf9a19471079fcdca3de99fa11f851cbb"),
    ("original_pipeline_cleanup", 0x82416BC8, 0x82416C54, "a87db0b8eae72ddfe38df9239226f380733254a4251e548c127ad998aede2b5b"),
    ("sdk_reference_release", 0x82441708, 0x82441788, "7ca6ed16eb6f4bed6502e5095e2d44f894a007fa55f70d0bf00d71548c5afb8e"),
    ("sdk_resource_destructor", 0x82441050, 0x82441234, "6ccccba63e86ab0f6fa7ef1f8f8002edab63bb46ab46bd205642a20f50c87482"),
    ("allocation_free_wrapper", 0x8238EB00, 0x8238EB88, "6d1e823ae58e84914eb4151f60eada544688136e8bbcd5172b4617dcd3a7b3b3"),
    ("original_allocator_lazy_resolver", 0x8268E7F0, 0x8268E858, "69e10164fc44d7bf170745014f27d97b622925669dfa2d13c960224bc083b410"),
    ("original_allocator_constructor", 0x8268E510, 0x8268E684, "11d95e38990f381daa4333c721bb9cae70bc4fb7d476878f6ff50af839decde2"),
    ("actual_normal_allocator_slot0", 0x8268DDA0, 0x8268DF8C, "ece50f1df205ffc7640a1c19e9e0e3c741068d701e8f40ec7e98c2a4f9558705"),
    ("actual_physical_allocator_slot20", 0x8268E138, 0x8268E1E8, "8afbd86dfe952b10734fcce3e9bede0221fea6c7a82c9d57ab4d62948f80a47a"),
    ("actual_allocator_vtable", 0x820B60B8, 0x820B60E0, "01727c576e243f775e92a4705043c9a8f40802d061660f97abb8e2287b758b0b"),
    ("sdk_resource_release_table", 0x8206A0A8, 0x8206A0B1, "406fb3be9b3a787747cb5746d33f25359a72c9e017653efc348c2b44d7ff333a"),
)
INSTRUCTIONS = {
    0x82416C70: (0x3C600001, "pipeline byte extent high16"),
    0x82416C74: (0x38A00001, "third index-constructor argument1"),
    0x82416C78: (0x38800008, "second index-constructor argument8"),
    0x82416C7C: (0x6063FFFE, "pipeline byte extent low16"),
    0x82416C80: (0x816B001C, "read caps82E3DFA0+1C"),
    0x82416C88: (0x7D6B58F8, "invert capword"),
    0x82416C8C: (0x55668FBC, "rotate17 and maskbit1 derive r6"),
    0x82416C9C: (0x917BFFF0, "clear original cursor before creation"),
    0x82416CA0: (0x4802AD69, "call original SDK82441A08"),
    0x82416CA4: (0x907BFFF4, "publish returned index at82D507F0"),
    0x82416E40: (0x4BFFFD89, "creation failure runs original cleanup"),
    0x82441A14: (0x7C7C1B78, "save inputr3 size"),
    0x82441A18: (0x7C9D2378, "save inputr4 flags"),
    0x82441A20: (0x38600020, "SDK header bytes20"),
    0x82441A24: (0x7CBE2B78, "save inputr5 usage; no incomingr6 capture"),
    0x82441A28: (0x4BF4CE59, "allocate header with fixed64800000 flags"),
    0x82441A68: (0x3C808280, "payload allocator base flags82800000"),
    0x82441A6C: (0x7F83E378, "payload byte size comes from savedr3"),
    0x82441A70: (0x5164E046, "payload memoryclass comes from savedr4"),
    0x82441A74: (0x4BF4CE0D, "allocate payload through same wrapper"),
    0x82441A88: (0x4BF4D079, "failed payload frees original header"),
    0x82441A94: (0x939F001C, "header retains byte size"),
    0x82441A9C: (0x93DF0000, "header flags derive savedr4/r5"),
    0x82441AA0: (0x917F0018, "header retains allocated data pointer"),
    0x8238E890: (0x7C7B1B78, "allocator saves only size"),
    0x8238E894: (0x7C9A2378, "allocator saves only flags"),
    0x8238E994: (0x38A10050, "normal allocator gets structuredhint inr5"),
    0x8238E998: (0x7F64DB78, "normal allocator gets saved bytes inr4"),
    0x8238E9B4: (0x4E800421, "normal dispatch through actual slot0"),
    0x8238EAA4: (0x7FE6FB78, "physical dispatch overwritesr6 with derived alignment"),
    0x8238EAB8: (0x4E800421, "physical dispatch through actual slot20"),
    0x824315EC: (0x7C9F2378, "SDK fallback saves flags"),
    0x824315F0: (0x7C7D1B78, "SDK fallback saves size"),
    0x82431628: (0x7CC8582E, "SDK fallback overwritesr6 from allocation flag table"),
    0x8268E548: (0x915F0000, "original manager installs vtable820B60B8"),
    0x8268DDAC: (0x7C7A1B78, "normal slot0 saves allocator instance"),
    0x8268DDB0: (0x7C9F2378, "normal slot0 saves byte size"),
    0x8268DDC4: (0x7CA92B78, "normal slot0 consumes structuredhint pointerr5"),
    0x8268DE28: (0x83690004, "normal slot0 alignment is hint-derived"),
    0x8268DEDC: (0x7FA6EB78, "normal slot0 overwritesr6 with hint-derived offset"),
    0x8268DF44: (0x7FA6EB78, "fallback heap dispatch also overwritesr6"),
    0x82416BEC: (0x4802AB1D, "original cleanup calls SDK reference release"),
    0x82416BF4: (0x917FFFFC, "original cleanup clears published index"),
    0x82441740: (0x2B030000, "release tests decremented retained reference count"),
    0x8244176C: (0x4BFFF8E5, "zero references invoke resource destructor"),
    0x82441188: (0x807F0018, "index destructor gets saved payload pointer"),
    0x8244118C: (0x3C80B180, "index destructor fixed payload free flags"),
    0x82441228: (0x4BF4D8D9, "resource destructor frees header"),
}


def verify(image, check_identity=True):
    if check_identity and (len(image) != IMAGE_BYTES or hashlib.sha256(image).hexdigest() != IMAGE_SHA):
        raise ValueError("Original image identity changed")
    for name, begin, end, digest in SPANS:
        if hashlib.sha256(image[begin-BASE:end-BASE]).hexdigest() != digest:
            raise ValueError("Original pipeline span changed: " + name)
    for address, (expected, meaning) in INSTRUCTIONS.items():
        if struct.unpack_from(">I", image, address-BASE)[0] != expected:
            raise ValueError("Original pipeline instruction changed: " + meaning)
    if struct.unpack_from(">I", image, 0x820B60B8-BASE)[0] != 0x8268DDA0:
        raise ValueError("Actual normal allocator slot0 changed")
    if struct.unpack_from(">I", image, 0x820B60D8-BASE)[0] != 0x8268E138:
        raise ValueError("Actual physical allocator slot20 changed")
    if image[0x8206A0A9-BASE] != 0x30:
        raise ValueError("Original index release dispatch changed")


def original_mode(capword):
    inverted = (~capword) & 0xFFFFFFFF
    return ((inverted << 17) | (inverted >> 15)) & 2


def contracts(image):
    take = lambda at: struct.unpack_from(">I", image, at-BASE)[0]
    byte_size = ((take(0x82416C70) & 0xFFFF) << 16) | (take(0x82416C7C) & 0xFFFF)
    if byte_size != 0x1FFFE or byte_size % 2:
        raise ValueError("Original pipeline R16 byte extent differs")
    capwords = sorted({0, 0xFFFFFFFF, 0xFFFF, 0xFFFF0000, 0xA5A5A5A5, 0x5A5A5A5A} |
                      {1 << bit for bit in range(32)} | {0xFFFFFFFF ^ (1 << bit) for bit in range(32)})
    cases = []
    negatives = 0
    for capword in capwords:
        mode = original_mode(capword)
        if mode != (0 if capword & 0x10000 else 2):
            raise ValueError("Original capword/r6 correlation differs")
        for candidate in (0, 1, 2, 3, 0x100, 0xFFFFFFFF):
            if candidate != mode:
                negatives += 1
        cases.append(dict(capword=f"{capword:08X}", r6=mode))
    return dict(byte_size=byte_size, native_type="Index16", index_capacity=byte_size // 2,
                input_r3=byte_size, input_r4=8, input_r5=1,
                input_r6="0 if caps82E3DFBC bit16 is set, otherwise2",
                sdk_header_bytes=0x20, sdk_header_flags="20100002",
                sdk_payload_allocator_flags="B2800000", scalar_cases=cases,
                mismatched_or_arbitrary_r6_cases=negatives)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    contract = contracts(image)
    mutated = bytearray(image)
    mutations = 0
    for _, begin, end, _ in SPANS:
        for address in range(begin, end):
            at = address - BASE
            mutated[at] ^= 1
            try:
                verify(mutated, check_identity=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError(f"Changed original pipeline byte admitted at {address:08X}")
            finally:
                mutated[at] ^= 1
    report = dict(schema=1,
        scope="Original source/argument/allocator/lifetime ABI only; no native execution",
        image=dict(path="analysis/simpsons.pe", bytes=IMAGE_BYTES, base=f"{BASE:08X}", sha256=IMAGE_SHA),
        spans=[dict(name=name, begin=f"{begin:08X}", exclusive_end=f"{end:08X}", bytes=end-begin, sha256=digest)
               for name, begin, end, digest in SPANS],
        instructions=[dict(address=f"{address:08X}", word=f"{word:08X}", meaning=meaning)
                      for address, (word, meaning) in INSTRUCTIONS.items()],
        contract=contract, mutation_rejections=mutations,
        producer="Complete82416C58 derivesr6 from inverse cap bit16, then calls82441A08 with identical size/flags/usage. It publishes the index and builds the same three declarations; failure invokes82416BC8.",
        sdk="Complete82441A08 captures onlyr3/r4/r5. Incomingr6 is neither read nor stored. Header and payload allocation flags, byte extents and published resource fields depend only on those captured arguments.",
        allocator="8238E880 saves size/flags. Physical slot20 overwritesr6 at8238EAA4. SDK fallback overwritesr6 from its flags table at82431628. Actual normal slot0=8268DDA0 readsr3/r4/r5, derives hint alignment/offset, and overwritesr6 at8268DEDC/8268DF44 before heap calls. No caller capability mode reaches allocation policy.",
        release="82416BC8 releases the published index and declarations and clears the fields.82441708 decrements the retained count; at zero82441050 uses resource kind2 to dispatch82441168, frees saved payload and header. No capword/r6 value is retained or used by release.",
        recommended_admission="Require exact source-correlatedr6=0/2, size1FFFE, flags8, usage1 and existing context/thread/owner/empty publication checks. The original argument's dead semantics do not authorize arbitraryr6, different sizes or caller scopes.",
        native_source_reference=dict(path="runtime/engine_pipeline_resources.cpp",
            sha256=hashlib.sha256((ROOT / "runtime/engine_pipeline_resources.cpp").read_bytes()).hexdigest()),
        native_lifecycle_verified=False,
        evidence_limits=["The native boundary replaces SDK header/payload allocation with a real host Index16 owner; original guest allocation/TLS accounting is a separate adaptation.",
                         "Source pins include lazy resolver/actual allocator family, but no lazy-init/failure heap execution is claimed.",
                         "Fresh unbound SDK index release has header+8=0; optional used-resource824574B8/deferred SDK release is not qualified by this proof.",
                         "Native create/use/readback/original cleanup plus malformed-correlated arguments must be independently executed for both cap patterns; this report grants no GPU or lifetime test receipt."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to(ROOT / "build") or output == args.image.resolve():
            raise ValueError("Report output must be a separate workspace build artifact")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(json.dumps(dict(output=str(output), mutation_rejections=mutations,
                              scalar_cases=len(contract["scalar_cases"]), native_lifecycle_verified=False), sort_keys=True))
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
