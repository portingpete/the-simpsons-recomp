from __future__ import annotations
import hashlib
import json
import re
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
import inline_fast_memory_generation as generation


class InlineFastMemoryGeneration(unittest.TestCase):
    def test_current_selections_are_exact_and_idempotent(self):
        selections = json.loads((ROOT/"config/inline_fast_memory.json").read_text())
        self.assertEqual({s["entry"] for s in selections},{"823F2D98","8270A590","8281E9B0","82B2A8D0","82844AB0","82B2A1C0","82A3D1D8"})
        for selected in selections:
            text = (ROOT/f"build/generated/ppc_recomp.{selected['chunk']}.cpp").read_text()
            result,coverage = generation.transform(text,[selected])
            second,again = generation.transform(result,[selected])
            self.assertEqual(result,second)
            self.assertEqual(coverage,again)
            before = generation.original_body(text)
            after = generation.original_body(result).replace("\n"+generation.HEADER,"")
            self.assertEqual(before.replace("\n"+generation.HEADER,""),after)
            start,end = generation.function_span(result,selected["entry"])
            for operation,count in selected["operations"].items():
                name = generation.OPERATIONS[operation]
                self.assertEqual(generation.code_only(result[start:end]).count(name),count)
            # Other qualified bodies may already use the same inline identifiers.
            # Compare the untouched ranges, allowing only shared-header insertion.
            old_start,old_end = generation.function_span(text,selected["entry"])
            prefix=text[:old_start]
            if generation.HEADER not in text:
                shared='#include "ppc_recomp_shared.h"'
                prefix=prefix.replace(shared,shared+"\n"+generation.HEADER,1)
            self.assertEqual(result[:start],prefix)
            self.assertEqual(result[end:],text[old_end:])

    def fixture(self,operation="PPC_LOAD_U32"):
        body = ("PPC_FUNC_IMPL(__imp__sub_12345678) {\n"
                " // PPC_LOAD_U32(0) { misleading comment\n"
                " const char* s=\"PPC_STORE_U32(0,0) }\";\n"
                f" ctx.r3.u32={operation}(ctx.r4.u32);\n"
                " if(ctx.r5.u32) { PPC_STORE_U32(ctx.r6.u32,ctx.r3.u32); }\n}")
        selected = {"entry":"12345678","body_sha256":generation.body_digest(body),
                    "operations":{"PPC_LOAD_U32":1,"PPC_STORE_U32":1}}
        return '#include "ppc_recomp_shared.h"\n'+body,selected

    def test_comments_literals_and_nested_braces_unchanged(self):
        text,selected = self.fixture()
        transformed,_ = generation.transform(text,[selected])
        self.assertIn('// PPC_LOAD_U32(0) { misleading comment',transformed)
        self.assertIn('"PPC_STORE_U32(0,0) }"',transformed)
        self.assertEqual(generation.original_body(transformed).replace("\n"+generation.HEADER,""),text)

    def test_hash_counts_duplicates_and_unsupported_width_fail_closed(self):
        text,selected = self.fixture()
        with self.assertRaisesRegex(RuntimeError,"body changed"):
            generation.transform(text.replace("if(ctx.r5.u32)","if(ctx.r7.u32)"),[selected])
        with self.assertRaisesRegex(RuntimeError,"counts changed"):
            generation.transform(text,[{**selected,"operations":{"PPC_LOAD_U32":2,"PPC_STORE_U32":1}}])
        with self.assertRaisesRegex(RuntimeError,"duplicate"):
            generation.transform(text,[selected,selected])
        text,selected = self.fixture("PPC_LOAD_U64")
        with self.assertRaisesRegex(RuntimeError,"unsupported access"):
            generation.transform(text,[selected])

    def byte_fixture(self):
        body = (generation.BYTE_PROLOGUE+
                '\t// PPC_LOAD_U8(0) and SIMPSONS_PROFILE_GUEST_SCOPE("comment")\n'
                '\tconst char* literal="PPC_STORE_U8(0,0) }";\n'
                '\tctx.r3.u64=PPC_LOAD_U8(ctx.r4.u32);\n'
                '\tif(ctx.r5.u32) { PPC_STORE_U8(ctx.r6.u32,ctx.r3.u8); }\n'
                '\tctx.r7.u64=PPC_LOAD_U32(ctx.r8.u32);\n'
                '\tPPC_STORE_U32(ctx.r9.u32,ctx.r7.u32);\n}')
        selected = {"entry":generation.BYTE_ENTRY,"body_sha256":generation.body_digest(body),
                    "operations":{name:1 for name in ("PPC_LOAD_U32","PPC_STORE_U32","PPC_LOAD_U8","PPC_STORE_U8")}}
        neighbor='\nPPC_FUNC_IMPL(__imp__sub_8281EBF8) { PPC_STORE_U8(0,PPC_LOAD_U8(0)); }\n'
        return '#include "ppc_recomp_shared.h"\n'+body+neighbor,selected

    def test_mixed_byte_word_scope_and_neighbor_contract(self):
        text,selected = self.byte_fixture()
        for scoped in (False,True):
            source = text.replace(generation.BYTE_PROLOGUE,
                                  generation.BYTE_PROLOGUE+generation.BYTE_SCOPE,1) if scoped else text
            result,coverage = generation.transform(source,[selected])
            self.assertEqual(generation.transform(result,[selected]),(result,coverage))
            self.assertEqual(generation.original_body(result).replace("\n"+generation.HEADER,""),source)
            self.assertEqual(result.count(generation.BYTE_SCOPE),int(scoped))
            start,end = generation.function_span(result,generation.BYTE_ENTRY)
            self.assertEqual(generation.body_digest(generation.original_body(result[start:end],generation.BYTE_ENTRY)),
                             selected["body_sha256"])
            self.assertIn('PPC_STORE_U8(0,PPC_LOAD_U8(0))',result[end:])

    def test_unexpected_scope_width_or_instruction_fails_closed(self):
        text,selected = self.byte_fixture()
        scoped = text.replace(generation.BYTE_PROLOGUE,generation.BYTE_PROLOGUE+generation.BYTE_SCOPE,1)
        for malformed in (scoped.replace('"Guest.byteDecode8281E9B0"','"Guest.other"',1),
                          scoped.replace(generation.BYTE_SCOPE,generation.BYTE_SCOPE*2,1),
                          scoped.replace(generation.BYTE_SCOPE,' '+generation.BYTE_SCOPE,1),
                          scoped.replace(generation.BYTE_SCOPE,'',1).replace('\n}', '\n'+generation.BYTE_SCOPE+'}',1)):
            with self.assertRaisesRegex(RuntimeError,"unexpected guest scope"):
                generation.transform(malformed,[selected])
        with self.assertRaisesRegex(RuntimeError,"body changed"):
            generation.transform(scoped.replace('ctx.r6.u32','ctx.r10.u32'),[selected])
        for operation in ('PPC_LOAD_U64','PPC_LOAD_U128'):
            body=text.replace('PPC_LOAD_U8(ctx.r4.u32)',operation+'(ctx.r4.u32)')
            start,end=generation.function_span(body,generation.BYTE_ENTRY)
            changed={**selected,"body_sha256":generation.body_digest(body[start:end])}
            with self.assertRaisesRegex(RuntimeError,"unsupported access"):
                generation.transform(body,[changed])
        with self.assertRaisesRegex(RuntimeError,"counts changed"):
            generation.transform(scoped,[{**selected,"operations":{**selected["operations"],"PPC_STORE_U8":2}}])

    def pair_fixture(self):
        body = (generation.PAIR_PROLOGUE+
                '\t// PPC_LOAD_U16(0) and SIMPSONS_PROFILE_GUEST_SCOPE("comment")\n'
                '\tconst char* literal="PPC_STORE_U16(0,0) }";\n'
                '\tctx.r12.u64 = ctx.lr;\n\t__savegprlr_25(ctx, base);\n'
                '\tctx.r11.u64=PPC_LOAD_U32(ctx.r30.u32+8);\n'
                '\tif(ctx.cr6.lt) { sub_82B64600(ctx, base); }\n'
                '\tctx.r6.u64=PPC_LOAD_U16(ctx.r9.u32+0);\n'
                '\tPPC_STORE_U16(ctx.r8.u32+0,ctx.r6.u16);\n'
                '\tPPC_STORE_U32(ctx.r30.u32+4,ctx.r31.u32);\n'
                '\t__restgprlr_25(ctx, base);\n}')
        selected = {"entry":generation.PAIR_ENTRY,"body_sha256":generation.body_digest(body),
                    "operations":{name:1 for name in ("PPC_LOAD_U32","PPC_STORE_U32","PPC_LOAD_U16","PPC_STORE_U16")}}
        neighbor='\nPPC_FUNC_IMPL(__imp__sub_82B2AA08) { PPC_STORE_U16(0,PPC_LOAD_U16(0)); }\n'
        return '#include "ppc_recomp_shared.h"\n'+body+neighbor,selected

    def test_halfword_word_pair_scope_calls_and_neighbor_contract(self):
        text,selected = self.pair_fixture()
        for scoped in (False,True):
            source = text.replace(generation.PAIR_PROLOGUE,
                                  generation.PAIR_PROLOGUE+generation.PAIR_SCOPE,1) if scoped else text
            result,coverage = generation.transform(source,[selected])
            self.assertEqual(generation.transform(result,[selected]),(result,coverage))
            self.assertEqual(generation.original_body(result).replace("\n"+generation.HEADER,""),source)
            self.assertEqual(result.count(generation.PAIR_SCOPE),int(scoped))
            start,end = generation.function_span(result,generation.PAIR_ENTRY)
            original=generation.original_body(result[start:end],generation.PAIR_ENTRY)
            self.assertEqual(generation.body_digest(original),selected["body_sha256"])
            self.assertIn('__savegprlr_25(ctx, base);',result[start:end])
            self.assertIn('sub_82B64600(ctx, base);',result[start:end])
            self.assertIn('__restgprlr_25(ctx, base);',result[start:end])
            self.assertIn('PPC_STORE_U16(0,PPC_LOAD_U16(0))',result[end:])
            for operation,count in selected["operations"].items():
                self.assertEqual(generation.code_only(result[start:end]).count(generation.OPERATIONS[operation]),count)

    def test_pair_scope_and_nonleaf_instruction_changes_fail_closed(self):
        text,selected = self.pair_fixture()
        scoped=text.replace(generation.PAIR_PROLOGUE,generation.PAIR_PROLOGUE+generation.PAIR_SCOPE,1)
        for malformed in (scoped.replace('"Guest.pairShift82B2A8D0"','"Guest.pairOwner82B2ABD8"',1),
                          scoped.replace(generation.PAIR_SCOPE,generation.BYTE_SCOPE,1),
                          scoped.replace(generation.PAIR_SCOPE,generation.PAIR_SCOPE*2,1),
                          scoped.replace(generation.PAIR_SCOPE,' '+generation.PAIR_SCOPE,1),
                          scoped.replace(generation.PAIR_SCOPE,'',1).replace('\n}', '\n'+generation.PAIR_SCOPE+'}',1)):
            with self.assertRaisesRegex(RuntimeError,"unexpected guest scope"):
                generation.transform(malformed,[selected])
        for before,after in (('sub_82B64600','sub_82B64604'),
                             ('__savegprlr_25','__savegprlr_26'),
                             ('__restgprlr_25','__restgprlr_26'),
                             ('if(ctx.cr6.lt)','if(ctx.cr6.gt)'),
                             ('ctx.r8.u32+0','ctx.r8.u32+2')):
            with self.assertRaisesRegex(RuntimeError,"body changed"):
                generation.transform(scoped.replace(before,after,1),[selected])
        with self.assertRaisesRegex(RuntimeError,"counts changed"):
            generation.transform(scoped,[{**selected,"operations":{**selected["operations"],"PPC_STORE_U16":2}}])
        unsupported=text.replace('PPC_LOAD_U16(ctx.r9.u32+0)','PPC_LOAD_U64(ctx.r9.u32+0)')
        start,end=generation.function_span(unsupported,generation.PAIR_ENTRY)
        with self.assertRaisesRegex(RuntimeError,"unsupported access"):
            generation.transform(unsupported,[{**selected,"body_sha256":generation.body_digest(unsupported[start:end])}])


    def scalar_copy_fixture(self):
        text=(ROOT/'build/generated/ppc_recomp.239.cpp').read_text()
        marker='PPC_FUNC_IMPL(__imp__sub_82A3D1D8) {'
        self.assertEqual(text.count(marker),1)
        start=text.index(marker);end=text.index('\n}',start)+2
        self.assertEqual(generation.function_span(text,'82A3D1D8'),(start,end))
        # Independent reversal of this leaf's four permitted scalar identifiers.
        body=re.sub(r'\bPPC_FAST_INLINE_(LOAD_U8|STORE_U8|LOAD_U32|STORE_U32)\b',
                    lambda match:'PPC_'+match.group(1),text[start:end])
        selected=next(row for row in json.loads((ROOT/'config/inline_fast_memory.json').read_text())
                      if row['entry']=='82A3D1D8')
        return text,start,end,body,selected

    def test_actual_scalar_copy_exact_sites_read_order_and_neighbors(self):
        text,start,end,body,selected=self.scalar_copy_fixture()
        expected={'chunk':239,'entry':'82A3D1D8',
                  'body_sha256':'62acfdc473f3817df8defe77314e1962ee7de9a2b7fa1883e8f2d754e684fb0e',
                  'operations':{'PPC_LOAD_U8':6,'PPC_STORE_U8':2,'PPC_LOAD_U32':1,'PPC_STORE_U32':2}}
        self.assertEqual(selected,expected)
        self.assertEqual(len(body.encode('utf-8')),3021)
        self.assertEqual(hashlib.sha256(body.encode('utf-8')).hexdigest(),expected['body_sha256'])
        operations=re.findall(r'\bPPC_(?:LOAD|STORE)_U\d+\b',body)
        self.assertEqual({name:operations.count(name) for name in set(operations)},expected['operations'])
        self.assertEqual(len(operations),11)
        self.assertNotRegex(body,r'PPCGuestPointer|simde_|ctx\.f\d|ctx\.v\d|(?:sub_|__save|__rest)\w+\(ctx, base\)')
        order=('loc_82A3D250:',
               'PPC_LOAD_U8(ctx.r4.u32 + 3)','PPC_LOAD_U8(ctx.r4.u32 + 2)',
               'PPC_LOAD_U8(ctx.r4.u32 + 1)','PPC_LOAD_U8(ctx.r4.u32 + 0)',
               'ctx.r4.s64 = ctx.r4.s64 + 4;',
               'PPC_STORE_U32(ctx.r6.u32 + 0, ctx.r7.u32)',
               'ctx.r6.s64 = ctx.r6.s64 + 4;',
               'if (ctx.ctr.u32 != 0) goto loc_82A3D250;')
        unaligned=body[body.index(order[0]):]
        positions=[unaligned.index(statement) for statement in order]
        self.assertEqual(positions,sorted(positions))
        expected_body=re.sub(r'\b(?:PPC_LOAD_U8|PPC_STORE_U8|PPC_LOAD_U32|PPC_STORE_U32)\b',
                             lambda match:generation.OPERATIONS[match.group()],body)
        result,coverage=generation.transform(text,[selected])
        prefix=text[:start]
        if generation.HEADER not in text:
            shared='#include "ppc_recomp_shared.h"'
            prefix=prefix.replace(shared,shared+'\n'+generation.HEADER,1)
        self.assertEqual(result,prefix+expected_body+text[end:])
        self.assertEqual(generation.transform(result,[selected]),(result,coverage))
        self.assertEqual(coverage,[{key:expected[key] for key in ('entry','body_sha256','operations')}])
        after_start,after_end=generation.function_span(result,'82A3D1D8')
        restored=re.sub(r'\bPPC_FAST_INLINE_(LOAD_U8|STORE_U8|LOAD_U32|STORE_U32)\b',
                        lambda match:'PPC_'+match.group(1),result[after_start:after_end])
        self.assertEqual(restored,body)
        self.assertEqual(result[:after_start],prefix)
        self.assertEqual(result[after_end:],text[end:])

    def test_scalar_copy_body_read_width_and_counts_fail_closed(self):
        text,start,end,body,selected=self.scalar_copy_fixture()
        for before,after in (
                ('ctx.r0.s64 = ctx.r5.s64 + 1;','ctx.r0.s64 = ctx.r5.s64 + 2;'),
                ('PPC_LOAD_U8(ctx.r4.u32 + 3)','PPC_LOAD_U8(ctx.r4.u32 + 0)'),
                ('PPC_STORE_U32(ctx.r6.u32 + 0, ctx.r7.u32)','PPC_STORE_U32(ctx.r6.u32 + 4, ctx.r7.u32)'),
                ('if (ctx.ctr.u32 != 0) goto loc_82A3D250;','if (ctx.ctr.u32 == 0) goto loc_82A3D250;')):
            self.assertIn(before,body)
            changed=text[:start]+body.replace(before,after,1)+text[end:]
            with self.subTest(instruction=before),self.assertRaisesRegex(RuntimeError,'body changed'):
                generation.transform(changed,[selected])
        with self.assertRaisesRegex(RuntimeError,'counts changed'):
            generation.transform(text,[{**selected,'operations':{**selected['operations'],'PPC_LOAD_U8':5}}])
        unsupported=body.replace('PPC_LOAD_U32(ctx.r4.u32 + 0)','PPC_LOAD_U64(ctx.r4.u32 + 0)',1)
        source=text[:start]+unsupported+text[end:]
        with self.assertRaisesRegex(RuntimeError,'unsupported access'):
            generation.transform(source,[{**selected,'body_sha256':hashlib.sha256(unsupported.encode()).hexdigest()}])

    def pair_bitset_fixture(self):
        text=(ROOT/'build/generated/ppc_recomp.265.cpp').read_text()
        marker='PPC_FUNC_IMPL(__imp__sub_82B2A1C0) {'
        self.assertEqual(text.count(marker),1)
        start=text.index(marker)
        # This exact recovered body has a single column-zero closing brace.
        end=text.index('\n}',start)+2
        self.assertEqual(generation.function_span(text,'82B2A1C0'),(start,end))
        body=re.sub(r'\bPPC_FAST_INLINE_(LOAD_U16|LOAD_U32|STORE_U32)\b',
                    lambda match:'PPC_'+match.group(1),text[start:end])
        selections=json.loads((ROOT/'config/inline_fast_memory.json').read_text())
        selected=next(row for row in selections if row['entry']=='82B2A1C0')
        return text,start,end,body,selected

    def test_actual_pair_bitset_exact_sites_vectors_order_and_neighbors(self):
        text,start,end,body,selected=self.pair_bitset_fixture()
        expected={'chunk':265,'entry':'82B2A1C0',
                  'body_sha256':'4c3fed32a6b29f9855317b2c9d680aa7c51a11e9c49ceab8caf112a019685bb8',
                  'operations':{'PPC_LOAD_U16':13,'PPC_LOAD_U32':15,'PPC_STORE_U32':5}}
        self.assertEqual(selected,expected)
        self.assertEqual(len(body.encode('utf-8')),10123)
        self.assertEqual(hashlib.sha256(body.encode('utf-8')).hexdigest(),expected['body_sha256'])
        operations=re.findall(r'\bPPC_(?:LOAD|STORE)_U\d+\b',body)
        self.assertEqual({name:operations.count(name) for name in set(operations)},expected['operations'])
        self.assertEqual(len(operations),33)
        vectors=(
            '\tsimde_mm_store_si128((simde__m128i*)ctx.v0.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)PPCGuestPointer(base, uint32_t(ctx.r11.u32) & ~0xFu, 16, false)), simde_mm_load_si128((simde__m128i*)VectorMaskL)));',
            '\tsimde_mm_store_si128((simde__m128i*)PPCGuestPointer(base, uint32_t(ctx.r9.u32) & ~0xFu, 16, true), simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)ctx.v0.u8), simde_mm_load_si128((simde__m128i*)VectorMaskL)));')
        self.assertEqual(tuple(line for line in body.splitlines() if 'PPCGuestPointer(' in line),vectors)
        order=('PPC_FUNC_PROLOGUE();','PPC_TRACE_ENTRY(ctx, 0x82B2A1C0);',
               '__savegprlr_29(ctx, base);','PPC_LOAD_U16(ctx.r6.u32 + 8)',
               'PPC_LOAD_U16(ctx.r6.u32 + 10)',vectors[0],
               'simde_mm_store_si128((simde__m128i*)ctx.v0.u8, simde_mm_setzero_si128());',
               vectors[1],'PPC_LOAD_U32(ctx.r3.u32 + 160)','__restgprlr_29(ctx, base);')
        positions=[body.index(statement) for statement in order]
        self.assertEqual(positions,sorted(positions))
        for statement in order[:3]+order[-1:]:
            self.assertEqual(body.count(statement),1)
        replacements={'PPC_LOAD_U16':'PPC_FAST_INLINE_LOAD_U16',
                      'PPC_LOAD_U32':'PPC_FAST_INLINE_LOAD_U32',
                      'PPC_STORE_U32':'PPC_FAST_INLINE_STORE_U32'}
        expected_body=re.sub(r'\b(?:PPC_LOAD_U16|PPC_LOAD_U32|PPC_STORE_U32)\b',
                             lambda match:replacements[match.group()],body)
        self.assertIn(generation.HEADER,text)  # Existing qualified PairShift shares this chunk.
        result,coverage=generation.transform(text,[selected])
        self.assertEqual(result,text[:start]+expected_body+text[end:])
        self.assertEqual(generation.transform(result,[selected]),(result,coverage))
        self.assertEqual(coverage,[{key:expected[key] for key in ('entry','body_sha256','operations')}])
        after_start,after_end=generation.function_span(result,'82B2A1C0')
        restored=re.sub(r'\bPPC_FAST_INLINE_(LOAD_U16|LOAD_U32|STORE_U32)\b',
                        lambda match:'PPC_'+match.group(1),result[after_start:after_end])
        self.assertEqual(restored,body)
        self.assertEqual(tuple(line for line in result[after_start:after_end].splitlines()
                               if 'PPCGuestPointer(' in line),vectors)
        self.assertEqual(result[:after_start],text[:start])
        self.assertEqual(result[after_end:],text[end:])
        selections=json.loads((ROOT/'config/inline_fast_memory.json').read_text())
        pair=next(row for row in selections if row['entry']==generation.PAIR_ENTRY)
        both,both_coverage=generation.transform(text,[pair,selected])
        self.assertEqual(both,generation.transform(text,[selected,pair])[0])
        self.assertEqual(both_coverage[0],{key:pair[key] for key in ('entry','body_sha256','operations')})
        self.assertEqual(both_coverage[1],coverage[0])
        entries={row['entry'] for row in selections}
        self.assertIn('82A3D1D8',entries)
        self.assertNotIn('82B3D5E8',entries)
        self.assertEqual(set(generation.OPERATIONS),
                         {'PPC_LOAD_U8','PPC_STORE_U8','PPC_LOAD_U16','PPC_STORE_U16','PPC_LOAD_U32','PPC_STORE_U32'})

    def test_pair_bitset_instruction_vector_width_and_counts_fail_closed(self):
        text,start,end,body,selected=self.pair_bitset_fixture()
        source=text[:start]+body+text[end:]
        for before,after in (
                ('__savegprlr_29(ctx, base);','__savegprlr_28(ctx, base);'),
                ('__restgprlr_29(ctx, base);','__restgprlr_28(ctx, base);'),
                ('PPC_TRACE_ENTRY(ctx, 0x82B2A1C0);','PPC_TRACE_ENTRY(ctx, 0x82B2A1C4);'),
                ('PPC_LOAD_U16(ctx.r6.u32 + 8)','PPC_LOAD_U16(ctx.r6.u32 + 6)'),
                ('ctx.r11.u32) & ~0xFu, 16, false','ctx.r11.u32) & ~0xFu, 8, false'),
                ('ctx.r9.u32) & ~0xFu, 16, true','ctx.r9.u32) & ~0xFu, 16, false'),
                ('simde_mm_setzero_si128()','simde_mm_set1_epi32(1)'),
                ('ctx.r9.u64 = ctx.r9.u64 ^ ctx.r6.u64;','ctx.r9.u64 = ctx.r9.u64 | ctx.r6.u64;'),
                ('if (ctx.cr6.lt) goto loc_82B2A204;','if (ctx.cr6.gt) goto loc_82B2A204;')):
            self.assertIn(before,body)
            changed=text[:start]+body.replace(before,after,1)+text[end:]
            with self.subTest(instruction=before),self.assertRaisesRegex(RuntimeError,'body changed'):
                generation.transform(changed,[selected])
        scoped=source.replace('PPC_TRACE_ENTRY(ctx, 0x82B2A1C0);\n',
                              'PPC_TRACE_ENTRY(ctx, 0x82B2A1C0);\n'+generation.PAIR_SCOPE,1)
        with self.assertRaisesRegex(RuntimeError,'unexpected guest scope'):
            generation.transform(scoped,[selected])
        for operation in selected['operations']:
            with self.subTest(operation=operation),self.assertRaisesRegex(RuntimeError,'counts changed'):
                generation.transform(source,[{**selected,'operations':{**selected['operations'],
                    operation:selected['operations'][operation]+1}}])
        with self.assertRaisesRegex(RuntimeError,'counts changed'):
            generation.transform(source,[{**selected,'operations':{**selected['operations'],'PPC_LOAD_U8':0}}])
        changed_body=body.replace('PPC_LOAD_U16(ctx.r6.u32 + 8)','PPC_LOAD_U32(ctx.r6.u32 + 8)',1)
        with self.assertRaisesRegex(RuntimeError,'counts changed'):
            generation.transform(text[:start]+changed_body+text[end:],[{**selected,
                'body_sha256':hashlib.sha256(changed_body.encode('utf-8')).hexdigest()}])
        changed_body=body.replace('PPC_LOAD_U16(ctx.r6.u32 + 8)','PPC_LOAD_U64(ctx.r6.u32 + 8)',1)
        with self.assertRaisesRegex(RuntimeError,'unsupported access'):
            generation.transform(text[:start]+changed_body+text[end:],[{**selected,
                'body_sha256':hashlib.sha256(changed_body.encode('utf-8')).hexdigest()}])

    def vm_fixture(self):
        text=(ROOT/'build/generated/ppc_recomp.182.cpp').read_text()
        start,end=generation.function_span(text,generation.VM_ENTRY)
        body=generation.original_body(text[start:end],generation.VM_ENTRY)
        selections=json.loads((ROOT/'config/inline_fast_memory.json').read_text())
        selected=next(row for row in selections if row['entry']==generation.VM_ENTRY)
        neighbor='\nPPC_FUNC_IMPL(__imp__sub_828449E0) { PPC_STORE_U32(0,PPC_LOAD_U32(0)); }\n'
        return '#include "ppc_recomp_shared.h"\n'+body+neighbor,selected

    def test_vm_exact_sites_scope_calls_and_unselected_callee_preserved(self):
        text,selected=self.vm_fixture()
        self.assertEqual(selected['chunk'],182)
        self.assertEqual(selected['body_sha256'],'7cf373f8be651007627f714455e8c9a7479aa03312674d8ee2ee57a2360aebfb')
        self.assertEqual(selected['operations'],{'PPC_LOAD_U32':36,'PPC_STORE_U32':17,'PPC_LOAD_U16':2,'PPC_LOAD_U8':6})
        self.assertEqual(sum(selected['operations'].values()),61)
        for scoped in (False,True):
            source=text.replace(generation.VM_PROLOGUE,generation.VM_PROLOGUE+generation.VM_SCOPE,1) if scoped else text
            result,coverage=generation.transform(source,[selected])
            self.assertEqual(generation.transform(result,[selected]),(result,coverage))
            self.assertEqual(generation.original_body(result).replace('\n'+generation.HEADER,''),source)
            self.assertEqual(result.count(generation.VM_SCOPE),int(scoped))
            start,end=generation.function_span(result,generation.VM_ENTRY)
            original=generation.original_body(result[start:end],generation.VM_ENTRY)
            self.assertEqual(generation.body_digest(original),selected['body_sha256'])
            self.assertEqual(coverage,[{'entry':generation.VM_ENTRY,'body_sha256':selected['body_sha256'],
                                        'operations':selected['operations']}])
            for operation,count in selected['operations'].items():
                self.assertEqual(generation.code_only(result[start:end]).count(generation.OPERATIONS[operation]),count)
            for unchanged in ('__savegprlr_28(ctx, base);','__restgprlr_28(ctx, base);',
                              'sub_828449E0(ctx, base);','sub_82A43D30(ctx, base);',
                              'sub_82A41FA0(ctx, base);','sub_828445D8(ctx, base);',
                              'sub_8283E360(ctx, base);','sub_82844838(ctx, base);',
                              'sub_828444D8(ctx, base);','sub_82844738(ctx, base);',
                              'PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);'):
                self.assertEqual(result[start:end].count(unchanged),original.count(unchanged))
                self.assertGreater(original.count(unchanged),0)
            self.assertIn('PPC_STORE_U32(0,PPC_LOAD_U32(0))',result[end:])
            self.assertNotIn('PPC_FAST_INLINE_',result[end:])

    def test_vm_scope_nonlocal_calls_control_flow_counts_fail_closed(self):
        text,selected=self.vm_fixture()
        scoped=text.replace(generation.VM_PROLOGUE,generation.VM_PROLOGUE+generation.VM_SCOPE,1)
        for malformed in (scoped.replace(generation.VM_SCOPE,generation.BYTE_SCOPE,1),
                          scoped.replace('"Guest.vmSetup82844AB0"','"Guest.vmOuter828389F0"',1),
                          scoped.replace(generation.VM_SCOPE,generation.VM_SCOPE*2,1),
                          scoped.replace(generation.VM_SCOPE,' '+generation.VM_SCOPE,1),
                          scoped.replace('PPC_TRACE_ENTRY(ctx, 0x82844AB0);','PPC_TRACE_ENTRY(ctx, 0x82844AB4);',1),
                          scoped.replace(generation.VM_SCOPE,'',1).replace('\tuint32_t ea{};\n',
                              '\tuint32_t ea{};\n'+generation.VM_SCOPE,1)):
            with self.assertRaisesRegex(RuntimeError,'unexpected guest scope'):
                generation.transform(malformed,[selected])
        for before,after in (('__savegprlr_28(ctx, base);','__savegprlr_29(ctx, base);'),
                             ('__restgprlr_28(ctx, base);','__restgprlr_29(ctx, base);'),
                             ('sub_82A43D30(ctx, base);','sub_82A43D34(ctx, base);'),
                             ('sub_828444D8(ctx, base);','sub_828444DC(ctx, base);'),
                             ('PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);','PPC_CALL_INDIRECT_FUNC(ctx.r11.u32);'),
                             ('if (ctx.cr6.eq) goto loc_82844AE0;','if (ctx.cr6.gt) goto loc_82844AE0;'),
                             ('ctx.r29.u32 + 0','ctx.r29.u32 + 4')):
            self.assertIn(before,scoped)
            with self.assertRaisesRegex(RuntimeError,'body changed'):
                generation.transform(scoped.replace(before,after,1),[selected])
        for operation in selected['operations']:
            with self.assertRaisesRegex(RuntimeError,'counts changed'):
                generation.transform(scoped,[{**selected,'operations':{**selected['operations'],
                    operation:selected['operations'][operation]+1}}])
        with self.assertRaisesRegex(RuntimeError,'counts changed'):
            generation.transform(scoped,[{**selected,'operations':{**selected['operations'],'PPC_STORE_U16':0}}])

    def test_actual_vm_outer_retains_unsupported_u64_and_unrecognized_scope(self):
        text=(ROOT/'build/generated/ppc_recomp.181.cpp').read_text()
        start,end=generation.function_span(text,'828389F0')
        scope='\tSIMPSONS_PROFILE_GUEST_SCOPE("Guest.vmOuter828389F0");\n'
        self.assertEqual(text[start:end].count(scope),1)
        unprofiled=generation.original_body(text[start:end]).replace(scope,'',1)
        accesses=[m.group() for m in generation.MEMORY.finditer(generation.code_only(unprofiled))]
        self.assertEqual(accesses.count('PPC_LOAD_U64'),2)
        self.assertEqual(accesses.count('PPC_STORE_U64'),2)
        selected={'entry':'828389F0','body_sha256':generation.body_digest(unprofiled),
                  'operations':{name:accesses.count(name) for name in set(accesses)}}
        with self.assertRaisesRegex(RuntimeError,'unexpected guest scope'):
            generation.transform(text,[selected])
        with self.assertRaisesRegex(RuntimeError,'unsupported access'):
            generation.transform(text[:start]+unprofiled+text[end:],[selected])
        entries={row['entry'] for row in json.loads((ROOT/'config/inline_fast_memory.json').read_text())}
        self.assertNotIn('828389F0',entries)
        self.assertNotIn('PPC_LOAD_U64',generation.OPERATIONS)
        self.assertNotIn('PPC_STORE_U64',generation.OPERATIONS)

    def test_only_verified_byte_pair_and_vm_scopes_can_be_removed(self):
        self.assertEqual(set(generation.VERIFIED_GUEST_SCOPES),{generation.BYTE_ENTRY,generation.PAIR_ENTRY,generation.VM_ENTRY})
        for entry,(prologue,scope) in generation.VERIFIED_GUEST_SCOPES.items():
            body=prologue+scope+'\treturn;\n}'
            self.assertEqual(generation.without_verified_guest_scope(body,entry),(prologue+'\treturn;\n}',scope))
            for other in ('823F2D98','8270A590','82B2ABD8','828389F0'):
                with self.assertRaisesRegex(RuntimeError,"unexpected guest scope"):
                    generation.without_verified_guest_scope(body,other)

    def test_byte_pair_bitset_and_vm_references_are_original_external_fast_without_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'reference.h'
            command=[sys.executable,'-B',str(ROOT/'tools/generate_inline_memory_reference.py')]
            for argument,chunk in (('source',70),('matrix-source',71),('runtime-source',239),
                                   ('vector-source',271),('blockcopy-source',140),('bytecopy-source',176),('pair-source',265),
                                   ('vm-source',182)):
                command += ['--'+argument,str(ROOT/f'build/generated/ppc_recomp.{chunk}.cpp')]
            subprocess.run(command+['--output',str(output)],check=True,capture_output=True,text=True)
            previous=output.read_bytes()
            emitted=output.read_text()
            guard='#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY\n'
            binding=(guard+'#pragma push_macro("PPCGuestPointer")\n#undef PPCGuestPointer\n'
                     '#define PPCGuestPointer PPCGuestPointerFast\n#endif\n')
            marker='PPC_FUNC(referenceFastPairBitset) {'
            begin=emitted.index(marker)
            finish=emitted.index('\n}',begin)+2
            self.assertTrue(emitted[:begin].endswith(binding))
            self.assertTrue(emitted[finish:].startswith(
                '\n\n'+guard+'#pragma pop_macro("PPCGuestPointer")\n#endif\n'))
            self.assertEqual(emitted.count('#pragma push_macro("PPCGuestPointer")'),1)
            self.assertEqual(emitted.count('#pragma pop_macro("PPCGuestPointer")'),1)
            self.assertEqual(emitted.count('#define PPCGuestPointer PPCGuestPointerFast'),1)
            for entry,symbol,chunk,argument,before,after in (
                    (generation.BYTE_ENTRY,'referenceFastByteDecode',176,'bytecopy-source',
                     'ctx.r12.u64 = ctx.lr;','ctx.r12.u64 = ctx.ctr.u64;'),
                    (generation.PAIR_ENTRY,'referenceFastPairShift',265,'pair-source',
                     'sub_82B64600(ctx, base);','sub_82B64604(ctx, base);'),
                    (generation.VM_ENTRY,'referenceFastVMSetup',182,'vm-source',
                     'sub_828444D8(ctx, base);','sub_828444DC(ctx, base);'),
                    ('82B2A1C0','referenceFastPairBitset',265,'pair-source',
                     '__savegprlr_29(ctx, base);','__savegprlr_28(ctx, base);'),
                    ('82A3D1D8','referenceFastScalarCopy',239,'runtime-source',
                     'ctx.r0.s64 = ctx.r5.s64 + 1;','ctx.r0.s64 = ctx.r5.s64 + 2;')):
                reference=output.read_text().split('PPC_FUNC('+symbol+') {',1)[1]
                actual='PPC_FUNC_IMPL(__imp__sub_'+entry+') {'+reference
                begin,finish=generation.function_span(actual,entry)
                actual=actual[begin:finish]
                self.assertNotIn('SIMPSONS_PROFILE_GUEST_SCOPE',actual)
                text=(ROOT/f'build/generated/ppc_recomp.{chunk}.cpp').read_text()
                start,end=generation.function_span(text,entry)
                original=generation.original_body(text[start:end],entry)
                for name in generation.OPERATIONS:
                    actual=actual.replace(name.replace('PPC_','PPC_EXTERNAL_FAST_',1),name)
                self.assertEqual(actual,original)
                changed=Path(directory)/('changed-'+entry+'-source.cpp')
                changed.write_text(text[:start]+text[start:end].replace(before,after,1)+text[end:])
                rejected=command.copy();rejected[rejected.index('--'+argument)+1]=str(changed)
                result=subprocess.run(rejected+['--output',str(output)],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('reference original body changed',result.stderr)
                self.assertEqual(output.read_bytes(),previous)


if __name__=="__main__":
    unittest.main()
