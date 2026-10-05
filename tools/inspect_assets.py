#!/usr/bin/env python3
"""Read-only, stdlib asset inspection; format scope and evidence in docs/assets.md."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import stat
import struct
import sys


class ParseError(ValueError):
    """Malformed input or an unsupported variant of a parsed format."""


def require(condition, message):
    if not condition:
        raise ParseError(message)


def span(data, offset, size):
    require(offset >= 0 and size >= 0 and offset + size <= len(data),
            f"out-of-bounds span at 0x{offset:x}, size {size}")
    return data[offset:offset + size]


def u32(data, offset, endian=">"):
    return struct.unpack(endian + "I", span(data, offset, 4))[0]


def align(value, boundary):
    return (value + boundary - 1) // boundary * boundary


def refpack(data, expected):
    """Decode observed 10 FB / BE24 variant; return bytes and consumed length.

    Command layout cross-checked against SSXModding/bigfile RefPack.cpp.
    Sizes, every read, backreferences, output budget and stop code are checked.
    Container padding is deliberately left to the caller.
    """
    require(span(data, 0, 2) == b"\x10\xfb", "unsupported RefPack signature")
    require(int.from_bytes(span(data, 2, 3), "big") == expected,
            "RefPack size disagrees with SToc")
    require(0 < expected <= 0xffffff, "invalid RefPack output size")
    pos, out = 5, bytearray()
    while True:
        command = span(data, pos, 1)[0]
        pos += 1
        count = distance = 0
        stop = command >= 0xfc
        if command < 0x80:
            a = span(data, pos, 1)[0]
            pos += 1
            literal = command & 3
            count = ((command >> 2) & 7) + 3
            distance = ((command & 0x60) << 3) + a + 1
        elif command < 0xc0:
            a, b = span(data, pos, 2)
            pos += 2
            literal = a >> 6
            count = (command & 0x3f) + 4
            distance = ((a & 0x3f) << 8) + b + 1
        elif command < 0xe0:
            a, b, c = span(data, pos, 3)
            pos += 3
            literal = command & 3
            count = ((command & 0x0c) << 6) + c + 5
            distance = ((command & 0x10) << 12) + (a << 8) + b + 1
        else:
            literal = command & 3 if stop else ((command & 0x1f) + 1) * 4
        require(len(out) + literal + count <= expected, "RefPack output overrun")
        out.extend(span(data, pos, literal))
        pos += literal
        if count:
            require(distance <= len(out), "RefPack backreference before output")
            # Repetition implements overlapping copies without a per-byte loop.
            seed = out[len(out) - distance:len(out) - distance + min(count, distance)]
            out.extend((seed * ((count + len(seed) - 1) // len(seed)))[:count])
        if stop:
            require(len(out) == expected, "RefPack premature stop")
            return bytes(out), pos


def stoc_entries(data):
    require(span(data, 0, 8) == b"SToc\0\0\0\x07", "unsupported SToc header")
    require(span(data, 9, 3) == b"\x04\0\0", "unsupported SToc bytes 9..11")
    count = data[8]  # Observed corpus profile, not a claim about other variants.
    metadata, table = u32(data, 12), u32(data, 16)
    require(metadata in (0, 20), "unsupported SToc metadata offset")
    require(table >= 20 and table % 4 == 0, "invalid SToc table offset")
    require(metadata != 0 or table == 20, "unexpected SToc metadata span")
    end = table + count * 24
    position = align(end, 2048)
    require(not any(span(data, end, position - end)), "nonzero SToc table padding")
    entries = []
    for index in range(count):
        offset = table + index * 24
        tag, storage, size, word12, stored, word20 = struct.unpack(">6I", span(data, offset, 24))
        require(storage in (0x0eac15c8, 0xb9f0b9ec), "unsupported SToc storage tag")
        require(size > 0 and stored > 0 and stored % 2048 == 0,
                "invalid SToc size/alignment")
        payload = span(data, position, stored)
        if storage == 0x0eac15c8:
            require(size <= stored, "raw SToc payload exceeds stored span")
        else:
            require(payload[:2] == b"\x10\xfb" and
                    int.from_bytes(payload[2:5], "big") == size,
                    "invalid SToc RefPack header/size")
        entries.append({"index": index, "table_offset": offset,
                        "tag_hex": f"{tag:08x}", "storage_tag_hex": f"{storage:08x}",
                        "decoded_size": size, "word_12": word12,
                        "stored_size": stored, "word_20": word20,
                        "file_offset": position,
                        "encoding": "refpack_10fb" if storage == 0xb9f0b9ec else "raw"})
        position += stored
    require(position == len(data), "SToc spans do not end at EOF")
    return {"format": "SToc_observed_v7_profile", "entry_count": count,
            "metadata_offset": metadata, "table_offset": table, "entries": entries}


def padded_string(data, offset):
    length = u32(data, offset)
    require(length > 0 and length % 4 == 0, "invalid resource string span")
    value = span(data, offset + 4, length)
    end = value.find(b"\0")
    require(end >= 0 and all(32 <= c <= 126 for c in value[:end]),
            "invalid ASCII resource string")
    require(all(c == 0xbf for c in value[end + 1:]), "invalid resource string padding")
    return value[:end].decode("ascii"), offset + 4 + length


def stream_toc(data):
    """Observed StreamTOC v9 layout. All offsets are relative to its payload.

    Record links are structural evidence, not an assertion of load-order ABI.
    Three-byte descriptors are independently matched to referenced SToc rows.
    """
    require(span(data, 0, 8) == bytes.fromhex("9cba7b2800000009"), "unsupported StreamTOC header")
    strings_offset, links_offset = u32(data, 8), u32(data, 12)
    total, references = struct.unpack(">HH", span(data, 16, 4))
    tag_count, tags_offset = u32(data, 20), u32(data, 24)
    require(total == references + 1 and total > 1, "StreamTOC record count mismatch")
    require(links_offset == 28 + total * 24, "StreamTOC record table end mismatch")
    require(0 < tag_count <= 256 and tags_offset % 4 == 0, "invalid StreamTOC tag table")
    tags = list(struct.unpack(">" + "I" * tag_count, span(data, tags_offset, tag_count * 4)))
    require(strings_offset == tags_offset + tag_count * 4 + 1 and
            span(data, strings_offset - 1, 1) == b"\0", "StreamTOC string-pool boundary mismatch")
    strings, pos = {}, strings_offset
    while pos < len(data):
        end = data.find(b"\0", pos)
        require(end > pos, "unterminated/empty StreamTOC string")
        value = data[pos:end]
        require(all(32 <= c <= 126 for c in value), "invalid StreamTOC string bytes")
        name = value.decode("ascii")
        require(not name.startswith(("/", "\\")) and ":" not in name and
                all(p not in ("", ".", "..") for p in name.replace("\\", "/").split("/")),
                "unsafe StreamTOC relative name")
        strings[pos] = name
        pos = end + 1
    require(len(strings) == total, "StreamTOC string count mismatch")
    link_words = list(struct.unpack(">" + "I" * references, span(data, links_offset, references * 4)))
    for target in link_words:
        require(28 <= target < links_offset and (target - 28) % 24 == 0,
                "StreamTOC link does not target a record boundary")
    cursor = links_offset + references * 4
    records, used_names, ids = [], set(), []
    for index in range(total):
        offset = 28 + index * 24
        ident, name_offset, packed, reserved, link, blocks = struct.unpack(">6I", span(data, offset, 24))
        require(name_offset in strings and name_offset not in used_names, "invalid/duplicate StreamTOC name reference")
        used_names.add(name_offset)
        item = {"index": index, "record_offset": offset, "word_00_hex": f"{ident:08x}",
                "name_offset": name_offset, "name": strings[name_offset], "word_08": packed,
                "word_12": reserved, "link_offset": link, "block_metadata_offset": blocks}
        if index == 0:
            require((ident, packed, reserved, link, blocks) == (0, 0, 0, 0, 0),
                    "unsupported StreamTOC self record")
            item["is_self_record"] = True
        else:
            require(packed & 0xffff00ff == 0x01000000 and reserved == 0,
                    "unsupported StreamTOC reference flags")
            count = (packed >> 8) & 255
            require(links_offset <= link < links_offset + references * 4 and (link - links_offset) % 4 == 0,
                    "invalid StreamTOC link-list offset")
            require(blocks == (cursor if count else 0), "StreamTOC block metadata offset/count mismatch")
            entries = []
            for _ in range(count):
                units, tag_index = struct.unpack(">HB", span(data, cursor, 3))
                require(tag_index < tag_count, "StreamTOC tag index out of bounds")
                entries.append({"stoc_word_12": units * 256, "tag_index": tag_index,
                                "tag_hex": f"{tags[tag_index]:08x}"})
                cursor += 3
            target = link_words[(link - links_offset) // 4]
            item.update({"entry_count": count, "linked_record_index": (target - 28) // 24,
                         "entries": entries})
            ids.append(ident)
        records.append(item)
    require(ids == sorted(set(ids)), "StreamTOC reference words are not unique/increasing")
    require(tags_offset == align(cursor, 4) and not any(span(data, cursor, tags_offset - cursor)),
            "StreamTOC block metadata end/padding mismatch")
    for item in records[1:]:
        target, visited = item["index"], set()
        while target:
            require(target not in visited, "cyclic StreamTOC record links")
            visited.add(target)
            target = records[target]["linked_record_index"]
    return {"format": "StreamTOC_observed_v9_profile", "package_name": strings[strings_offset],
            "record_count": total, "reference_count": references, "links_offset": links_offset,
            "link_record_offsets": link_words, "tags_offset": tags_offset,
            "tags_hex": [f"{t:08x}" for t in tags], "strings_offset": strings_offset,
            "records": records, "validation": "bounded_profile_and_acyclic_record_links",
            "unverified": "Record-link runtime meaning, word_00 hash algorithm and load-order ABI."}


def resource_chunks(data):
    """Observed LE chunk envelope and BE 0x716 resource descriptor only."""
    chunks, offset = [], 0
    while offset < len(data):
        tag, size, version = struct.unpack("<3I", span(data, offset, 12))
        body = span(data, offset + 12, size)
        require(version == 0x1802ffff, "unsupported resource chunk version")
        item = {"decoded_offset": offset, "tag_hex": f"{tag:08x}",
                "body_size": size, "version_hex": f"{version:08x}"}
        if tag == 0x716:
            descriptor_size = u32(body, 0)
            descriptor = span(body, 4, descriptor_size)
            name, pos = padded_string(descriptor, 0)
            words = list(struct.unpack(">4I", span(descriptor, pos, 16)))
            pos += 16
            strings = []
            for _ in range(3):
                value, pos = padded_string(descriptor, pos)
                strings.append(value)
            require(pos + 8 <= len(descriptor), "resource descriptor length mismatch")
            unknown, payload_size = u32(descriptor, pos), u32(descriptor, pos + 4)
            require(not any(descriptor[pos + 8:]), "nonzero resource descriptor padding")
            payload_offset = 4 + descriptor_size
            payload = span(body, payload_offset, payload_size)
            require(align(payload_offset + payload_size, 4) == len(body),
                    "resource payload length mismatch")
            item.update({"name": name, "type_name": strings[0],
                         "source_path": strings[1], "extra_string": strings[2],
                         "descriptor_size": descriptor_size, "id_words_be": words,
                         "descriptor_tail_word": unknown, "payload_size": payload_size,
                         "payload_decoded_offset": offset + 12 + payload_offset,
                         "payload_header_hex": payload[:32].hex(),
                         "payload_sha256": hashlib.sha256(payload).hexdigest()})
            if strings[0] == "StreamTOC":
                item["stream_toc"] = stream_toc(payload)
        else:
            item["status"] = "opaque_chunk_body"
            item["body_header_hex"] = body[:32].hex()
        chunks.append(item)
        offset += 12 + size
    return chunks


def decode_entry(data, entry):
    """Decode exactly one previously validated SToc entry, checking its padding."""
    raw = span(data, entry["file_offset"], entry["stored_size"])
    decoded, consumed = refpack(raw, entry["decoded_size"]) if entry["encoding"] == "refpack_10fb" else (
        raw[:entry["decoded_size"]], entry["decoded_size"])
    require(not any(raw[consumed:]), "nonzero SToc payload padding")
    return decoded, consumed


def inspect_str(data, decode, cache):
    result = stoc_entries(data)
    start, end = result["metadata_offset"], result["table_offset"]
    result["metadata_ascii_evidence"] = [
        {"file_offset": start + m.start(), "text": m.group().decode("ascii")}
        for m in re.finditer(rb"[ -~]{4,}", span(data, start, end - start))
    ] if start else []
    result["validation"] = "decoded_resources" if decode else "table_and_payload_headers_only"
    if decode:
        for entry in result["entries"]:
            raw = span(data, entry["file_offset"], entry["stored_size"])
            key = (entry["encoding"], entry["decoded_size"], hashlib.sha256(raw).digest())
            if key not in cache:
                decoded, consumed = decode_entry(data, entry)
                cache[key] = {"encoded_bytes_consumed": consumed,
                              "decoded_sha256": hashlib.sha256(decoded).hexdigest(),
                              "chunks": resource_chunks(decoded)}
            entry.update(cache[key])
    return result


def inspect_lua(data):
    require(not data.startswith(b"\x1bLua"), "Lua bytecode is outside text inspector scope")
    try:
        source = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ParseError("invalid UTF-8 Lua text") from exc
    require(all(c >= " " or c in "\r\n\t" for c in source), "non-text Lua bytes")
    return {"format": "UTF-8_source_text", "validation": "text_only_not_Lua_syntax",
            "line_count": len(source.splitlines()),
            "lexical_evidence": [{"line": i, "text": line.strip()}
                                 for i, line in enumerate(source.splitlines(), 1)
                                 if not line.lstrip().startswith("--") and re.search(
                                     r"MapPkg:New|MoviePkg:New|tolua\.takeownership|NewMap\(|NewMovie\(|ScoreKeeper:|GameFlowManager:", line)]}


def eaac_header(data, offset):
    first, second = u32(data, offset), u32(data, offset + 4)
    result = {"header_offset": offset, "version": first >> 28,
              "codec_id": (first >> 24) & 15, "codec": "EA-XMA",
              "channels": ((first >> 18) & 63) + 1, "sample_rate": first & 0x3ffff,
              "type": second >> 30, "loop": bool((second >> 29) & 1),
              "samples": second & 0x1fffffff}
    require(result["version"] == 0 and result["codec_id"] == 3 and result["type"] == 1,
            "unsupported EAAC version/codec/type")
    require(0 < result["sample_rate"] <= 200000 and result["samples"] > 0,
            "invalid EAAC rate/sample count")
    return result


def eaac_blocks(data, start, end, header):
    span(data, start, end - start)
    require(start < end, "empty EAAC extent")
    offset, count, samples, layers = start, 0, 0, 0
    segments = []
    while offset < end:
        word = u32(data, offset)
        flag, length = word >> 24, word & 0xffffff
        require(flag in (0, 0x80) and length >= 8 and offset + length <= end,
                f"invalid EAAC block at 0x{offset:x}")
        samples += u32(data, offset + 4)
        pos = offset + 8
        for _ in range((header["channels"] + 1) // 2):
            require(pos + 4 <= offset + length, "truncated EA-XMA layer word")
            value = u32(data, pos)
            layer_length = value >> 2
            require(value & 3 == 3 and layer_length >= 8 and pos + layer_length <= offset + length,
                    "invalid EA-XMA layer span")
            require(span(data, pos + 4, 4) == b"\x08\0\0\0", "unsupported EA-XMA layer prefix")
            pos += layer_length
            layers += 1
        padding = offset + length - pos
        require((padding == 0 or (flag == 0x80 and padding <= 63)) and
                not any(span(data, pos, padding)), "invalid EAAC block padding")
        offset += length
        count += 1
        if flag == 0x80:
            segments.append({"end_offset": offset, "cumulative_samples": samples})
    require(samples == header["samples"], "EAAC block/header sample count mismatch")
    require(segments and segments[-1]["end_offset"] == end, "EAAC missing final segment end")
    require(len(segments) == (2 if header["loop"] else 1), "unsupported EAAC segment structure")
    return {"audio_offset": start, "audio_size": end - start, "block_count": count,
            "layer_count": layers, "sample_sum": samples, "segments": segments,
            "first_block_header_hex": span(data, start, min(32, end - start)).hex(),
            "validation": "block_and_layer_spans_and_samples_only_not_XMA_bitstream"}


def inspect_snu(data):
    require(span(data, 0, 4) in (b"\x01\0\0\0", b"\x01\x04\0\0", b"\x01\x08\0\0", b"\x01\x0c\0\0"),
            "unsupported SNU wrapper prefix")
    require(u32(data, 12) == 0, "unsupported SNU word at 0x0c")
    header = eaac_header(data, 16)
    start = u32(data, 8)
    require(start >= (32 if header["loop"] else 24) and start % 16 == 0, "invalid SNU audio offset")
    blocks = eaac_blocks(data, start, len(data), header)
    if header["loop"]:
        header.update({"loop_start_sample": u32(data, 24), "loop_offset_relative": u32(data, 28)})
        first_end = blocks["segments"][0]
        require(first_end["end_offset"] == start + header["loop_offset_relative"] and
                first_end["cumulative_samples"] == header["loop_start_sample"], "SNU loop boundary mismatch")
    return {"format": "SNU_EAAC", "wrapper_word_04": u32(data, 4),
            "metadata_status": "wrapper-to-audio region otherwise opaque", "header": header, "audio": blocks}


def inspect_mus(data):
    count = u32(data, 4, "<")
    require(0 < count <= 65536, "invalid MUS entry count")
    table_end = 40 + count * 28
    span(data, 0, table_end)
    streams, ranges = [], []
    for index in range(count):
        pos = 40 + index * 28
        ordinal, zero = struct.unpack(">HH", span(data, pos + 4, 4))
        head = u32(data, pos + 8) * 16
        start, length = u32(data, pos + 12) * 128, u32(data, pos + 20)
        require(ordinal == index and zero == 0 and u32(data, pos + 16) == 8 and
                u32(data, pos + 24) == 0, "unsupported MUS record")
        require(not any(span(data, head + 8, 8)), "nonzero MUS header-slot padding")
        header = eaac_header(data, head)
        require(not header["loop"], "looping MUS records unsupported")
        streams.append({"index": index, "table_offset": pos, "id_hex": span(data, pos, 4).hex(),
                        "header": header, "audio": eaac_blocks(data, start, start + length, header)})
        ranges.extend(((head, head + 16), (start, start + length)))
    previous = table_end
    for start, end in sorted(ranges):
        require(start >= previous, "overlapping MUS table/header/audio spans")
        require(not any(span(data, previous, start - previous)), "nonzero MUS inter-span padding")
        previous = end
    require(len(data) == align(previous, 128), "invalid MUS EOF alignment/padding span")
    require(not any(span(data, previous, len(data) - previous)), "nonzero MUS EOF padding")
    return {"format": "MUS_EAAC_index", "entry_count_audio": count,
            "outer_header_hex": span(data, 0, 40).hex(), "streams": streams}


def inspect_vp6(data):
    require(span(data, 0, 16) == b"SCHl\x28\0\0\0GSTR\x01\0\0\0", "unsupported EA movie header")
    tags, pos = {}, 16
    while pos < 40:
        tag = span(data, pos, 1)[0]
        pos += 1
        if tag == 0xff:
            break
        if tag == 0xfd:
            continue
        length = span(data, pos, 1)[0]
        pos += 1
        require(0 < length <= 4 and pos + length <= 40 and tag not in tags, "invalid SCHl TLV")
        tags[tag] = int.from_bytes(span(data, pos, length), "big")
        pos += length
    require(tag == 0xff and not any(span(data, pos, 40 - pos)), "invalid SCHl termination/padding")
    require(tags.get(0x80) == 3 and tags.get(0x82) == 2 and tags.get(0x85, 0) > 0,
            "unsupported EA movie audio header")
    require(0x83 not in tags and 0xa0 not in tags, "unsupported EA movie codec override")
    require(0 < tags.get(0x84, 48000) <= 200000, "invalid EA movie audio rate")
    require(span(data, 40, 12) == b"MVhd\x20\0\0\0vp60", "unsupported MVhd")
    width, height = struct.unpack("<HH", span(data, 52, 4))
    frames, rate_num, rate_den = u32(data, 56, "<"), u32(data, 64, "<"), u32(data, 68, "<")
    require(min(width, height, frames, rate_num, rate_den) > 0, "invalid MVhd dimensions/count/rate")
    require(span(data, 72, 8) == b"SCCl\x0c\0\0\0", "missing SCCl")
    result = {"format": "EA_chunked_VP6", "video_codec": "VP6 (vp60)",
              "audio_codec": "EA ADPCM revision 3 (FFmpeg interpretation)",
              "audio_channels": tags[0x82], "audio_sample_rate_interpreted": tags.get(0x84, 48000),
              "audio_rate_basis": "SCHl tag 84" if 0x84 in tags else "FFmpeg revision-3 default; not an explicit rate field",
              "width": width, "height": height, "frames_declared": frames,
              "rate_numerator": rate_num, "rate_denominator": rate_den,
              "mvhd_word_14": u32(data, 60, "<"),
              "schl_tlvs": {f"{k:02x}": v for k, v in tags.items()},
              "audio_blocks_declared": u32(data, 80)}
    offset, samples, audio_ended = 84, 0, False
    counts = Counter({"SCHl": 1, "MVhd": 1, "SCCl": 1})
    while offset < len(data):
        tag = span(data, offset, min(4, len(data) - offset))
        if tag not in (b"MV0K", b"MV0F", b"SCDl", b"SCEl"):
            result.update({"status": "rejected", "error": f"unparsed movie trailer/chunk at 0x{offset:x}",
                           "unparsed_tail": {"file_offset": offset, "size": len(data) - offset,
                                             "header_hex": data[offset:offset + 32].hex()}})
            break
        length = u32(data, offset + 4, "<")
        require(length >= 8, "invalid EA movie chunk length")
        block = span(data, offset, length)
        if tag in (b"MV0K", b"MV0F"):
            require(length > 8, "empty EA movie video packet")
        if tag == b"SCDl":
            require(not audio_ended, "EA movie audio packet after SCEl")
            count = u32(block, 8)
            left, right = u32(block, 12) + 20, u32(block, 16) + 20
            require(count > 0 and 20 <= left < right < length, "invalid EA movie audio offsets")
            # Channel codec payloads remain opaque; offsets and sample totals are validated.
            samples += count
        if tag == b"SCEl":
            require(length == 8 and not audio_ended, "unexpected/repeated SCEl")
            audio_ended = True
        counts[tag.decode("ascii")] += 1
        offset += length
    require(counts["MV0K"] + counts["MV0F"] == frames and counts["SCDl"] == result["audio_blocks_declared"] and
            samples == tags[0x85] and counts["SCEl"] == 1, "EA movie declared counts/sample sum mismatch")
    result.update({"chunk_counts": dict(counts), "audio_sample_sum": samples,
                   "parsed_end_offset": offset, "validation": "chunk_spans_audio_offsets_and_counts_only_not_codec_bitstreams"})
    return result


def inspect_media(data, extension):
    return {".snu": inspect_snu, ".mus": inspect_mus, ".vp6": inspect_vp6}[extension](data)


def inventory(root):
    """Refuse links/junctions/special files so the input scope cannot silently expand."""
    paths = []
    pending = [root]
    while pending:
        directory = pending.pop()
        for path in sorted(directory.iterdir(), key=lambda p: p.name):
            info = path.lstat()
            require(not stat.S_ISLNK(info.st_mode) and not
                    (getattr(info, "st_file_attributes", 0) & 0x400),
                    f"links/reparse points are unsupported: {path.relative_to(root).as_posix()}")
            if stat.S_ISDIR(info.st_mode):
                pending.append(path)
            else:
                require(stat.S_ISREG(info.st_mode), "non-regular input file")
                paths.append(path)
    return sorted(paths, key=lambda p: p.relative_to(root).as_posix())


def inspect_image(data, layout):
    """Inspect .rdata only; no executable decoding, loading, or shader compilation.

    A flat XEX-derived image uses RVA as file offset. An ordinary PE uses each
    section's PointerToRawData. Do not silently infer one from the other.
    Other sections may be absent in a derived image; they are not parsed here.
    """
    require(span(data, 0, 2) == b"MZ", "missing image MZ signature")
    pe = u32(data, 60, "<")
    require(span(data, pe, 4) == b"PE\0\0", "missing PE signature")
    machine, count = struct.unpack("<HH", span(data, pe + 4, 4))
    optional_size = struct.unpack("<H", span(data, pe + 20, 2))[0]
    require(0 < count <= 96 and optional_size >= 96, "invalid PE header sizes")
    optional = span(data, pe + 24, optional_size)
    require(optional[:2] == b"\x0b\x01", "only PE32 images are supported")
    base = u32(optional, 28, "<")
    table = span(data, pe + 24 + optional_size, count * 40)
    sections = []
    for i in range(count):
        row = table[i * 40:(i + 1) * 40]
        name = row[:8].split(b"\0", 1)[0]
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<4I", row, 8)
        if name == b".rdata":
            offset = rva if layout == "flat" else raw_offset
            size = virtual_size if layout == "flat" else min(virtual_size, raw_size)
            require(size > 0 and base + rva + size <= 0x100000000,
                    "invalid .rdata address span")
            span(data, offset, size)
            sections.append({"name": ".rdata", "file_offset": offset, "size": size,
                             "virtual_address": base + rva})
    require(len(sections) == 1, "expected exactly one backed .rdata section")
    section = sections[0]
    region = span(data, section["file_offset"], section["size"])
    sources, descriptions, boundary = [], [], []
    for match in re.finditer(rb"[\x09\x0a\x0d\x20-\x7e]{8,}", region):
        value = match.group()
        text = value.decode("ascii")
        evidence = {"file_offset": section["file_offset"] + match.start(),
                    "virtual_address": section["virtual_address"] + match.start(),
                    "size": len(value), "sha256": hashlib.sha256(value).hexdigest(),
                    "nul_terminated": region[match.end():match.end() + 1] == b"\0",
                    "text": text}
        # Require syntax tokens, not just the English word 'shader'. These are
        # source candidates; text extraction does not establish compilation.
        if b"\n" in value and re.search(rb"\b(?:float[234]|VS_OUTPUT|sampler\w*)\b", value) and (
                b"{" in value and b"}" in value and b";" in value):
            sources.append(evidence)
        elif re.search(rb"shader", value, re.I) and (b"Simpsons " in value or b"\n" in value):
            if b"Simpsons " in value:
                evidence["simpsons_phrase_virtual_address"] = evidence["virtual_address"] + value.index(b"Simpsons ")
            descriptions.append(evidence)
        if re.search(rb"StreamTOC|EARS_ITXD|frontend\\frontend\.str|spr_hub\.str", value):
            boundary.append(evidence)
    return {"format": "PE32_rdata_text_inventory", "layout": layout,
            "machine_hex": f"{machine:04x}", "image_base": base,
            "scan_sections": sections, "shader_source_candidates": sources,
            "shader_description_or_diagnostic_strings": descriptions,
            "simpsons_description_count": sum("simpsons_phrase_virtual_address" in x for x in descriptions),
            "resource_boundary_strings": boundary,
            "limits": ["Only backed .rdata scanned; no shader binary disassembly or compilation.",
                       "Descriptions and diagnostics are not recovered shader programs.",
                       "Input image derivation is external; SHA-256 identifies the supplied bytes."]}


def inspect_file(path, parser):
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        mapping = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) if before.st_size else None
        try:
            data = mapping if mapping is not None else b""
            item = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                    "header_hex": data[:32].hex()}
            try:
                item["inspection"] = parser(data)
            except ParseError as exc:
                item["inspection"] = {"status": "rejected", "error": str(exc)}
            after = os.fstat(stream.fileno())
            require((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns),
                    "input changed during inspection")
            return item
        finally:
            if mapping is not None:
                mapping.close()


def summarize(files):
    extensions, directories = defaultdict(lambda: [0, 0]), defaultdict(lambda: [0, 0])
    formats, types, chunk_tags, encodings = Counter(), Counter(), Counter(), Counter()
    decoded_bytes = entries = empty = toc_count = toc_references = 0
    manifest = hashlib.sha256()
    errors = []
    for item in files:
        for group, key in ((extensions, item["extension"]),
                           (directories, item["path"].split("/")[0] if "/" in item["path"] else "<root>")):
            group[key][0] += 1
            group[key][1] += item["size"]
        manifest.update((item["path"] + "\0" + str(item["size"]) + "\0" + item["sha256"] + "\n").encode("utf-8"))
        result = item["inspection"]
        formats[result.get("format", result.get("status", "unknown"))] += 1
        if result.get("status") == "rejected":
            errors.append({"path": item["path"], "error": result["error"]})
        if "entry_count" in result:
            empty += result["entry_count"] == 0
        for entry in result.get("entries", []):
            entries += 1
            encodings[entry["encoding"]] += 1
            decoded_bytes += entry["decoded_size"]
            for chunk in entry.get("chunks", []):
                chunk_tags[chunk["tag_hex"]] += 1
                if "type_name" in chunk:
                    types[chunk["type_name"]] += 1
                if "stream_toc" in chunk:
                    toc_count += 1
                    toc_references += chunk["stream_toc"]["reference_count"]
    return {"file_count": len(files), "total_bytes": sum(f["size"] for f in files),
            "manifest_sha256": manifest.hexdigest(),
            "by_extension": {k: {"count": v[0], "bytes": v[1]} for k, v in sorted(extensions.items())},
            "by_top_directory": {k: {"count": v[0], "bytes": v[1]} for k, v in sorted(directories.items())},
            "formats": dict(formats), "rejections": errors,
            "str": {"entry_count": entries, "empty_files": empty,
                    "declared_decoded_bytes": decoded_bytes, "encodings": dict(encodings),
                    "chunk_tags": dict(chunk_tags), "resource_type_occurrences": dict(types),
                    "stream_toc_occurrences": toc_count, "stream_toc_references": toc_references}}


def resolve_stream_tocs(files):
    """Resolve only against the already inventoried root, case-insensitively.

    No filesystem reads or path traversal arise from names supplied by assets.
    Entry counts and (word_12, tag) pairs prove the three-byte table relationship.
    """
    index = {}
    for file in files:
        key = file["path"].casefold()
        require(key not in index, "ambiguous case-insensitive inventory path")
        index[key] = file
    for source in files:
        result = source["inspection"]
        try:
            for entry in result.get("entries", []):
                for chunk in entry.get("chunks", []):
                    if "stream_toc" not in chunk:
                        continue
                    toc = chunk["stream_toc"]
                    directory = source["path"].rsplit("/", 1)[0] if "/" in source["path"] else ""
                    for record in toc["records"]:
                        relative = "/".join(p for p in (directory, record["name"].replace("\\", "/") + ".str") if p)
                        target = index.get(relative.casefold())
                        require(target is not None, f"StreamTOC target absent from inventory: {relative}")
                        target_result = target["inspection"]
                        require(target_result.get("format") == "SToc_observed_v7_profile" and
                                target_result.get("status") != "rejected", f"invalid StreamTOC target: {relative}")
                        if record["index"] == 0:
                            require(target["path"] == source["path"], "StreamTOC self name does not resolve to its source")
                        else:
                            require(record["entry_count"] == target_result["entry_count"],
                                    f"StreamTOC/SToc entry count mismatch: {relative}")
                            for encoded, actual in zip(record["entries"], target_result["entries"]):
                                require((encoded["stoc_word_12"], encoded["tag_hex"]) == (actual["word_12"], actual["tag_hex"]),
                                        f"StreamTOC/SToc block metadata mismatch: {relative}")
                        record["resolved_path"] = target["path"]
                        record["resolved_stoc_entry_count"] = target_result["entry_count"]
                    for record in toc["records"][1:]:
                        record["linked_resolved_path"] = toc["records"][record["linked_record_index"]]["resolved_path"]
                    toc["validation"] = "profile_links_names_and_all_referenced_SToc_tables"
        except ParseError as exc:
            result.update({"status": "rejected", "error": str(exc)})


def main(argv=None):
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "Simpsons Game, The (USA)")
    cli.add_argument("--output", "-o", default="-", help="JSON file outside inputs, or - for stdout (default)")
    cli.add_argument("--decode-str", action="store_true", help="validate RefPack, padding and resource descriptors; slower")
    cli.add_argument("--image", type=Path, help="optional read-only derived PE for embedded shader text inventory")
    cli.add_argument("--image-layout", choices=("flat", "raw"), default="flat", help="flat: file offsets are RVAs; raw: use PE section raw offsets")
    args = cli.parse_args(argv)
    try:
        root = args.root.resolve(strict=True)
        require(root.is_dir(), "--root must be a directory")
        image_path = args.image.resolve(strict=True) if args.image else None
        output = Path(args.output).absolute() if args.output != "-" else None
        if output:
            require(not output.resolve().is_relative_to(root), "report must be outside the original asset tree")
            require(image_path is None or output.resolve() != image_path, "report cannot overwrite the image")
            require(output.resolve() != Path(__file__).resolve(), "report cannot overwrite the inspector")
            if output.exists() or output.is_symlink():
                info = output.lstat()
                require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and not
                        (getattr(info, "st_file_attributes", 0) & 0x400),
                        "refusing linked or non-regular report destination")
        files, cache = [], {}
        for path in inventory(root):
            extension = path.suffix.lower() or "<none>"
            def parse(data):
                if extension == ".str":
                    return inspect_str(data, args.decode_str, cache)
                if extension == ".lua":
                    return inspect_lua(data[:])
                if extension in (".vp6", ".snu", ".mus"):
                    return inspect_media(data, extension)
                return {"status": "opaque", "validation": "inventory_and_header_only"}
            record = inspect_file(path, parse)
            record.update({"path": path.relative_to(root).as_posix(), "extension": extension})
            files.append(record)
        resolve_stream_tocs(files)
        report = {"schema_version": 1, "asset_root_name": root.name,
                  "options": {"decode_str": args.decode_str},
                  "evidence_policy": {"parsed": "Validated only to the stated format/profile scope.",
                                      "lexical": "Text is byte evidence, not proof of runtime use.",
                                      "unknown": "Uninterpreted fields retain offset-based names; opaque payloads are not validated.",
                                      "hypotheses": "Engine integration proposals are in docs/assets.md, not inferred as parsed facts."},
                  "summary": summarize(files), "files": files}
        rejected = len(report["summary"]["rejections"])
        if image_path:
            report["image"] = inspect_file(image_path, lambda data: inspect_image(data, args.image_layout))
            report["image"]["name"] = image_path.name
            rejected += report["image"]["inspection"].get("status") == "rejected"
        # No timestamps, platform paths, elapsed times or unordered sets in JSON.
        encoded = json.dumps(report, sort_keys=True, ensure_ascii=True, indent=2) + "\n"
        if output:
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(encoded)
            print(f"Inspected {len(files)} files; {rejected} rejection(s); report: {output}", file=sys.stderr)
        else:
            sys.stdout.write(encoded)
        return 1 if rejected else 0
    except (OSError, ParseError) as exc:
        print(f"inspection failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
