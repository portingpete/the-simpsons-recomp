"""Check the original AMX cue inventory and every STR placement alias."""

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_amx_audio_catalog as amx


ORIGINAL = ROOT / "Simpsons Game, The (USA)"
REPORT = ROOT / "analysis/assets.json"
CATALOG = ROOT / "analysis/amx_audio_catalog.json"
CATALOG_BIN = ROOT / "analysis/amx_audio_catalog.bin"


def cue(frames, *, loop=False, payload=b"\x08"):
    header = struct.pack(">II", 0x0300BB80, frames | (0x20000000 if loop else 0))
    if loop:
        header += struct.pack(">I", 0)
    size = 12 + len(payload)
    block = struct.pack(">III", size, frames, ((size - 8) << 2) | 3) + payload
    return header + block


def envelope(body, metadata=16):
    prefix = bytearray(metadata + 64)
    struct.pack_into(">4I", prefix, 0, 9, len(body), metadata, len(body))
    return bytes(prefix) + body


class SyntheticAmxTests(unittest.TestCase):
    def test_contiguous_cues_and_loop_flag(self):
        data = envelope(cue(25) + cue(30, loop=True) + b"\0" * 8)
        parsed = amx.scan_payload(data)
        self.assertEqual((parsed["cue_count"], parsed["block_count"],
                          parsed["prefix_bytes"], parsed["trailing_bytes"]),
                         (2, 2, 80, 8))
        self.assertEqual(parsed["cues"][0]["end_offset"],
                         parsed["cues"][1]["header_offset"])
        self.assertEqual((parsed["cues"][1]["loop"],
                          parsed["cues"][1]["loop_start_sample"]), (True, 0))
        for entry in parsed["cues"]:
            self.assertEqual(entry["cue_sha256"], hashlib.sha256(
                data[entry["header_offset"]:entry["end_offset"]]).hexdigest())

    def test_rejects_envelope_mismatch_and_gap(self):
        data = bytearray(envelope(cue(25) + cue(30)))
        data[12:16] = struct.pack(">I", 99)
        with self.assertRaises(ValueError):
            amx.scan_payload(bytes(data))
        with self.assertRaises(ValueError):
            amx.scan_payload(envelope(cue(25) + b"\0" + cue(30)))


class OriginalAmxTests(unittest.TestCase):
    def test_all_original_aliases_and_reproducibility(self):
        report = json.loads(REPORT.read_text(encoding="utf-8"))
        expected = amx.report_occurrences(report)
        self.assertEqual(len(expected), 58)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "amx.json"
            first = amx.generate(ORIGINAL, REPORT, output)
            content = output.read_bytes()
            binary = output.with_suffix(".bin").read_bytes()
            second = amx.generate(ORIGINAL, REPORT, output)
            self.assertEqual(content, output.read_bytes())
            self.assertEqual(binary, output.with_suffix(".bin").read_bytes())
            self.assertEqual(first, second)
            self.assertEqual(content, CATALOG.read_bytes())
            self.assertEqual(binary, CATALOG_BIN.read_bytes())
        self.assertEqual((first["payload_count"], first["resource_occurrence_count"],
                          first["cue_count"], first["cue_occurrence_count"],
                          first["block_count"], first["unique_block_sequence_count"],
                          first["loop_cue_count"]),
                         (8, 58, 254, 1999, 254, 247, 32))
        observed = {(alias["container_path"], alias["entry_index"], alias["name"])
                    for payload in first["payloads"] for alias in payload["occurrences"]}
        expected_keys = {(item["container_path"], item["entry_index"], item["name"])
                         for item in expected}
        self.assertEqual(observed, expected_keys)
        for payload in first["payloads"]:
            alias = payload["occurrences"][0]
            original, _ = amx.select_payload(
                ORIGINAL / alias["container_path"], alias["entry_index"],
                alias["name"], ORIGINAL)
            self.assertEqual(payload["audio_sha256"], hashlib.sha256(
                original[payload["prefix_bytes"]:payload["audio_end_offset"]]).hexdigest())
            self.assertEqual(payload["cue_count"], len(payload["cues"]))
            self.assertEqual(payload["block_count"],
                             sum(len(entry["blocks"]) for entry in payload["cues"]))
            self.assertEqual(payload["prefix_bytes"] >= payload["metadata_bytes"] + 64, True)
            self.assertEqual(payload["cues"][-1]["end_offset"] + payload["trailing_bytes"],
                             payload["payload_bytes"])
            for prior, following in zip(payload["cues"], payload["cues"][1:]):
                self.assertEqual(prior["end_offset"], following["header_offset"])
            self.assertEqual([entry["ordinal"] for entry in payload["cues"]],
                             list(range(payload["cue_count"])))


if __name__ == "__main__":
    unittest.main()
