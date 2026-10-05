"""Offline source-field and malformed-owner regressions; no native credit."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_packaged_mesh_vfx as a
from audit_mission_asset_support import coverage, join_packaged_fields


def chunk(tag, body):
    return struct.pack('<3I', tag, len(body), 0x1C02002D) + body


def pool():
    first = bytearray(400)
    first[:8] = bytes.fromhex('BFBFBFBF01000000')
    struct.pack_into('>5I', first, 8, 400, 0, 16, 5, 1)
    for at, field in enumerate((112, 176, 180, 192, 120)):
        struct.pack_into('>2I', first, 28 + at * 8, field, 0)
    struct.pack_into('>3I', first, 68, 0x3C43A23D, 56, 100)
    struct.pack_into('>6I', first, 100, 0x30002, 0, 0, 164, 1, 324)
    struct.pack_into('>8I', first, 164, 12, 12, 2, 256, 300, 8, 1, 312)
    struct.pack_into('>HHIBBBB', first, 256, 0, 0, 0x2A23B9, 0, 0, 0, 0)
    struct.pack_into('>HHIBBBB', first, 268, 255, 0, 0xFFFFFFFF, 0, 0, 0, 0)
    struct.pack_into('>4I', first, 336, 6, 0xFFFFFFFE, 1, 3)
    return struct.pack('<3I', 16, len(first), 0) + first


def mesh(pools):
    geom = chunk(1, struct.pack('<4I', 0x0801003F, 1, 1, 1))
    geom += chunk(3, b''.join(chunk(0xEA33, p) for p in pools))
    return chunk(16, chunk(1, bytes(12)) + chunk(26, chunk(1, struct.pack('<I', 1)) + chunk(15, geom)))


class PackagedAuditTests(unittest.TestCase):
    def test_source_field_association_preserves_signed_base_and_raw_declaration(self):
        result = a.parse_mesh(mesh([pool()]))
        fields = result['geometries'][0]['native_records'][0]['records'][0]['fields']
        self.assertEqual((fields['stride'], fields['vertex_bytes'], fields['index_bytes']), (12, 12, 8))
        self.assertEqual(fields['declaration'][1]['stream'], 255)
        self.assertEqual(fields['submeshes'][0]['base_vertex'], -2)
        self.assertEqual(fields['submeshes'][0]['primitive'], 6)
        self.assertIsNone(fields['shader_selection'])

    def test_bad_pool_does_not_hide_later_original_record(self):
        damaged = bytearray(pool())
        damaged[12] ^= 1
        result = a.parse_mesh(mesh([damaged, pool()]))
        self.assertEqual(len(result['failures']), 1)
        self.assertEqual(result['geometries'][0]['native_records'][1]['records'][0]['fields']['stride'], 12)

    def test_bad_reference_does_not_hide_later_vfx_row(self):
        data = bytearray(128)
        data[28] = 2
        struct.pack_into('>I', data, 48, 0xFFFFFFFC)
        struct.pack_into('>I', data, 76, 108)
        struct.pack_into('>3I', data, 108, 0x2E707274, 7, 0x10)
        result = a.parse_vfx(data)
        self.assertEqual(len(result['failures']), 1)
        self.assertEqual(result['rows'][1]['module_key'], '2E707274')
        self.assertIsNone(result['rows'][1]['stride'])

    def test_unrelocated_pointer_never_becomes_guessed_packaged_offset(self):
        damaged = bytearray(pool())
        struct.pack_into('>I', damaged, 12 + 28, 116)
        result = a.parse_mesh(mesh([damaged]))
        self.assertIn('no original relocation entry', result['failures'][0]['reason'])

    def test_truncated_owner_and_chunk_bounds_reject(self):
        for data in (b'', bytes(47), bytes(48) + b'\0'):
            if len(data) < 48:
                with self.assertRaises(ValueError):
                    a.parse_vfx(data)
        with self.assertRaises(ValueError):
            a.parse_mesh(mesh([pool()])[:-1])
        damaged = bytearray(pool())
        struct.pack_into('<I', damaged, 4, 401)
        with self.assertRaises(ValueError):
            a.native_pool(damaged)

    def test_unknown_metadata_is_retained_without_fields(self):
        damaged = bytearray(pool())
        struct.pack_into('>I', damaged, 12 + 100, 0x30003)
        fields = a.parse_mesh(mesh([damaged]))['geometries'][0]['native_records'][0]['records'][0]['fields']
        self.assertEqual(fields['status'], 'unqualified_metadata_version')
        self.assertIsNone(fields['declaration'])

    def test_original_zero_geometry_list_is_retained_as_empty(self):
        result = a.parse_mesh(chunk(16, chunk(1, bytes(12)) + chunk(26, chunk(1, bytes(4)))))
        self.assertTrue(result['empty_geometry_list'])
        self.assertEqual(result['failures'], [])
        self.assertEqual(result['geometries'], [])

    def test_repeated_entry_rechecks_each_named_identity_and_continues(self):
        payload = bytes(96)
        entry = dict(index=0, encoding='raw', file_offset=0, stored_size=len(payload),
                     decoded_size=len(payload), decoded_sha256=a.sha(payload),
                     chunks=[dict(type_name='VFX', payload_decoded_offset=0, payload_size=48,
                                  payload_sha256=a.sha(payload[:48]), name='first.vfx')])
        wrong_entry = copy.deepcopy(entry)
        wrong_entry.update(index=1, decoded_sha256='0' * 64)
        wrong_payload = copy.deepcopy(entry)
        wrong_payload['index'] = 2
        wrong_payload['chunks'][0].update(name='bad.vfx', payload_sha256='0' * 64)
        different_span = copy.deepcopy(entry)
        different_span['index'] = 3
        different_span['chunks'][0].update(name='second.vfx', payload_decoded_offset=48)
        later_valid = copy.deepcopy(entry)
        later_valid['index'] = 4
        later_valid['chunks'][0]['name'] = 'later.vfx'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / 'fixture.str').write_bytes(payload)
            assets = dict(files=[dict(path='fixture.str', sha256=a.sha(payload), inspection=dict(
                entries=[entry, wrong_entry, wrong_payload, different_span, later_valid]))])
            rows, failures, summary = a.audit(assets, root)
        self.assertEqual(len(failures), 1)
        self.assertEqual(failures[0]['entry'], 1)
        self.assertEqual([r['name'] for r in rows], ['first.vfx', 'bad.vfx', 'second.vfx', 'later.vfx'])
        self.assertEqual(rows[1]['parameters']['reason'], 'Original named payload hash differs')
        self.assertEqual(summary['qualification_failures'], 1)
        self.assertEqual(summary['unique_decoded_entries'], 1)
        for row in (rows[0], rows[2], rows[3]):
            self.assertEqual(row['parameters']['status'], 'source_pinned_fields')
            self.assertFalse(row['native_use_tested'])

    def test_exact_occurrence_join_never_grants_native_coverage(self):
        resource = dict(kind='geometry', source='loc/loc.str', entry=2, payload_decoded_offset=144,
                        name='a.rws', payload_sha256='a' * 64, payload_bytes=12, coverage=coverage())
        field_row = {k: resource[k] for k in resource if k != 'coverage'}
        field_row['parameters'] = dict(status='source_pinned_fields', stride=48, shader_selection=None)
        report = dict(schema_version=1, scope='source_pinned_offline_packaged_fields', catalog_sha256='catalog', rows=[field_row],
                      image_sha256=a.IMAGE_SHA, tool_sha256=a.sha(Path(a.__file__).read_bytes()))
        result = join_packaged_fields([resource], report, 'catalog')
        self.assertEqual(result['joined'], 1)
        self.assertEqual(resource['parameters']['stride'], 48)
        self.assertEqual(resource['coverage'], coverage())
        for key, value in [('payload_decoded_offset', 148), ('source', 'brt/brt.str'), ('payload_sha256', 'b' * 64)]:
            wrong = copy.deepcopy(report)
            wrong['rows'][0][key] = value
            with self.assertRaises(ValueError):
                join_packaged_fields([resource], wrong, 'catalog')
        wrong = copy.deepcopy(report)
        wrong['rows'][0]['native_use_tested'] = True
        with self.assertRaises(ValueError):
            join_packaged_fields([resource], wrong, 'catalog')

    def test_complete_original_span_pins_detect_each_mutation(self):
        image = (a.ROOT / 'analysis/simpsons.pe').read_bytes()
        a.verify(image)
        for name, (start, end, _) in a.SPANS.items():
            for address in (start, end - 1):
                damaged = bytearray(image)
                damaged[address - a.BASE] ^= 1
                with self.assertRaisesRegex(ValueError, name):
                    a.verify(damaged, check_hash=False)


if __name__ == '__main__':
    unittest.main()
