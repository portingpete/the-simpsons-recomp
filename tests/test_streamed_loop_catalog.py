"""Check every shipped looped SNU against its original reader-owned blocks.

The original 823412C0 callback clears bit 31 of a terminal EAAC block before
823424D8 receives it. This test checks that exact normalization, then walks
the intro, body, and first body block after a seek back to the loop offset.
"""

from __future__ import annotations

from collections import defaultdict
import hashlib
from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
from tools import generate_audio_catalog as audio_catalog


ASSETS = ROOT / "Simpsons Game, The (USA)"
CATALOG = ROOT / "analysis/audio_catalog.bin"


def digest(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def reader_bytes(raw: bytes) -> bytes:
    return bytes((raw[0] & 0x7F,)) + raw[1:]


class OriginalStreamedLoopCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = audio_catalog.read_catalog(CATALOG)
        cls.sources = list(cls.catalog.sources)
        cls.streams = list(cls.catalog.streams)
        cls.by_header = defaultdict(list)
        for index, stream in enumerate(cls.streams):
            cls.by_header[stream["header"]].append(index)

    def candidates_for(self, header, prior, ordinal, owned):
        """Independently replay the catalog's ordered block admission key."""
        candidates = self.by_header[header] if prior is None else prior
        signature = digest(owned)
        survivors = []
        agreed = None
        for index in candidates:
            stream = self.streams[index]
            if ordinal >= stream["block_count"]:
                continue
            block = self.catalog.blocks[stream["first_block"] + ordinal]
            if (block["raw_bytes"] != len(owned) or
                    block["normalized_sha256"] != signature):
                continue
            metadata = (block["frames"], block["raw_bytes"],
                        block["terminal_padding"], block["layer_count"])
            if agreed is None:
                agreed = metadata
            self.assertEqual(agreed, metadata)
            survivors.append(index)
        self.assertTrue(survivors, f"No ordered original block for {header.hex()} #{ordinal}")
        return survivors

    def test_all_63_loop_intros_bodies_and_restart_blocks(self):
        loops = [(index, stream) for index, stream in enumerate(self.streams)
                 if stream["flags"]]
        self.assertEqual(len(loops), 63)
        self.assertEqual({stream["channels"] for _, stream in loops}, {2, 4})
        self.assertEqual(sum(stream["channels"] == 4 for _, stream in loops), 62)
        self.assertEqual({stream["loop_start_sample"] for _, stream in loops}, {1})

        checked_blocks = 0
        for index, stream in loops:
            source = self.sources[stream["source_index"]]
            path = source["path"]
            with self.subTest(path=path):
                self.assertEqual(source["kind"], 1)  # Original .snu file.
                self.assertEqual(stream["ordinal"], 0)
                original = (ASSETS / path).read_bytes()
                self.assertEqual(len(original), source["file_bytes"])
                self.assertEqual(digest(original), source["sha256"])
                self.assertEqual(original[stream["header_offset"]:
                                          stream["header_offset"] + 8], stream["header"])
                self.assertEqual(struct.unpack_from(">II", original,
                                                   stream["header_offset"] + 8),
                                 (stream["loop_start_sample"],
                                  stream["loop_offset_relative"]))

                blocks = self.catalog.blocks[stream["first_block"]:
                                             stream["first_block"] + stream["block_count"]]
                self.assertGreaterEqual(len(blocks), 2)
                self.assertEqual(blocks[0]["raw_offset"], stream["audio_offset"])
                self.assertEqual(blocks[0]["raw_bytes"], stream["loop_offset_relative"])
                self.assertEqual(blocks[0]["frames"], 1)
                self.assertEqual(blocks[1]["raw_offset"],
                                 stream["audio_offset"] + stream["loop_offset_relative"])

                survivors = None
                produced = 0
                cursor = stream["audio_offset"]
                for ordinal, block in enumerate(blocks):
                    self.assertEqual(block["stream_index"], index)
                    self.assertEqual(block["ordinal"], ordinal)
                    self.assertEqual(block["raw_offset"], cursor)
                    end = cursor + block["raw_bytes"]
                    raw = original[cursor:end]
                    self.assertEqual(len(raw), block["raw_bytes"])
                    self.assertEqual(raw[0], block["segment_flag"])
                    self.assertEqual(int.from_bytes(raw[:4], "big") & 0xFFFFFF,
                                     block["raw_bytes"])
                    self.assertEqual(int.from_bytes(raw[4:8], "big"), block["frames"])
                    self.assertEqual(digest(raw), block["raw_sha256"])
                    owned = reader_bytes(raw)
                    self.assertEqual(digest(owned), block["normalized_sha256"])
                    survivors = self.candidates_for(stream["header"], survivors,
                                                    ordinal, owned)
                    self.assertIn(index, survivors)

                    # The original producer queues F=0 at each segment start,
                    # then F=1 for subsequent blocks in that segment.
                    restart = ordinal in (0, 1)
                    self.assertEqual(block["segment_flag"],
                                     0x80 if ordinal in (0, len(blocks) - 1) else 0)
                    if restart:
                        self.assertEqual(produced, 0 if ordinal == 0 else 1)
                    produced += block["frames"]
                    cursor = end
                    checked_blocks += 1

                self.assertEqual(produced, stream["frames"])
                self.assertEqual(cursor,
                                 stream["audio_offset"] + stream["audio_bytes"])
                self.assertEqual(blocks[0]["raw_offset"] + blocks[0]["raw_bytes"],
                                 blocks[1]["raw_offset"])
                self.assertTrue(all(self.streams[candidate]["block_count"] == len(blocks)
                                    for candidate in survivors))

                # 823428F8 seeks back to audioOffset+loopOffsetRelative and
                # resets producer progress to one intro sample. Re-admitting
                # ordinal one must preserve the same digest-backed identity.
                seek = stream["audio_offset"] + stream["loop_offset_relative"]
                self.assertEqual(seek, blocks[1]["raw_offset"])
                body = original[seek:seek + blocks[1]["raw_bytes"]]
                replay = self.candidates_for(stream["header"], survivors, 1,
                                             reader_bytes(body))
                self.assertIn(index, replay)
                self.assertEqual(1 + blocks[1]["frames"],
                                 stream["loop_start_sample"] + blocks[1]["frames"])

        print(f"Checked {len(loops)} original looped SNU streams and "
              f"{checked_blocks} ordered reader-owned blocks")


if __name__ == "__main__":
    unittest.main()
