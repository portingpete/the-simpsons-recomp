"""Exact authored audio identity and full lifetime markers are required to join."""
import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import export_audio_stream_lifetime_evidence as export


def fixture():
    data=bytearray(4096)
    header='0314bb804024f651';data[1280:1288]=bytes.fromhex(header)
    struct.pack_into('>7I',data,40,0x12345678,0,80,15,8,100,0)
    row=dict(kind='sound_stream',source='audiostreams/menu_mus.mus',ordinal=0,header_offset=1280,
        header_hex=header,audio_offset=1920,audio_bytes=100,samples=1000,blocks=100)
    row['id']=export.audit.identity('sound_stream',row['source'],0,1280)
    marker=('PASS configured original MUS case=0: 50 checks; ordinal=0 header=500 audio=780 '
        'selector=0 blocks=20 decoded=100 normal-releases=0 cancel-releases=20; original owned '
        'file/read/request/mixer/stop/codec free/metadata free/deferred reader retirement/service close/worker join')
    stream=dict(test='OriginalConfiguredMusCancel',catalog_lookup=dict(kind='stream',source=row['source'],
        ordinal=0,header_offset=1280,source_sha256=export.audit.sha(data)),original_record_offset=40,
        original_header_hex=header,original_audio_offset=1920,original_audio_bytes=100,
        selector=0,observed_blocks=20,decoded_samples=100,normal_releases=0,cancellation_releases=20,
        create_use_release=True,pass_output=marker)
    return data,row,stream,marker


class AudioLifetimeEvidenceTests(unittest.TestCase):
    def test_named_identity_is_exact_and_unique(self):
        _,row,stream,_=fixture()
        self.assertEqual(export.selected_row(dict(rows=[row]),stream)['id'],row['id'])
        with self.assertRaisesRegex(ValueError,'not unique'):
            export.selected_row(dict(rows=[row,row]),stream)
        bad=copy.deepcopy(stream);bad['original_audio_offset']+=128
        with self.assertRaisesRegex(ValueError,'span differs'):
            export.selected_row(dict(rows=[row]),bad)
        bad=copy.deepcopy(stream);bad['catalog_lookup']['ordinal']=1
        with self.assertRaisesRegex(ValueError,'not unique'):
            export.selected_row(dict(rows=[row]),bad)

    def test_original_record_header_and_payload_relation_are_rechecked(self):
        data,row,stream,_=fixture();export.validate_binary(row,stream,data)
        bad=bytearray(data);struct.pack_into('>I',bad,48,81)
        altered=copy.deepcopy(stream);altered['catalog_lookup']['source_sha256']=export.audit.sha(bad)
        with self.assertRaisesRegex(ValueError,'association differs'):
            export.validate_binary(row,altered,bad)
        bad=bytearray(data);bad[1280]^=1
        altered['catalog_lookup']['source_sha256']=export.audit.sha(bad)
        with self.assertRaisesRegex(ValueError,'bounds differ'):
            export.validate_binary(row,altered,bad)
        with self.assertRaisesRegex(ValueError,'source hash differs'):
            export.validate_binary(row,stream,bad)

    def test_incomplete_ambiguous_or_changed_marker_cannot_grant_lifetime(self):
        _,row,stream,marker=fixture()
        self.assertEqual(export.passing_output(stream,'prefix\n'+marker+'\n',row),marker)
        for text in (marker.replace('/worker join',''),marker+'\n'+marker,
                     marker.replace('cancel-releases=20','cancel-releases=19')):
            with self.assertRaises(ValueError):export.passing_output(stream,text,row)
        bad=copy.deepcopy(stream);bad['create_use_release']=False
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            export.passing_output(bad,marker,row)

    def test_cancel_and_natural_completion_have_distinct_sample_credit(self):
        _,row,stream,marker=fixture()
        bad=copy.deepcopy(stream);bad['decoded_samples']=row['samples']
        bad['pass_output']=marker.replace('decoded=100','decoded=1000')
        with self.assertRaisesRegex(ValueError,'actual live claims'):
            export.passing_output(bad,bad['pass_output'],row)
        complete=copy.deepcopy(stream);complete.update(test='OriginalConfiguredMusComplete',
            decoded_samples=1000,normal_releases=20,cancellation_releases=0)
        complete_row=dict(row,ordinal=1,blocks=20)
        complete['pass_output']=(marker.replace('case=0','case=1').replace('decoded=100','decoded=1000')
            .replace('ordinal=0','ordinal=1').replace('normal-releases=0','normal-releases=20').replace('cancel-releases=20','cancel-releases=0'))
        self.assertEqual(export.passing_output(complete,complete['pass_output'],complete_row),complete['pass_output'])
        complete['decoded_samples']=999;complete['pass_output']=complete['pass_output'].replace('decoded=1000','decoded=999')
        with self.assertRaisesRegex(ValueError,'authored sample extent'):
            export.passing_output(complete,complete['pass_output'],complete_row)


if __name__=='__main__':unittest.main()
