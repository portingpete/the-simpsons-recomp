"""Regression checks for the deterministic original-game audio inventory."""

import hashlib
import json
from collections import defaultdict
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
from tools import generate_audio_catalog as catalog
from tools.inspect_assets import inspect_snu


ORIGINAL = ROOT / "Simpsons Game, The (USA)"
CATALOG = ROOT / "analysis/audio_catalog.bin"
SUMMARY = ROOT / "analysis/audio_catalog_summary.json"


def eaac_block(*, sample_count=1, payload=0x42, segment_end=False):
    """A small, structurally valid one-layer EA-XMA block for framing tests."""
    flag = 0x80000000 if segment_end else 0
    return struct.pack(">III", flag | 17, sample_count, (9 << 2) | 3) + b"\x08\0\0\0" + bytes((payload,))


def snu(blocks, *, loop=False):
    total = sum(struct.unpack_from(">I", block, 4)[0] for block in blocks)
    wrapper = bytearray(32)
    wrapper[:4] = b"\x01\0\0\0"
    struct.pack_into(">I", wrapper, 8, 32)
    struct.pack_into(">I", wrapper, 16, 0x0300BB80)  # EA-XMA, one channel, 48 kHz.
    struct.pack_into(">I", wrapper, 20, 0x40000000 | (0x20000000 if loop else 0) | total)
    if loop:
        struct.pack_into(">II", wrapper, 24, 1, len(blocks[0]))
    result = bytes(wrapper) + b"".join(blocks)
    inspect_snu(result)  # The synthetic records must pass the real container parser.
    return result


def sha(data):
    return hashlib.sha256(data).hexdigest()


class SyntheticAudioCatalogTests(unittest.TestCase):
    def test_collisions_loops_and_deterministic_bytes(self):
        first = eaac_block(payload=0x51)
        alpha = snu((first, eaac_block(payload=0xA1, segment_end=True)))
        beta = snu((first, eaac_block(payload=0xB2, segment_end=True)))
        loop = snu((eaac_block(payload=0xC3, segment_end=True),
                    eaac_block(payload=0xD4, segment_end=True)), loop=True)
        with tempfile.TemporaryDirectory(prefix="simpsons-audio-catalog-") as temporary:
            base = Path(temporary)
            root = base / "original"
            streams = root / "audiostreams"
            streams.mkdir(parents=True)
            for name, data in (("alpha.exa.snu", alpha), ("beta.exa.snu", beta),
                               ("gamma.exa.snu", alpha), ("loop.exa.snu", loop)):
                (streams / name).write_bytes(data)
            first_binary, first_summary = base / "first.bin", base / "first.json"
            second_binary, second_summary = base / "second.bin", base / "second.json"
            catalog.generate(root, None, first_binary, first_summary)
            catalog.generate(root, None, second_binary, second_summary)
            self.assertEqual(first_binary.read_bytes(), second_binary.read_bytes())
            self.assertEqual(first_summary.read_bytes(), second_summary.read_bytes())
            self.assertEqual(first_binary.read_bytes()[:8], b"SIMAUD01")
            parsed = catalog.read_catalog(first_binary)
            self.assertEqual((len(parsed.sources), len(parsed.streams), len(parsed.blocks), len(parsed.layers)),
                             (4, 4, 8, 8))
            report = json.loads(first_summary.read_text(encoding="utf-8"))
            self.assertEqual(report["schema_version"], 1)
            self.assertEqual((report["snu_count"], report["mus_count"], report["loop_streams"]),
                             (4, 0, 1))
            self.assertEqual(report["binary_sha256"], sha(first_binary.read_bytes()))

            sources = list(parsed.sources)
            self.assertEqual([source["path"] for source in sources],
                             [f"audiostreams/{name}.exa.snu" for name in
                              ("alpha", "beta", "gamma", "loop")])
            self.assertEqual(sources[0]["sha256"], sources[2]["sha256"])
            self.assertNotEqual(sources[0]["sha256"], sources[1]["sha256"])
            streams = list(parsed.streams)
            self.assertEqual([stream["source_index"] for stream in streams], list(range(4)))
            self.assertEqual([stream["ordinal"] for stream in streams], [0] * 4)
            self.assertEqual(len({stream["header"] for stream in streams[:3]}), 1)
            self.assertNotEqual(streams[0]["header"], streams[3]["header"])
            self.assertEqual([stream["first_block"] for stream in streams], [0, 2, 4, 6])
            self.assertEqual([stream["block_count"] for stream in streams], [2] * 4)
            self.assertEqual((streams[3]["flags"], streams[3]["loop_start_sample"],
                              streams[3]["loop_offset_relative"]), (1, 1, 17))

            blocks = list(parsed.blocks)
            self.assertEqual([block["raw_offset"] for block in blocks], [32, 49] * 4)
            self.assertEqual([block["raw_bytes"] for block in blocks], [17] * 8)
            self.assertEqual([block["segment_flag"] for block in blocks],
                             [0, 0x80] * 3 + [0x80, 0x80])
            # A first-block hash cannot distinguish alpha from beta; their
            # second blocks can. Gamma deliberately aliases alpha in full.
            self.assertEqual(blocks[0]["normalized_sha256"], blocks[2]["normalized_sha256"])
            self.assertNotEqual(blocks[1]["normalized_sha256"], blocks[3]["normalized_sha256"])
            self.assertEqual(blocks[0]["normalized_sha256"], blocks[4]["normalized_sha256"])
            self.assertEqual(blocks[1]["normalized_sha256"], blocks[5]["normalized_sha256"])
            self.assertEqual(blocks[1]["raw_sha256"], hashlib.sha256(alpha[49:66]).digest())
            self.assertEqual(blocks[1]["normalized_sha256"],
                             hashlib.sha256(b"\0" + alpha[50:66]).digest())
            self.assertNotEqual(blocks[1]["raw_sha256"], blocks[1]["normalized_sha256"])
            self.assertEqual([(layer["block_index"], layer["payload_bytes"], layer["restored_ff"])
                              for layer in parsed.layers], [(index, 5, 2043) for index in range(8)])


class OriginalAudioCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CATALOG.is_file() or not SUMMARY.is_file():
            raise AssertionError("Generate analysis/audio_catalog.bin and its JSON summary first")
        cls.catalog = catalog.read_catalog(CATALOG)
        cls.summary = json.loads(SUMMARY.read_text(encoding="utf-8"))

    def test_complete_stream_and_block_census(self):
        parsed = self.catalog
        self.assertEqual(len(parsed.sources), 7430)  # 7,413 SNU + 17 MUS files.
        self.assertEqual(len(parsed.streams), 9470)   # 7,413 SNU + 2,057 MUS entries.
        self.assertEqual(len(parsed.blocks), 377643) # 265,077 SNU + 112,566 MUS blocks.
        self.assertEqual(len(parsed.layers), 677048)
        self.assertEqual(self.summary["binary_sha256"], sha(CATALOG.read_bytes()))
        self.assertEqual((self.summary["snu_count"], self.summary["mus_count"],
                          self.summary["loop_streams"]), (7413, 17, 63))

    def test_known_header_collision_and_original_block_identity(self):
        parsed = self.catalog
        source_by_index = list(parsed.sources)
        source_by_path = {source["path"]: source for source in source_by_index}
        self.assertEqual(len(source_by_path), len(source_by_index))
        header = bytes.fromhex("0300bb804001c000")
        aliases = [source_by_index[stream["source_index"]]["path"]
                   for stream in parsed.streams if stream["header"] == header]
        self.assertEqual(len(aliases), 9)
        self.assertIn("audiostreams/cb_xxx_0/d_chcb_xxx_0000b2b.exa.snu", aliases)
        self.assertIn("audiostreams/cb_xxx_0/d_chcb_xxx_00066fa.exa.snu", aliases)
        path = "audiostreams/cb_xxx_0/d_chcb_xxx_0000b2b.exa.snu"
        source = source_by_path[path]
        self.assertEqual(source["sha256"].hex(),
                         "9a7aac625ef82bb88696c71a627a523680aca03c0c2215276a59114c40ea443c")
        self.assertEqual(source["sha256"], hashlib.sha256((ORIGINAL / path).read_bytes()).digest())
        stream = parsed.streams[source["first_stream"]]
        self.assertEqual(stream["header"], header)
        block = parsed.blocks[stream["first_block"]]
        with (ORIGINAL / path).open("rb") as input_file:
            input_file.seek(block["raw_offset"])
            raw = input_file.read(block["raw_bytes"])
        self.assertEqual(len(raw), block["raw_bytes"])
        self.assertEqual(block["raw_sha256"], hashlib.sha256(raw).digest())
        self.assertEqual(block["normalized_sha256"],
                         hashlib.sha256(bytes((raw[0] & 0x7F,)) + raw[1:]).digest())
        self.assertEqual(block["normalized_sha256"].hex(),
                         "7a1792275e4da6ade9b2ecce90283c27c5100f54285f8e94256417abd36799f4")

    def test_header_and_first_block_collisions_remain_distinct(self):
        parsed = self.catalog
        by_header = defaultdict(list)
        by_first = defaultdict(list)
        for index, stream in enumerate(parsed.streams):
            by_header[stream["header"]].append(index)
            first_hash = parsed.blocks[stream["first_block"]]["normalized_sha256"]
            by_first[(stream["header"], first_hash)].append(index)
        header_collisions = [group for group in by_header.values() if len(group) > 1]
        first_collisions = [group for group in by_first.values() if len(group) > 1]
        self.assertEqual((len(header_collisions), sum(map(len, header_collisions))), (1379, 4254))
        self.assertEqual((len(first_collisions), sum(map(len, first_collisions))), (213, 447))
        divergent = []
        for group in first_collisions:
            signatures = {
                tuple((block["normalized_sha256"], block["raw_bytes"], block["frames"])
                      for block in parsed.blocks[stream["first_block"]:
                                                 stream["first_block"] + stream["block_count"]])
                for stream in (parsed.streams[index] for index in group)
            }
            if len(signatures) > 1:
                divergent.append(group)
        # Full-content aliases retain every original path. One ambient group
        # needs a second block to resolve which decoded content was selected.
        self.assertEqual(len(divergent), 1)
        self.assertEqual(len(divergent[0]), 21)
        second_hashes = {parsed.blocks[parsed.streams[index]["first_block"] + 1]["normalized_sha256"]
                         for index in divergent[0]}
        self.assertGreater(len(second_hashes), 1)

    def test_all_record_ranges_and_loop_boundaries(self):
        parsed = self.catalog
        sources = list(parsed.sources)
        self.assertEqual([source["path"] for source in sources],
                         sorted(source["path"] for source in sources))
        self.assertEqual(sum(source["stream_count"] for source in sources), len(parsed.streams))
        self.assertEqual(sum(stream["block_count"] for stream in parsed.streams), len(parsed.blocks))
        self.assertEqual(sum(block["layer_count"] for block in parsed.blocks), len(parsed.layers))
        for stream_index, stream in enumerate(parsed.streams):
            source = sources[stream["source_index"]]
            self.assertLessEqual(stream["header_offset"] + 8, source["file_bytes"])
            self.assertLessEqual(stream["audio_offset"] + stream["audio_bytes"], source["file_bytes"])
            first = stream["first_block"]
            end = first + stream["block_count"]
            self.assertLessEqual(end, len(parsed.blocks))
            blocks = parsed.blocks[first:end]
            self.assertEqual([block["ordinal"] for block in blocks], list(range(len(blocks))))
            self.assertTrue(all(block["stream_index"] == stream_index for block in blocks))
            self.assertEqual(sum(block["frames"] for block in blocks), stream["frames"])
            self.assertEqual(blocks[0]["raw_offset"], stream["audio_offset"])
            self.assertEqual(blocks[-1]["raw_offset"] + blocks[-1]["raw_bytes"],
                             stream["audio_offset"] + stream["audio_bytes"])
            cursor = stream["audio_offset"]
            for block_index, block in enumerate(blocks, first):
                self.assertEqual(block["raw_offset"], cursor)
                cursor += block["raw_bytes"]
                layers = parsed.layers[block["first_layer"]:
                                       block["first_layer"] + block["layer_count"]]
                self.assertEqual(len(layers), (stream["channels"] + 1) // 2)
                layer_cursor = block["raw_offset"] + 8
                for layer in layers:
                    self.assertEqual(layer["block_index"], block_index)
                    self.assertEqual(layer["header_offset"], layer_cursor)
                    self.assertEqual(layer["payload_offset"], layer_cursor + 4)
                    self.assertEqual(layer["payload_bytes"], layer["layer_bytes"] - 4)
                    self.assertEqual(layer["restored_ff"], (-layer["payload_bytes"]) % 2048)
                    layer_cursor += layer["layer_bytes"]
                self.assertEqual(layer_cursor + block["terminal_padding"], cursor)
            ends = [index for index, block in enumerate(blocks) if block["segment_flag"] == 0x80]
            self.assertEqual(len(ends), 2 if stream["flags"] else 1)
            if stream["flags"]:
                introduction = blocks[ends[0]]
                self.assertEqual(introduction["raw_offset"] + introduction["raw_bytes"],
                                 stream["audio_offset"] + stream["loop_offset_relative"])
                self.assertEqual(sum(block["frames"] for block in blocks[:ends[0] + 1]),
                                 stream["loop_start_sample"])


if __name__ == "__main__":
    unittest.main()
