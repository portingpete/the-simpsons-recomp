"""Reject stale, partial or reassociated real streamed decode census evidence."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import audit_audio_runtime_coverage as audit


class StreamedAudioAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = audit.read_catalog(ROOT / "analysis/audio_catalog.bin")
        cls.expected = audit.stream_decode_expectations(cls.catalog)
        cls.source = ROOT / "build/stream-catalog-decode/full/report.json"
        cls.report = json.loads(cls.source.read_text(encoding="utf-8"))

    def check(self, value, mutate_log=None, missing_log=False):
        with tempfile.TemporaryDirectory(prefix="simpsons-audio-audit-") as temporary:
            path = Path(temporary) / "report.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            log = path.with_name("native.tsv")
            if not missing_log:
                shutil.copyfile(self.source.with_name("native.tsv"), log)
                if mutate_log:
                    log.write_text(mutate_log(log.read_text(encoding="utf-8")), encoding="utf-8")
            return audit.stream_decode_report(path, self.catalog, self.expected)

    def test_actual_complete_census_and_explicit_limits(self):
        value = audit.stream_decode_report(self.source, self.catalog, self.expected)
        self.assertTrue(value["one_pass_decode_proven"])
        self.assertEqual((value["unique_chains_decoded"], value["sources_covered"],
                          value["streams_covered"], value["blocks_covered"]), (9256, 7430, 9470, 377643))
        self.assertEqual(value["unique_chain_blocks_decoded"] + value["duplicate_blocks_reverified"], 377643)
        self.assertIn("live quota/ring scheduling", value["unproven"])
        self.assertIn("seek", value["unproven"])
        self.assertFalse(value["raw_eof_sent"])
        self.assertFalse(value["audio_endpoint_opened"])

    def test_absent_optional_report_never_claims_decode(self):
        self.assertEqual(audit.stream_decode_report(None, self.catalog),
                         {"present": False, "one_pass_decode_proven": False})
        with tempfile.TemporaryDirectory(prefix="simpsons-audio-audit-") as temporary:
            with self.assertRaisesRegex(ValueError, "missing"):
                audit.stream_decode_report(Path(temporary) / "missing.json", self.catalog)

    def test_partial_and_failed_censuses_are_rejected(self):
        for field, bad in (("complete", False), ("scope", "representative_complete_stream_chains"),
                           ("failed_count", 1), ("completed_blocks", self.report["completed_blocks"] - 1)):
            with self.subTest(field=field):
                value = deepcopy(self.report); value[field] = bad
                with self.assertRaises(ValueError): self.check(value)

    def test_input_and_all_code_dependency_identities_are_required(self):
        for field in ("catalog_sha256", "probe_source_sha256", "driver_source_sha256",
                      "codec_source_sha256", "parser_source_sha256"):
            with self.subTest(field=field):
                value = deepcopy(self.report); value[field] = "0" * 64
                with self.assertRaisesRegex(ValueError, "identity|provenance"): self.check(value)
        value = deepcopy(self.report)
        value["codec_dll_sha256"]["avcodec-simpsonsxma-62.dll"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "DLL provenance"): self.check(value)
        value = deepcopy(self.report); del value["codec_dll_sha256"]["libwinpthread-1.dll"]
        with self.assertRaisesRegex(ValueError, "incomplete"): self.check(value)

    def test_duplicate_files_and_alias_partition_cannot_be_omitted(self):
        value = deepcopy(self.report); value["alias_verification"]["additional_full_source_hashes"] -= 1
        with self.assertRaisesRegex(ValueError, "duplicate source coverage"): self.check(value)
        value = deepcopy(self.report)
        row = next(row for row in value["records"] if len(row["catalog_stream_aliases"]) > 1)
        row["catalog_stream_aliases"].pop()
        with self.assertRaisesRegex(ValueError, "ordered input identity"): self.check(value)

    def test_reassociated_source_and_packet_or_frame_counts_are_rejected(self):
        for field, bad in (("source_sha256", "0" * 64), ("header_hex", "00" * 8),
                           ("packets", [1]), ("raw_frames", [512])):
            with self.subTest(field=field):
                value = deepcopy(self.report); value["records"][0][field] = bad
                with self.assertRaises(ValueError): self.check(value)

    def test_report_hashes_must_match_retained_native_output(self):
        value = deepcopy(self.report)
        value["records"][0]["raw_hashes_fnv64"][0] = str(int(value["records"][0]["raw_hashes_fnv64"][0]) ^ 1)
        with self.assertRaisesRegex(ValueError, "actual native output"): self.check(value)
        with self.assertRaisesRegex(ValueError, "actual native output"):
            self.check(self.report, mutate_log=lambda text: text.replace("\tOK\t", "\tFAIL\t", 1))
        with self.assertRaisesRegex(ValueError, "output is missing"): self.check(self.report, missing_log=True)

    def test_device_or_eof_results_cannot_be_used_as_pure_decode_evidence(self):
        for field in ("raw_eof_sent", "pcm_files_written", "audio_endpoint_opened"):
            with self.subTest(field=field):
                value = deepcopy(self.report); value[field] = True
                with self.assertRaisesRegex(ValueError, "pure raw decode contract"): self.check(value)


if __name__ == "__main__":
    unittest.main()
