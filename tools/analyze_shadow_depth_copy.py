"""Read-only original shadow depth copy evidence; stdlib and original image only.

No arguments prints JSON. --verify suppresses JSON; --self-test runs nine focused
mutations. Optional --reference-root checks pinned local register/format headers.
No disassembler, runtime, generated code, build, report file or emulator is used.
Semantic labels below describe reviewed instructions, not recovered SDK symbols.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA = '6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0'

# Whole .pdata function extents, not guessed next-symbol boundaries. Raw hashes
# pin instructions outside the semantic anchors too, including alternate paths.
# name, start, bytes, .pdata VA, packed unwind word, original SHA256
FUNCTIONS = (
    ('helper', 0x82704BE8, 0x5C, 0x822008E0, 0x40001704,
     '346b556554440e0257d960c2ca1e4a49581d7d4481b992e8fd490fc319a179cc'),
    ('parent', 0x82707220, 0x334, 0x822009E0, 0x4000CD04,
     'cbf638faadd2e1a014ceb100843ee530ec8ae0485ba5e2bee9548905228e10c7'),
    ('draw_and_end', 0x82706F48, 0x110, 0x822009B8, 0x40004403,
     'e35366a502f478e27bde81e95b54f08cb57fb7be79ef7297b955f1a4c7239e48'),
    ('device_getter', 0x823EE8F8, 0x28, 0x821F16B0, 0x40000A03,
     'a1b0fbb2d7fb8b74622a9aa7a245124970e292a3ee24cb3c29f5af7a85c40b75'),
    ('private_depth_create', 0x823F61F8, 0xA8, 0x821F1AD8, 0x40002A03,
     '093cacb7007b2e8555d39563a9df75befb6590dc62b233a51013b4e9b6e62fc2'),
    ('shadow_constructor', 0x827064C0, 0x554, 0x82200990, 0x40015503,
     '944da8fb3533bc9a3784dfc38279e42f64f318367800bde0daf65844ddf35f4c'),
    ('surface_format', 0x8243FC38, 0x254, 0x821F3710, 0x40009503,
     'a7e37fda962359f84cfbf7d6b1c9fd2d309e422a6b0547e7f60bb50520081413'),
    ('sdk_copy', 0x82455570, 0xE44, 0x821F3E50, 0x40039105,
     'e3b994307e6e95ca72f3e9d7b184d4ec60d5e5e5cdc72978dd1c90cb66d0e26f'),
    ('temporary_scissor', 0x82439C20, 0x2E0, 0x821F35D0, 0x4000B803,
     'a06b2d898f3f60715af1950f71e6c6595a96a0cc26a5256802e646f08c581991'),
)

# BE instructions/data independently compared before interpreting operands.
ANCHORS = {
    'helper': (0x82704BE8, '''
        7d8802a6 9181fff8 fbe1fff0 9421ff80 7c7f1b78 4bce9cfd
        38a00000 3d60821e 39400000 39200000 39000000 38e00000
        90a10064 7fe6fb78 c02bd0d8 38800004 90a1005c 4bd50945
        38210080 8181fff8 7d8803a6 ebe1fff0 4e800020'''),
    'parent_frame_camera': (0x82707220, '''
        7d8802a6 48335191 dbe1ffa8 9421ff20 7c7f1b78 7c9a2378
        2f1a0000 831f05b4 419a0008 831f05b8'''),
    'parent_path': (0x827072C4, '3d6082cf 896bff5c 2b0b0000 419a0130'),
    'parent_destination': (0x82707398, '''
        7f04c378 7fe3fb78 4bfffba9 2f1a0000 409a000c 807f00f0
        48000008 807f00f4 4bffd831'''),
    'alpha_effect_camera_end': (0x82707038,
        '7f63db78 4bfff0f5 807b0018 4bfadad5 7f23cb78 4bcea9bd'),
    'private_source_path': (0x823F71D0, '''
        3d6082cd 814b1d88 2f0a0000 419a0024 3d401a22 93ab1d88
        3d6082d1 614a0197 915e0018 816bcafc 917e0000 48000068
        7fc4f378 7fe3fb78 4bffeff1'''),
    'source_format': (0x823F6204, '''
        3d601a22 7c9f2378 617c0197 3ca01828 38c00000 60a50186
        939f0018 83c30010 83a3000c 7fc4f378 7fa3eb78 4bff7701
        39600000 90610058 38e10058 38c00000 7f85e378 7fc4f378
        7fa3eb78 9161005c 91610060 4804a441'''),
    'destination_formats': (0x827065C8, '''
        3d001a22 39400003 39200000 61080197 c00bff24 3d6082cf
        d01f00c8 38e00002 38c00001 38a00001 38800400 c00bff20
        3d6082cf d01f00cc 38600400 c00bff28 d01f00d0 4bd39f6d
        3d001a22 907f00f0 39400003 39200000 61080197 38e00002
        38c00001 38a00001 38800400 38600400 4bd39f41 907f00f4'''),
    'format_selector': (0x8243FC4C, '''
        579806be 7c9b2378 7cde3378 7cfa3b78 7d1f4378 7d354b78 7d545378
        3ac00001 2b180016 419a0010 2b180017 3b200000 409a0008 7ed9b378'''),
    'format_table_use': (0x8243FDA4, '''
        3d608207 5704083c 915f0018 396ba028 815f0024 2f190000
        554a077e 7cca5378 50aa1bb8 915f0024 7d645a2e 556bc73e
        419a004c 556b83de 552a053e 2f17ffff 7d6b5378 917f001c'''),
    'format_table_entry': (0x8206A056, '1120'),
    'source_and_sample': (0x82455584, '''
        7c972378 ffc00890 7cb82b78 56ee077e 7c7f1b78 7cd33378
        7cfe3b78 7d054378 7d364b78 91c10090 7d555378 56eb0677
        4082004c 2b0e0004 409a000c 817f30a0 48000010 396e0c24
        556b103a 7d6bf82e a16b0018 556b07be 2b0b0000 409a000c
        62f70010 48000018 2b0b0001 409a000c 62f70050 48000008 62f70070'''),
    'full_region': (0x82455708, '''
        39600000 2b180000 409a001c 916100b0 3b0100b0 916100b4
        81610074 932100bc 916100b8 2b1e0000 409a000c 3d608207 3bcbad58'''),
    'default_point': (0x8206AD58, '00000000 00000000'),
    'zero_clear_value': (0x821DD0D8, '00000000'),
    'copy_format_code': (0x82455814,
        '2b1e0016 419a000c 2b1e0017 409a0008 3bc00006'),
    'command_and_clears': (0x824558BC, '''
        56ea04a5 39200001 4182000c 39200003 48000010 2b0e0004
        409a0008 39200000 72ea0377 917f2a24 5529a296 91ff2a1c
        394afff0 911f2a20 83a101e4 7d3e5378 93df2a18 e97f0020
        616b3c00 56f905ad f97f0020 41820018 7fa4eb78 80c101dc
        7fe3fb78 fc20f090 4bfff6cd 56eb05ef 41820018 57c5077e
        1020a8c3 7fa4eb78 7fe3fb78 4bfffb59'''),
    'copy_scissor': (0x82455944, '''
        39600000 81580008 82f80000 394a0007 55560038 9161007c
        8178000c 83180004 396b0007 55750038 817f28c4 556a881c
        7d4a8e70 7f175000 41980038 556b083c 7d6b8e70 7f185800
        41980028 817f28c8 556a881c 7d4a8e70 7f165000 41990014
        556b083c 7d6b8e70 7f155800 40990024 7ea7ab78 7ec6b378
        7f05c378 7ee4bb78 7fe3fb78 4bfe4259 39600001 9161007c'''),
    'restore_scissor': (0x824561F0, '''
        419a0034 817f28c8 7fe3fb78 815f28c4 5569083c 556b881c
        5548083c 7d278e70 554a881c 7d668e70 7d058e70 7d448e70 4bfe3a01'''),
    'sdk_resource_tokens': (0x82456338, '''
        815f2a9c 280a0000 917f0030 41820010 91530008 9153000c
        48000054 817f2aa0 81530000 7d6b5039 41820044'''),
}

REFERENCES = {
    'include/rex/graphics/xenos.h':
        '7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227',
    'include/rex/graphics/registers.h':
        '2ccf7732db706133acfec34f7d70659e3d12b0533bc51e8b95ca8137cc4d1c40',
}


def need(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def take(image, va, size):
    offset = va - BASE
    need(offset >= 0 and size >= 0 and offset + size <= len(image), 'Out-of-image evidence range')
    return image[offset:offset + size]


def word(image, va):
    need(not va & 3, 'Unaligned instruction')
    return struct.unpack('>I', take(image, va, 4))[0]


def signed16(value):
    return (value & 0x7FFF) - (value & 0x8000)


def immediate(image, va):
    return word(image, va) & 0xFFFF


def address_pair(image, high, low):
    return ((immediate(image, high) << 16) + signed16(immediate(image, low))) & 0xFFFFFFFF


def format_pair(image, high, low):
    return (immediate(image, high) << 16) | immediate(image, low)


def branch(image, pc):
    value = word(image, pc)
    need(value >> 26 == 18 and value & 3 == 1, 'Expected relative BL')
    delta = value & 0x03FFFFFC
    if delta & 0x02000000:
        delta -= 0x04000000
    return (pc + delta) & 0xFFFFFFFF


def rotate_mask(value, instruction):
    """Decode the few pinned RLWINM bit extractions; no instruction execution loop."""
    need(instruction >> 26 == 21, 'Expected RLWINM')
    shift, begin, end = (instruction >> 11) & 31, (instruction >> 6) & 31, (instruction >> 1) & 31
    mask = sum(1 << (31 - bit) for bit in range(32)
               if (begin <= bit <= end if begin <= end else bit >= begin or bit <= end))
    rotated = ((value << shift) | (value >> ((32 - shift) % 32))) & 0xFFFFFFFF
    return rotated & mask


def contracts(image):
    # This is also the mutation-test entry point, deliberately independent of
    # whole-image hashing so tests must reach their specific evidence anchor.
    for name, (va, raw) in ANCHORS.items():
        expected = bytes.fromhex(raw)
        need(take(image, va, len(expected)) == expected, 'Changed evidence: ' + name)

    calls = {pc: branch(image, pc) for pc in (
        0x82704BFC, 0x82704C2C, 0x827073A0, 0x827073B8, 0x8270703C,
        0x82707044, 0x8270704C, 0x823F7208, 0x823F6258, 0x8270660C,
        0x82706638, 0x82455924, 0x82455940, 0x824559C8, 0x82456220)}
    need(calls == dict(zip(calls, (
        0x823EE8F8, 0x82455570, 0x82706F48, 0x82704BE8, 0x82706130,
        0x826B4B18, 0x823F1A08, 0x823F61F8, 0x82440698, 0x82440578,
        0x82440578, 0x82454FF0, 0x82455498, 0x82439C20, 0x82439C20))), 'Call chain changed')

    source_format = format_pair(image, 0x823F6204, 0x823F620C)
    destinations = [format_pair(image, hi, lo) for hi, lo in
                    ((0x827065C8, 0x827065D4), (0x82706610, 0x82706620))]
    selector = rotate_mask(source_format, word(image, 0x8243FC4C))
    table = address_pair(image, 0x8243FDA4, 0x8243FDB0)
    table_va = table + rotate_mask(selector, word(image, 0x8243FDA8))
    entry = struct.unpack('>H', take(image, table_va, 2))[0]
    depth_info = rotate_mask(rotate_mask(entry, word(image, 0x8243FDD0)), word(image, 0x8243FDD8))
    need(source_format == 0x1A220197 and destinations == [source_format] * 2 and
         selector == 23 and table_va == 0x8206A056 and depth_info == 0x10000,
         'Source/destination floating depth format differs')

    flags = immediate(image, 0x82704C24)
    source = rotate_mask(flags, word(image, 0x82455590))
    # Source header +0x18 low two bits select MSAA. Only zero (single sample)
    # is qualified here; other original branches remain pinned but not ported.
    need(rotate_mask(flags, word(image, 0x824555B0)) == 0 and
         rotate_mask(0, word(image, 0x824555D8)) == 0, 'Explicit sample selection changed')
    normalized_flags = flags | immediate(image, 0x824555E4)
    command = immediate(image, 0x824558D8)
    control = ((normalized_flags & immediate(image, 0x824558DC)) +
               signed16(immediate(image, 0x824558EC))) | rotate_mask(command, word(image, 0x824558E4))
    need(flags == source == control == 4 and normalized_flags == 0x14 and command == 0 and
         rotate_mask(normalized_flags, word(image, 0x824558BC)) == 0 and
         rotate_mask(normalized_flags, word(image, 0x82455908)) == 0 and
         rotate_mask(normalized_flags, word(image, 0x82455928)) == 0,
         'Expected raw depth/sample0 with neither clear helper entered')

    helper_frame = -signed16(immediate(image, 0x82704BF4))
    parent_frame = -signed16(immediate(image, 0x8270722C))
    saved_owner = helper_frame + signed16(immediate(image, 0x82704BF0) & ~3) + 4
    return {
        'calls': [{'pc': f'{pc:08X}', 'target': f'{target:08X}'} for pc, target in calls.items()],
        'parent': {'entry': '82707220', 'path': 'BE8[82CEFF5C] != 0',
                   'selector': 'r26 == 0 selects O+5B4 camera / O+F0 texture; nonzero selects O+5B8 / O+F4',
                   'ordering': '82706F48: alpha, effect end, camera end; then helper at 827073B8',
                   'frame_bytes': parent_frame, 'helper_frame_bytes': helper_frame,
                   'helper_backchain_offset': helper_frame,
                   'parent_backchain_offset_from_helper_sp': helper_frame + parent_frame,
                   'saved_owner_low_word_offset': saved_owner,
                   'saved_parent_lr_offset': helper_frame - 8, 'saved_parent_lr': '827073BC'},
        'request': {'helper': '82704BE8', 'call': '82704C2C', 'sdk': '82455570', 'sdk_lr': '82704C30',
                    'lr_before_bl': '82704C00 (return from 823EE8F8)',
                    'r3': 'device returned by original 823EE8F8', 'r4': flags,
                    'r5': 0, 'r6': 'selected original O+F0 or O+F4 texture; also r31',
                    'r7': 0, 'r8': 0, 'r9': 0, 'r10': 0, 'f1_bits': '0000000000000000',
                    'stack_words': {'+5C': 0, '+64': 0},
                    'getter_side_effect': '823EE904 calls CPU cache reset 823EDD38, then loads 82D0CAF8'},
        'format': {'source_and_destinations': f'{source_format:08X}', 'texture_selector': selector,
                   'table_va': f'{table_va:08X}', 'table_be16': f'{entry:04X}',
                   'rb_depth_info_format': depth_info >> 16,
                   'meaning': 'D24FS8: 20e4 floating depth plus 8-bit stencil',
                   'private_source_condition': '82CD1D88 == 0 at 823F71D4; original 823F7208 allocation branch',
                   'source': 'currently selected SDK depth surface at device+30A0, not the destination or default copy',
                   'destinations': 'distinct one-level 1024x1024 textures created at 8270660C / 82706638',
                   'temporary_copy_format': immediate(image, 0x82455824),
                   'conversion': 'SDK selects raw depth command despite temporary color-format code6'},
        'copy': {'normalized_flags': normalized_flags, 'rb_copy_control': control,
                 'source_select': control & 7, 'sample_select': (control >> 4) & 7,
                 'command': (control >> 20) & 3, 'color_clear': bool(control & 0x100),
                 'depth_clear': bool(control & 0x200),
                 'region': [0, 0, 1024, 1024], 'destination_point': [0, 0],
                 'region_rule': 'null source rectangle uses destination dimensions; null point is original zero pair',
                 'scissor': 'SDK expands right/bottom to multiples of8; temporarily widens scissor and restores it',
                 'postconditions': 'full depth/stencil destination overwrite including border; source unchanged; scissor retained',
                 'sdk_bookkeeping': 'temporary copy state/commands, dirty groups and destination +8/+C resource-use tokens',
                 'native_obligation': 'retain both owners through GPU completion and preserve ordering/sampling visibility'},
    }


def inspect(image, reference_root=None):
    need(len(image) == IMAGE_SIZE and sha(image) == IMAGE_SHA, 'Original image identity changed')
    report = contracts(image)
    pins = []
    for name, va, size, pdata, packed, digest in FUNCTIONS:
        need(struct.unpack('>II', take(image, pdata, 8)) == (va, packed) and
             ((packed >> 8) & 0x3FFFFF) * 4 == size, 'Changed .pdata extent: ' + name)
        need(sha(take(image, va, size)) == digest, 'Changed original function: ' + name)
        pins.append({'name': name, 'va': f'{va:08X}', 'bytes': size,
                     'pdata_va': f'{pdata:08X}', 'pdata_word': f'{packed:08X}', 'sha256': digest})
    refs = []
    for relative, digest in REFERENCES.items():
        if reference_root is not None:
            need(sha((reference_root / relative).read_bytes()) == digest, 'Pinned reference changed: ' + relative)
        refs.append({'relative_path': relative, 'sha256': digest, 'checked': reference_root is not None})
    report.update({'image_sha256': IMAGE_SHA, 'functions': pins,
                   'raw_evidence': {name: {'va': f'{va:08X}', 'hex': ''.join(raw.split())}
                                    for name, (va, raw) in ANCHORS.items()},
                   'optional_references': refs,
                   'reference_meanings': 'xenos: kD24FS8=1, k_24_8_FLOAT=23, CopyCommand raw=0; registers: RB_COPY_CONTROL fields',
                   'scope': ['Static original evidence, not a runtime adapter or GPU correctness test.',
                             'Only the nonzero parent-mode helper path and single-sample resources are qualified.',
                             'Does not qualify alternate parent direct SDK calls, other flags, mips, slices or partial rectangles.',
                             'Existing decoded 20e4 float/stencil backing can be copied without another reversal or quantization.']})
    return report


def self_test(image):
    # Each mutation reaches a specific semantic anchor, not just the image hash.
    cases = (
        ('helper', 0x82704C24, '38800000'),                       # color source instead of depth
        ('parent_destination', 0x827073B4, '807f00f0'),           # alias both destination slots
        ('source_format', 0x823F620C, '617c0196'),               # fixed-point source depth
        ('destination_formats', 0x82706620, '61080196'),         # mismatched destination depth
        ('source_and_sample', 0x824555E4, '62f70020'),            # sample1 instead of sample0
        ('command_and_clears', 0x824558D8, '39200001'),           # conversion command
        ('command_and_clears', 0x82455910, '40820018'),           # enter clear path with flags4
        ('format_table_entry', 0x8206A056, '1020'),              # remove floating-depth bit
        ('alpha_effect_camera_end', 0x8270704C, '4bcea9b9'),     # change original camera-end call
    )
    for anchor, va, replacement in cases:
        changed = bytearray(image)
        data = bytes.fromhex(replacement)
        need(take(image, va, len(data)) != data, 'Stale mutation fixture')
        changed[va - BASE:va - BASE + len(data)] = data
        try:
            contracts(changed)
        except ValueError as error:
            need(str(error) == 'Changed evidence: ' + anchor, 'Mutation failed outside its intended anchor')
        else:
            raise ValueError('Accepted mutation: ' + anchor)
    print(f'PASS {len(cases)} focused original-evidence mutations')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true', help='Verify pins without JSON output')
    parser.add_argument('--self-test', action='store_true', help='Reject nine targeted evidence mutations')
    parser.add_argument('--image', type=Path, default=ROOT / 'analysis/simpsons.pe')
    parser.add_argument('--reference-root', type=Path, help='Optional local RexGlue root; verify both pinned headers')
    args = parser.parse_args()
    try:
        image = args.image.read_bytes()
        report = inspect(image, args.reference_root)
        if args.self_test:
            self_test(image)
        if not args.verify:
            print(json.dumps(report, indent=2))
        print('PASS original shadow depth copy: exact helper/parent, D24FS8 source/destinations, raw sample0, no clears')
    except (OSError, ValueError) as error:
        parser.exit(1, f'FAIL {error}\n')


if __name__ == '__main__':
    main()
