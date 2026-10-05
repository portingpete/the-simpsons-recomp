"""Pin the original singleton decal producer's fixed pools and reuse policy.

This is source-only evidence. It does not qualify the broader caller-owned
intrusive-list helpers or grant native draw, allocator or retirement coverage.
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
    ("startup_allocation", 0x827502A0, 0x827502BC, "1044740f82883349ca7a33aa6f9a604c2a56fdf6bc92ebff03c912e5b3b6b31f"),
    ("pool_constructor", 0x82767340, 0x82767668, "60b7d4f2bec41ea342f6b4412d5d3609a064e5d6d27f3f425e108692766f222e"),
    ("quad_acquire", 0x82767708, 0x827677D8, "7235deba9e5c06f5442eb6bff90d11388c7a77e4ce2d68350a5d760e9c514362"),
    ("cached_acquire", 0x827677D8, 0x827678B0, "7e5fedcf6f77cf8aa2c7354762c1ae7489efc85fc3a09c2111904b8f60d93a00"),
    ("gameplay_insert", 0x82769490, 0x827695E8, "e75847ee62a12c562f4d9e613b1d9405e34c105552172ff77efc2e8e374903ff"),
    ("group_return", 0x82764C78, 0x82764D6C, "dbd4b3f37ac42e1ed20446b3c2ecc1cdaa4cbf3e00df3ef5e5959d7733e9fa40"),
    ("expiry_return", 0x827678B0, 0x82767A10, "bad6e0965e08a9e1124ffb0aa1661d65c8f3af07f77746aca8f36499a92a3b04"),
    ("draw_lists", 0x827640A0, 0x82764240, "c5bb83afb7b2d91baaed96eedc3cd26057cb3e6fe05395d08e44bdf48dba8c58"),
    ("count_lists", 0x82764048, 0x827640A0, "569216315b283b7c0c16116c3e826fdc1ae67437227152b758e261c1c5321c5f"),
    ("game_draw_entry", 0x82753508, 0x82753538, "cb0f4d068ddd12bc2b76259614137969ee4edfb72f294c1474c34ae3a89b75d6"),
    ("pool_destructor", 0x82764B58, 0x82764C20, "291313ad2df2deb1f93394c7f5a02685c7f328f7253de4044b092c2bf0bfde3c"),
    ("owner_deleting_destructor", 0x82767668, 0x827676B8, "fed96754b67d851093b160b1cebf20ab47ea226968b11c966cc0078691f2f679"),
    ("generic_insertion", 0x827649E0, 0x82764A88, "1f48b82b4aeb57efe9b68feabd840418b5f5e7e75e0b7c6226f6fcb656ca9e75"),
)
INSTRUCTIONS = {
    0x827502A0: (0x3C60000F, "allocation size high16"),
    0x827502A4: (0x38800010, "original allocator alignment16"),
    0x827502A8: (0x60632560, "allocation size low16"),
    0x827502AC: (0x4BFFB865, "original owner allocator8274BB10"),
    0x827502B8: (0x48017089, "original constructor82767340"),
    0x82767364: (0x93C9E344, "publish singleton82DFE344"),
    0x827674B8: (0x397E0250, "quad first slot owner+250"),
    0x827674BC: (0x3940031F, "quad loop counter799, inclusive800 iterations"),
    0x827674C8: (0x93EB0010, "clear quad material owner"),
    0x827674CC: (0x912B0000, "quad callback table82153174"),
    0x827674D0: (0x396B0080, "quad slot stride80"),
    0x827674D8: (0x4098FFEC, "repeat initialization while decremented count>=0"),
    0x827674DC: (0x3F9E0002, "cached first slot high displacement20000"),
    0x827674E0: (0x3BA000C7, "cached loop counter199, inclusive200 iterations"),
    0x827674E4: (0x3B9C9260, "cached first slot signed low displacement9260"),
    0x827674EC: (0x4BFFE915, "cached node constructor82765E00"),
    0x827674F4: (0x3B9C1160, "cached slot stride1160"),
    0x827674FC: (0x4098FFEC, "repeat cached initialization while count>=0"),
    0x82767514: (0x392000A0, "160 groups of five quad free-chain entries"),
    0x827675BC: (0x93FE0244, "initial quad free head owner+244 is null"),
    0x82767610: (0x39400028, "40 groups of five cached free-chain entries"),
    0x8276770C: (0x806A0244, "load quad free head"),
    0x82767730: (0x419A0090, "empty group list has no node to reuse"),
    0x82767790: (0x556A003E, "reuse existing quad pointer without allocation"),
    0x82767798: (0x7D435378, "return existing quad pointer"),
    0x827677B8: (0x916A0010, "clear reused quad material owner"),
    0x827677C0: (0x38600000, "no reusable quad returns null"),
    0x827677C8: (0x81630030, "load quad next free link"),
    0x827677CC: (0x916A0244, "pop quad free head"),
    0x827677E0: (0x396B9250, "cached free head owner+19250"),
    0x827677E4: (0x806B0000, "load cached free head"),
    0x82767808: (0x419A0090, "empty group list has no cached node to reuse"),
    0x82767868: (0x556A003E, "reuse existing cached pointer without allocation"),
    0x82767870: (0x7D435378, "return existing cached pointer"),
    0x82767890: (0x916A0010, "clear reused cached material owner"),
    0x82767898: (0x38600000, "no reusable cached node returns null"),
    0x827678A0: (0x81430030, "load cached next free link"),
    0x827678A4: (0x914B0000, "pop cached free head"),
    0x82769558: (0x4BFFE281, "gameplay cached acquire827677D8"),
    0x82769590: (0x4BFFE179, "gameplay quad acquire82767708"),
    0x827695B4: (0x397E0030, "link the acquired node through node+30"),
    0x827695C8: (0x912A0030, "publish acquired node in original intrusive list"),
    0x82764CAC: (0x912B0030, "group cleanup links quad back to free head"),
    0x82764CB0: (0x916A0244, "group cleanup publishes quad free head"),
    0x82764CF8: (0x910B0030, "group cleanup links cached node to free head"),
    0x82764CFC: (0x916A0000, "group cleanup publishes cached free head"),
    0x82764B74: (0x3BC000C7, "destructor visits200 cached slots"),
    0x82764B88: (0x3BFFEEA0, "destructor cached stride minus1160"),
    0x82764BB4: (0x3940031F, "destructor visits800 quad slots"),
    0x82764BBC: (0x396BFF80, "destructor quad stride minus80"),
    0x82767684: (0x4BFFD4D5, "deleting destructor calls full pool destructor"),
    0x82767698: (0x4BFE44D9, "deleting flag bit0 calls paired owner free8274BB70"),
}


def verify(image, check_identity=True):
    if check_identity and (len(image) != IMAGE_BYTES or hashlib.sha256(image).hexdigest() != IMAGE_SHA):
        raise ValueError("Original image identity changed")
    for name, begin, end, digest in SPANS:
        if hashlib.sha256(image[begin-BASE:end-BASE]).hexdigest() != digest:
            raise ValueError("Original decal span changed: " + name)
    for address, (expected, meaning) in INSTRUCTIONS.items():
        if struct.unpack_from(">I", image, address-BASE)[0] != expected:
            raise ValueError("Original decal instruction changed: " + meaning)


def immediate(image, address):
    value = struct.unpack_from(">I", image, address-BASE)[0] & 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def arithmetic(image):
    allocation = (immediate(image, 0x827502A0) << 16) | immediate(image, 0x827502A8)
    quad_count = immediate(image, 0x827674BC) + 1
    cached_count = immediate(image, 0x827674E0) + 1
    quad_start, quad_stride = immediate(image, 0x827674B8), immediate(image, 0x827674D0)
    cached_start = (immediate(image, 0x827674DC) << 16) + immediate(image, 0x827674E4)
    cached_stride = immediate(image, 0x827674F4)
    if (allocation, quad_count, cached_count, quad_start, quad_stride, cached_start, cached_stride) != (
            0xF2560, 800, 200, 0x250, 0x80, 0x19260, 0x1160):
        raise ValueError("Original allocation/slot arithmetic differs")
    if 5 * immediate(image, 0x82767514) != quad_count or 5 * immediate(image, 0x82767610) != cached_count:
        raise ValueError("Original free chains differ from their initialized pools")
    quads = [quad_start + i * quad_stride for i in range(quad_count)]
    cached = [cached_start + i * cached_stride for i in range(cached_count)]
    if len(set(quads + cached)) != quad_count + cached_count:
        raise ValueError("Original slot identities overlap")
    if quads[-1] + quad_stride > cached_start or cached[-1] + cached_stride != allocation:
        raise ValueError("Original slots exceed/overlap their allocated owner")
    if immediate(image, 0x82764B74) + 1 != cached_count or immediate(image, 0x82764BB4) + 1 != quad_count:
        raise ValueError("Original destructor iteration count differs")
    if -immediate(image, 0x82764B88) != cached_stride or -immediate(image, 0x82764BBC) != quad_stride:
        raise ValueError("Original destructor strides differ")
    return dict(allocation_bytes=allocation, unique_slot_count=quad_count+cached_count,
                quad=dict(count=quad_count, stride=quad_stride, first_offset=f"{quads[0]:08X}",
                          last_offset=f"{quads[-1]:08X}", exclusive_end=f"{quads[-1]+quad_stride:08X}"),
                cached=dict(count=cached_count, stride=cached_stride, first_offset=f"{cached[0]:08X}",
                            last_offset=f"{cached[-1]:08X}", exclusive_end=f"{cached[-1]+cached_stride:08X}"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    pools = arithmetic(image)
    mutations = 0
    mutated = bytearray(image)
    for _, begin, end, _ in SPANS:
        for address in range(begin, end):
            at = address - BASE
            mutated[at] ^= 1
            try:
                verify(mutated, check_identity=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError(f"Changed original decal byte admitted at {address:08X}")
            finally:
                mutated[at] ^= 1
    report = dict(
        schema=1, scope="Original singleton producer source/slot arithmetic only; no native execution",
        image=dict(path="analysis/simpsons.pe", bytes=IMAGE_BYTES, base=f"{BASE:08X}", sha256=IMAGE_SHA),
        spans=[dict(name=name, begin=f"{begin:08X}", exclusive_end=f"{end:08X}", bytes=end-begin, sha256=digest)
               for name, begin, end, digest in SPANS],
        instructions=[dict(address=f"{address:08X}", word=f"{word:08X}", meaning=meaning)
                      for address, (word, meaning) in INSTRUCTIONS.items()],
        pools=pools, mutation_rejections=mutations,
        acquisition="82767708/827677D8 pop existing free-chain nodes; if empty they unlink/reuse an existing group's node or return null. These complete pinned functions contain no allocation/growth path.",
        insertion="82769490 selects the two acquisitions, initializes the returned node and links it at827695B4..D8. Game draw82753508 uses singleton82DFE344;82764C20 traverses its groups. Uncorrupted nodes remain among the original1000 slots.",
        release="82764C78 group cleanup and827678B0 expiry return the same nodes to the singleton free chains.82764B58 visits all200 cached/all800 quad slots and clears the singleton;82767668 calls it and conditionally passes the owner to paired allocator free8274BB70. Source calls are not executed allocator/GPU lifetime evidence.",
        guard_assessment=dict(native_cap=65536, proven_gameplay_unique_node_max=1000,
                              verified_valid_rejection=False,
                              classification="Redundant cap for the proven singleton producer range"),
        broader_route="Generic827649E0/82764A10 insertion and82764A40 group initialization impose no count cutoff. Their caller-owned allocation domain, callbacks and paired lifetime beyond the singleton pool are unqualified; the pool proof does not bound every structurally constructible list.",
        next_regression="Allocate the genuineF2560 owner, construct it, exhaust800/200 acquisitions, insert/count/use invisible original children, perform paired group return/reacquisition and deleting destructor. Independently reject cycles/duplicate ownership/unmapped pointers/callback or material mismatch without state/pixel changes. No production/native fixture added in this wave.",
        native_lifecycle_verified=False,
    )
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to(ROOT / "build") or output == args.image.resolve():
            raise ValueError("Report output must be a separate workspace build artifact")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(json.dumps(dict(output=str(output), pools=pools, mutation_rejections=mutations,
                              native_lifecycle_verified=False), sort_keys=True))
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
