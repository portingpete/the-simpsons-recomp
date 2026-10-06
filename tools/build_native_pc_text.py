"""Replace physical Xbox 360 references in the retail English UI text.

The original LH2 lookup hashes, offsets, extent and unrelated strings stay
intact. Shorter PC wording fits the existing string slots. Outputs are separate
native assets; the user's retail packages are only read.
"""
from pathlib import Path
import argparse
import hashlib
import struct
from inspect_assets import stoc_entries, decode_entry, resource_chunks, align

ROOT = Path(__file__).resolve().parents[1]
TEXT_ROOTS = ('bargainbin', 'bigsuperhappy', 'brt', 'cheater', 'colossaldonut',
              'dayofthedolphins', 'dayspringfieldstoodstill', 'eighty_bites',
              'frontend', 'gamehub', 'grand_theft_scratchy', 'loc',
              'medal_of_homer', 'meetthyplayer', 'mob_rules', 'neverquest',
              'rhymes', 'simpsons_chars', 'spr_hub', 'tree_hugger')
TEXT_PACKAGES = tuple(stage+'/text/e172a05c.str' for stage in TEXT_ROOTS)
PACKAGE_SHA = '0ce9379a0619308e1e202509d05d905dc5883efc205b28652362fee3204aebf8'
LH2_SHA = '5a7921d5dda95e58ab145f40d05d1c306d28b2acf857c7f3d2b99af0d0161310'
REPLACEMENTS = ((b'Xbox 360 console', b'PC'),
                (b'Xbox 360 storage device', b'PC save folder'),
                (b'Xbox 360 Memory Unit', b'storage device'),
                (b'Your Xbox 360', b'Your PC'))
EXPECTED_CHANGED_STRINGS = 14


def u(data, at):
    return struct.unpack_from('>I', data, at)[0]


def text_slots(payload):
    """Validate the observed LCH2 hash/offset tables and each terminated slot."""
    if len(payload) < 32 or payload[:4] != b'2HCL' or u(payload, 4) != len(payload):
        raise ValueError('Unsupported LH2 header or extent')
    if tuple(u(payload, at) for at in (8, 12, 20, 24, 28)) != (1, 0, 1, 0, 0):
        raise ValueError('Unsupported LH2 layout')
    count = u(payload, 16)
    pool = 32+count*8
    if not count or pool >= len(payload):
        raise ValueError('Invalid LH2 table extent')
    hashes = [u(payload, 32+4*i) for i in range(count)]
    if hashes != sorted(set(hashes)):
        raise ValueError('LH2 hashes must be unique and sorted')
    starts = [u(payload, 32+count*4+4*i) for i in range(count)]
    if starts[0] != pool or starts != sorted(set(starts)) or starts[-1] >= len(payload):
        raise ValueError('Invalid LH2 string offsets')
    slots = []
    for start, end in zip(starts, [*starts[1:], len(payload)]):
        terminator = payload.find(b'\0', start, end)
        if terminator < 0 or any(payload[terminator:end]):
            raise ValueError('Invalid LH2 string termination or padding')
        slots.append((start, end, payload[start:terminator]))
    return slots


def patch_text(payload):
    if hashlib.sha256(payload).hexdigest() != LH2_SHA:
        raise ValueError('Unsupported retail English LH2 identity')
    output = bytearray(payload)
    changed = 0
    for start, end, original in text_slots(payload):
        pc = original
        for old, new in REPLACEMENTS:
            pc = pc.replace(old, new)
        if pc != original:
            if len(pc) >= end-start:
                raise ValueError('PC wording exceeds the authored string slot')
            output[start:end] = pc+b'\0'*(end-start-len(pc))
            changed += 1
    if changed != EXPECTED_CHANGED_STRINGS or b'Xbox 360' in output:
        raise ValueError('Retail physical platform wording changed unexpectedly')
    text_slots(output)
    return bytes(output)


def build(source):
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != PACKAGE_SHA:
        raise ValueError('Unsupported retail English text package identity')
    entries = stoc_entries(data)['entries']
    if len(entries) != 1:
        raise ValueError('Unsupported English text package entry count')
    entry = entries[0]
    decoded, _ = decode_entry(data, entry)
    chunks = resource_chunks(decoded)
    if len(chunks) != 1 or chunks[0].get('name') != 'simpsons_global.en.LH2':
        raise ValueError('Original global English resource missing')
    chunk = chunks[0]
    at, size = chunk['payload_decoded_offset'], chunk['payload_size']
    payload = patch_text(decoded[at:at+size])
    changed = decoded[:at]+payload+decoded[at+size:]
    stored = align(len(changed), 2048)
    output = bytearray(data[:entry['file_offset']])
    struct.pack_into('>5I', output, entry['table_offset']+4,
                     0x0eac15c8, len(changed), stored, stored, 0)
    output.extend(changed)
    output.extend(b'\0'*(stored-len(changed)))
    stoc_entries(output)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    for package in TEXT_PACKAGES:
        output = build(ROOT/'Simpsons Game, The (USA)'/package)
        target = args.output_root/package
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != output:
            target.write_bytes(output)
    print('Native PC wording: 14 strings in', len(TEXT_PACKAGES), 'English packages')


if __name__ == '__main__':
    main()
