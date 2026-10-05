"""Structural and provenance tests for the original embedded SBK cue catalog."""

import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_resident_audio_catalog as resident


ORIGINAL = ROOT / "Simpsons Game, The (USA)"
REPORT = ROOT / "analysis/assets.json"
CATALOG_JSON = ROOT / "analysis/resident_audio_catalog.json"
CATALOG_BIN = ROOT / "analysis/resident_audio_catalog.bin"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def block(frames, selector, payload):
    size = 12 + len(payload)
    return struct.pack(">III", size, frames, ((size - 8) << 2) | selector) + payload


def synthetic_bank():
    plain = (struct.pack(">II", 0x0300BB80, 25) +
             block(25, 3, b"\x08"))
    looping = (struct.pack(">III", 0x03003E80, 0x20000000 | 40, 10) +
               block(10, 0, b"\x08") + block(30, 3, b"\x09"))
    audio_offset = 64
    content = b"\0" * 5 + plain + looping
    data = (struct.pack(">6I", resident.MAGIC, resident.VERSION, 16,
                        len(content), audio_offset, len(content)) +
            b"\0" * (audio_offset - 24) + content)
    return data


class SyntheticResidentAudioCatalogTests(unittest.TestCase):
    def test_envelope_looped_cue_hashes_and_deterministic_binary(self):
        data = synthetic_bank()
        envelope = resident.bank_envelope(data)
        self.assertEqual(envelope, {"metadata_bytes": 16, "audio_offset": 64,
                                    "audio_bytes": len(data) - 64})
        cues = resident.discover_cues(data, envelope["audio_offset"])
        self.assertEqual(len(cues), 2)
        self.assertEqual((cues[0]["header_offset"], cues[0]["frames"],
                          cues[0]["loop"], len(cues[0]["blocks"])), (69, 25, False, 1))
        self.assertEqual((cues[1]["header_offset"], cues[1]["frames"],
                          cues[1]["loop"], cues[1]["loop_start_sample"],
                          len(cues[1]["blocks"])), (cues[0]["end_offset"], 40, True, 10, 2))
        self.assertEqual((cues[1]["playback_rate"],
                          [piece["codec_rate"] for piece in cues[1]["blocks"]]),
                         (16000, [24000, 48000]))
        self.assertEqual(cues[1]["end_offset"], len(data))
        bank = {"container_path": "synthetic/sound.str", "name": "synthetic.sbk",
                "entry_index": 2, "payload_bytes": len(data),
                "payload_sha256": sha(data), "container_sha256": sha(b"container"),
                **envelope, "cues": cues}
        encoded = resident.binary_catalog([bank])
        self.assertEqual(encoded, resident.binary_catalog([bank]))
        (magic, version, banks, cue_count, block_count, bank_at, cue_at,
         block_at, strings_at, string_bytes) = resident.BIN_HEADER.unpack_from(encoded)
        self.assertEqual((magic, version, banks, cue_count, block_count),
                         (b"SIMRES01", 1, 1, 2, 3))
        self.assertEqual((bank_at, cue_at, block_at, strings_at),
                         (resident.BIN_HEADER.size,
                          bank_at + resident.BIN_BANK.size,
                          cue_at + 2 * resident.BIN_CUE.size,
                          block_at + 3 * resident.BIN_BLOCK.size))
        self.assertEqual(strings_at + string_bytes, len(encoded))
        bank_row = resident.BIN_BANK.unpack_from(encoded, bank_at)
        strings = encoded[strings_at:]
        self.assertEqual(strings[bank_row[0]:bank_row[0] + bank_row[1]], b"synthetic/sound.str")
        self.assertEqual(strings[bank_row[2]:bank_row[2] + bank_row[3]], b"synthetic.sbk")
        self.assertEqual((bank_row[4], bank_row[5], bank_row[6], bank_row[7],
                          bank_row[8], bank_row[9], bank_row[10]),
                         (len(data), 16, 64, len(data) - 64, 0, 2, 2))
        self.assertEqual(bank_row[11], hashlib.sha256(data).digest())
        loop_row = resident.BIN_CUE.unpack_from(encoded, cue_at + resident.BIN_CUE.size)
        self.assertEqual((loop_row[0], loop_row[1], loop_row[2], loop_row[3],
                          loop_row[4], loop_row[5], loop_row[6], loop_row[7],
                          loop_row[8], loop_row[9], loop_row[11]),
                         (0, 1, cues[1]["header_offset"], 1, 2, 40, 16000, 1, 10, 1, len(data)))
        self.assertEqual(loop_row[10], bytes.fromhex(cues[1]["header_hex"]))
        flattened = [piece for cue in cues for piece in cue["blocks"]]
        for ordinal, piece in enumerate(flattened):
            row = resident.BIN_BLOCK.unpack_from(encoded,
                                                 block_at + ordinal * resident.BIN_BLOCK.size)
            self.assertEqual((row[0], row[1], row[2], row[3], row[4], row[5],
                              row[6], row[7], row[8]),
                             (0 if ordinal == 0 else 1, 0 if ordinal != 2 else 1,
                              piece["offset"], piece["bytes"], piece["frames"],
                              piece["selector"], piece["codec_rate"],
                              piece["restored_ff"], piece["payload_bytes"]))
            self.assertEqual(row[9], hashlib.sha256(data[piece["offset"]:
                                                     piece["offset"] + piece["bytes"]]).digest())

    def test_rejects_bad_envelope_and_cue_gap(self):
        original = synthetic_bank()
        changed = bytearray(original)
        changed[0] ^= 1
        with self.assertRaisesRegex(ValueError, "envelope"):
            resident.bank_envelope(changed)
        changed = bytearray(original)
        changed[12:16] = (1).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "envelope"):
            resident.bank_envelope(changed)
        cues = resident.discover_cues(original, 64)
        changed = original[:cues[1]["header_offset"]] + b"\0" + original[cues[1]["header_offset"]:]
        with self.assertRaisesRegex(ValueError, "Gap or overlap"):
            resident.discover_cues(changed, 64)
        self.assertIsNone(resident.cue_at(original, len(original) - 3))


class OriginalResidentAudioCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.summary = json.loads(CATALOG_JSON.read_text(encoding="utf-8"))
        cls.binary = CATALOG_BIN.read_bytes()
        cls.header = resident.BIN_HEADER.unpack_from(cls.binary)

    def test_complete_corpus_counts_and_binary_digest(self):
        result = self.summary
        self.assertEqual((result["bank_count"], result["banks_with_cues"],
                          result["cue_count"], result["block_count"]),
                         (156, 156, 45382, 45498))
        self.assertEqual((result["mono_cues"], result["stereo_cues"], result["loop_cues"]),
                         (45371, 11, 394))
        self.assertEqual(result["binary_sha256"], sha(self.binary))
        self.assertEqual(result["binary_bytes"], len(self.binary))
        self.assertEqual(self.header[:5], (b"SIMRES01", 1, 156, 45382, 45498))
        self.assertEqual(self.header[5], resident.BIN_HEADER.size)
        self.assertEqual(self.header[6], self.header[5] + 156 * resident.BIN_BANK.size)
        self.assertEqual(self.header[7], self.header[6] + 45382 * resident.BIN_CUE.size)
        self.assertEqual(self.header[8], self.header[7] + 45498 * resident.BIN_BLOCK.size)
        self.assertEqual(self.header[8] + self.header[9], len(self.binary))

    def test_every_binary_row_and_cue_section_matches_provenance(self):
        _, _, _, _, _, bank_at, cue_at, block_at, strings_at, _ = self.header
        strings = self.binary[strings_at:]
        cue_cursor = block_cursor = 0
        for bank_index, bank in enumerate(self.summary["banks"]):
            row = resident.BIN_BANK.unpack_from(self.binary,
                                                bank_at + bank_index * resident.BIN_BANK.size)
            self.assertEqual(strings[row[0]:row[0] + row[1]].decode("utf-8"), bank["container_path"])
            self.assertEqual(strings[row[2]:row[2] + row[3]].decode("utf-8"), bank["name"])
            self.assertEqual((row[4], row[5], row[6], row[7], row[8], row[9], row[10]),
                             (bank["payload_bytes"], bank["metadata_bytes"], bank["audio_offset"],
                              bank["audio_bytes"], cue_cursor, len(bank["cues"]), bank["entry_index"]))
            self.assertEqual(row[11].hex(), bank["payload_sha256"])
            self.assertEqual(row[12].hex(), bank["container_sha256"])
            self.assertEqual(bank["cue_start_offset"] - bank["audio_offset"],
                             bank["audio_prefix_bytes"])
            end = bank["cue_start_offset"]
            for ordinal, cue in enumerate(bank["cues"]):
                self.assertEqual(cue["header_offset"], end)
                packed_cue = resident.BIN_CUE.unpack_from(self.binary,
                    cue_at + cue_cursor * resident.BIN_CUE.size)
                self.assertEqual((packed_cue[0], packed_cue[1], packed_cue[2],
                                  packed_cue[3], packed_cue[4], packed_cue[5],
                                  packed_cue[6], packed_cue[7], packed_cue[8],
                                  packed_cue[9], packed_cue[11]),
                                 (bank_index, ordinal, cue["header_offset"], block_cursor,
                                  len(cue["blocks"]), cue["frames"], cue["playback_rate"],
                                  cue["channels"], cue["loop_start_sample"],
                                  int(cue["loop"]), cue["end_offset"]))
                self.assertEqual(packed_cue[10].hex(), cue["header_hex"])
                self.assertEqual(len(cue["blocks"]), 2 if cue["loop_start_sample"] else 1)
                self.assertEqual(sum(piece["frames"] for piece in cue["blocks"]), cue["frames"])
                block_end = cue["header_offset"] + (12 if cue["loop"] else 8)
                for block_ordinal, piece in enumerate(cue["blocks"]):
                    self.assertEqual(piece["offset"], block_end)
                    packed_block = resident.BIN_BLOCK.unpack_from(self.binary,
                        block_at + block_cursor * resident.BIN_BLOCK.size)
                    self.assertEqual((packed_block[0], packed_block[1], packed_block[2],
                                      packed_block[3], packed_block[4], packed_block[5],
                                      packed_block[6], packed_block[7], packed_block[8]),
                                     (cue_cursor, block_ordinal, piece["offset"],
                                      piece["bytes"], piece["frames"], piece["selector"],
                                      piece["codec_rate"], piece["restored_ff"],
                                      piece["payload_bytes"]))
                    self.assertEqual(packed_block[9].hex(), piece["sha256"])
                    self.assertEqual(piece["restored_ff"], (-piece["payload_bytes"]) % 2048)
                    block_end += piece["bytes"]
                    block_cursor += 1
                end = cue["end_offset"]
                self.assertEqual(block_end, end)
                cue_cursor += 1
            self.assertEqual(end, bank["payload_bytes"])
        self.assertEqual((cue_cursor, block_cursor), (45382, 45498))

    def test_original_frontend_source_and_first_certified_block(self):
        bank = next(bank for bank in self.summary["banks"] if
                    bank["payload_sha256"] ==
                    "6605ab30a96453c9108af5866b3670ddda719cafe36301f857ae6279bc136482")
        payload, provenance = resident.select_payload(
            ORIGINAL / bank["container_path"], bank["entry_index"], bank["name"], ORIGINAL)
        self.assertEqual(sha(payload), bank["payload_sha256"])
        self.assertEqual(provenance["source"]["sha256"], bank["container_sha256"])
        self.assertEqual(resident.bank_envelope(payload)["audio_offset"], bank["audio_offset"])
        cue = next(cue for cue in bank["cues"] if cue["header_offset"] == 33335)
        self.assertEqual((cue["channels"], cue["playback_rate"], cue["blocks"][0]["codec_rate"]),
                         (2, 8000, 24000))
        self.assertEqual(cue["blocks"][0]["sha256"],
                         "69afdab657fa91c308ffc76a84b72ec20458d0c5740b8d7d54993fb7a7128381")
        self.assertEqual(resident.cue_at(payload, 33335), cue)


if __name__ == "__main__":
    unittest.main()
