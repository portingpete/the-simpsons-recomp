"""Guest/native forwarding retains call semantics and complete profiler coverage."""
from __future__ import annotations

import importlib.util
import hashlib
import json
from pathlib import Path
import re
import tempfile
import tomllib
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "stall_profile_generation", ROOT / "tools/stall_profile_generation.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)

SHARED = ('#pragma once\n#include "ppc_context.h"\n'
          'PPC_EXTERN_FUNC(sub_82230000);\n'
          'PPC_EXTERN_FUNC(__imp__NtReadFile);\n'
          'PPC_EXTERN_FUNC(__imp__MissingImport);\n')
MAPPING = ('#include "ppc_recomp_shared.h"\n'
           'PPCFuncMapping PPCFuncMappings[] = {\n'
           '  { 0x82230000, sub_82230000 },\n'
           '  { 0x82CC24D4, __imp__NtReadFile },\n'
           '  { 0x82CC24E4, __imp__MissingImport },\n};\n')
CHUNK = ('#include "ppc_recomp_shared.h"\n'
         'extern bool SimpsonsNativeDraw(PPCContext& ctx, uint8_t* base);\n'
         'extern void SimpsonsNativePresent(PPCContext& ctx, uint8_t* base);\n'
         'extern void SimpsonsNativeFrameWaitBegin(PPCContext& ctx, uint8_t* base);\n'
         'extern void SimpsonsNativeFrameWaitEnd(PPCContext& ctx, uint8_t* base);\n'
         'PPC_FUNC_IMPL(__imp__sub_82230000) {\n'
         '  SimpsonsNativeFrameWaitBegin(ctx, base);\n'
         '  if (SimpsonsNativeDraw(ctx, base)) return;\n'
         '  SimpsonsNativeFrameWaitEnd(ctx, base);\n'
         '  SimpsonsNativePresent(ctx, base);\n}\n')
GUEST_SELECTION = {"chunk": 0, "entry": "82230000", "label": "Guest.fixture82230000"}
GUEST_CHUNK = ('#include "ppc_recomp_shared.h"\n'
    '__attribute__((alias("__imp__sub_82230000"))) PPC_WEAK_FUNC(sub_82230000);\n'
    'PPC_FUNC_IMPL(__imp__sub_82230000) {\n'
    '  PPC_FUNC_PROLOGUE();\n'
    '  PPC_TRACE_ENTRY(ctx, 0x82230000);\n'
    '  goto loc_82230000;\n'
    'loc_82230000:\n'
    '  ctx.lr = 0x82230004;\n'
    '  PPC_STORE_U32(ctx.r1.u32 - 8, ctx.r12.u32);\n'
    '  if (ctx.r3.u32) return;\n'
    '  __imp__NtReadFile(ctx, base);\n'
    '  ctx.r3.u64 = PPC_LOAD_U8(ctx.r4.u32);\n'
    '  return;\n}\n'
    'PPC_FUNC_IMPL(__imp__sub_82230008) {\n'
    '  PPC_FUNC_PROLOGUE();\n'
    '  PPC_TRACE_ENTRY(ctx, 0x82230008);\n'
    '  ctx.r3.u32 = 7; return;\n}\n')


class StallProfileGenerationTests(unittest.TestCase):
    def test_all_import_pointers_include_unimplemented_imports(self):
        names = profile.import_names(SHARED)
        self.assertEqual(names, ["__imp__MissingImport", "__imp__NtReadFile"])
        result = profile.instrument_mapping(MAPPING, names)
        self.assertIn('{ 0x82230000, sub_82230000 }', result)
        for name in names:
            self.assertIn(f'SIMPSONS_PROFILE_IMPORT({name})', result)
        source = profile.forwarding_source(names, {"__imp__NtReadFile": "FileIO"})
        self.assertIn('Section::FileIO, "NtReadFile", &ctx', source)
        self.assertIn('Section::Runtime, "MissingImport", &ctx', source)
        for name in names:
            self.assertIn(f'PPC_FUNC(SimpsonsProfile{name})', source)
            self.assertIn(f'{name}(ctx, base);', source)

    def test_missing_and_undeclared_mapping_fail_closed(self):
        with self.assertRaisesRegex(RuntimeError, "missing mapping coverage"):
            profile.instrument_mapping(MAPPING, profile.import_names(SHARED) + ["__imp__Absent"])
        with self.assertRaisesRegex(RuntimeError, "undeclared import"):
            profile.instrument_mapping(MAPPING, ["__imp__NtReadFile"])

    def test_direct_import_calls_use_forwarders_and_preserve_guest_aliases(self):
        chunk = ('#include "ppc_recomp_shared.h"\n'
                 '__attribute__((alias("__imp__sub_82230000"))) PPC_WEAK_FUNC(sub_82230000);\n'
                 'PPC_FUNC_IMPL(__imp__sub_82230000) {\n'
                 '  __imp__NtReadFile(ctx, base);\n'
                 '  __imp__MissingImport(ctx,base);\n'
                 '  __imp__sub_82230008(ctx, base);\n'
                 '  // __imp__NtReadFile(ctx, base)\n'
                 '  const char* name = "__imp__NtReadFile(ctx, base)";\n}\n')
        result = profile.instrument_direct_imports(chunk, profile.import_names(SHARED))
        self.assertIn('SIMPSONS_PROFILE_IMPORT(__imp__NtReadFile)(ctx, base);', result)
        self.assertIn('SIMPSONS_PROFILE_IMPORT(__imp__MissingImport)(ctx,base);', result)
        for line in chunk.splitlines():
            if ('alias(' in line or 'PPC_FUNC_IMPL(' in line or
                    '__imp__sub_82230008(' in line or '// ' in line or 'const char*' in line):
                self.assertIn(line, result)
        self.assertEqual(profile.instrument_direct_imports(chunk, [], enabled=False), chunk)
        with self.assertRaisesRegex(RuntimeError, "undeclared import"):
            profile.instrument_direct_imports(chunk, ["__imp__NtReadFile"])

    def test_hooks_preserve_declarations_boolean_control_flow_and_guest_names(self):
        hooks = profile.hook_declarations(CHUNK)
        result = profile.instrument_hooks(CHUNK, hooks)
        for line in CHUNK.splitlines():
            if line.startswith("extern ") or line.startswith("PPC_FUNC_IMPL"):
                self.assertIn(line, result)
        self.assertIn('if (SIMPSONS_PROFILE_HOOK(SimpsonsNativeDraw)(ctx, base)) return;', result)
        self.assertIn('SIMPSONS_PROFILE_HOOK(SimpsonsNativePresent)(ctx, base);', result)
        for name in profile.EXCLUDED_HOOKS:
            self.assertIn(f'  {name}(ctx, base);', result)
            self.assertNotIn(f'SIMPSONS_PROFILE_HOOK({name})', result)

    def test_comments_strings_and_function_definitions_retain_original_names(self):
        suffix = ('// SimpsonsNativeDraw(ctx, base)\n'
                  'const char* description = "SimpsonsNativeDraw(ctx, base)";\n'
                  '/* { 0x82CC24D4, __imp__MissingImport } */\n'
                  'bool SimpsonsNativeDraw(PPCContext& ctx, uint8_t* base) { return false; }\n')
        result = profile.instrument_hooks(CHUNK + suffix, profile.hook_declarations(CHUNK))
        self.assertTrue(result.endswith(suffix))
        mapping = profile.instrument_mapping(MAPPING + suffix, profile.import_names(SHARED))
        self.assertTrue(mapping.endswith(suffix))

    def test_unsupported_native_call_and_conflicting_signature_fail_closed(self):
        with self.assertRaisesRegex(RuntimeError, "no supported declaration"):
            profile.instrument_hooks(CHUNK + 'SimpsonsNativeUnknown(ctx, base);', profile.hook_declarations(CHUNK))
        with self.assertRaisesRegex(RuntimeError, "Conflicting"):
            profile.hook_declarations(CHUNK + 'extern void SimpsonsNativeDraw(PPCContext& ctx, uint8_t* base);')

    def test_compiled_out_transform_is_original_identity(self):
        self.assertEqual(profile.instrument_mapping(MAPPING, [], enabled=False), MAPPING)
        self.assertEqual(profile.instrument_hooks(CHUNK, {}, enabled=False), CHUNK)
        header = profile.forwarding_header(profile.import_names(SHARED), profile.hook_declarations(CHUNK), {})
        disabled = header.split('#else\n', 1)[1]
        self.assertEqual(disabled, '#define SIMPSONS_PROFILE_IMPORT(name) name\n'
                                  '#define SIMPSONS_PROFILE_HOOK(name) name\n'
                                  '#define SIMPSONS_PROFILE_GUEST_SCOPE(label)\n#endif\n')
        self.assertNotIn('StallProfiler', disabled)
        self.assertIn('#if SIMPSONS_STALL_PROFILER\n#include "runtime/stall_profiler.h"', header)

    def test_runtime_disabled_branches_bypass_scope_initialization(self):
        names = profile.import_names(SHARED)
        header = profile.forwarding_header(names, profile.hook_declarations(CHUNK), {})
        source = profile.forwarding_source(names, {})
        for text, name, prefix in (
                (header, "SimpsonsNativeDraw", "SimpsonsProfile_"),
                (header, "SimpsonsNativePresent", "SimpsonsProfile_"),
                (source, "__imp__NtReadFile", "SimpsonsProfile"),
                (source, "__imp__MissingImport", "SimpsonsProfile")):
            body = text.split(prefix + name, 1)[1].split('}\n', 1)[0]
            self.assertLess(body.index(f'if (!Simpsons::StallProfiler::enabled) return {name}(ctx, base);'),
                            body.index('StallProfiler::Scope scope'))

    def test_native_forwarders_keep_profiler_control_flow_outside_guest_functions(self):
        hooks = profile.hook_declarations(CHUNK)
        header = profile.forwarding_header([], hooks, {})
        for name, result in hooks.items():
            if name not in profile.EXCLUDED_HOOKS:
                self.assertIn(f'__declspec(noinline) inline {result} SimpsonsProfile_{name}(', header)

    def test_all_native_hook_families_preserve_return_and_exception_semantics(self):
        chunk = ('#include "ppc_recomp_shared.h"\n'
                 'extern void SimpsonsAudioReader8233D644(PPCContext& ctx,uint8_t* base);\n'
                 'extern bool SimpsonsOriginalMeshDeclarationDestroy(PPCContext& ctx,uint8_t* base);\n'
                 'extern void SimpsonsRejectAudioReset(PPCContext& ctx,uint8_t* base);\n'
                 'extern void SimpsonsNonlocalJumpTransfer(PPCContext& ctx,uint8_t* base);\n'
                 'PPC_FUNC_IMPL(__imp__sub_82230000) {\n'
                 '  SimpsonsAudioReader8233D644(ctx,base);\n'
                 '  if (SimpsonsOriginalMeshDeclarationDestroy(ctx,base)) return;\n'
                 '  SimpsonsRejectAudioReset(ctx,base);\n'
                 '  SimpsonsNonlocalJumpTransfer(ctx,base);\n}\n')
        hooks = profile.hook_declarations(chunk)
        result = profile.instrument_hooks(chunk, hooks)
        header = profile.forwarding_header([], hooks, {})
        for name in hooks:
            self.assertIn(f'SIMPSONS_PROFILE_HOOK({name})(ctx,base)', result)
            self.assertIn(f'return {name}(ctx, base);', header)
        self.assertNotIn('catch', header)

    def test_present_ends_rendering_before_frame_report_and_wait_partitions_are_excluded(self):
        header = profile.forwarding_header([], profile.hook_declarations(CHUNK),
                                           {"SimpsonsNativePresent": "Rendering"})
        present = header.split('inline void SimpsonsProfile_SimpsonsNativePresent', 1)[1]
        present = present.split('}\n', 1)[0]
        self.assertIn('Section::Rendering, "SimpsonsNativePresent", &ctx', present)
        self.assertLess(present.index('Scope scope'), present.index('  SimpsonsNativePresent(ctx, base);'))
        self.assertLess(present.index('  SimpsonsNativePresent(ctx, base);'), present.index('scope.finish();'))
        self.assertLess(present.index('scope.finish();'), present.index('frameBoundary(&ctx);'))
        self.assertNotIn('Section::Present', present)
        for name in profile.EXCLUDED_HOOKS:
            self.assertNotIn(name, header)

    def test_classification_follows_definitions_and_macro_implementation_units(self):
        sources = {
            "runtime/filesystem.cpp": 'PPC_FUNC(__imp__NtReadFile) { }',
            "runtime/native_saves.cpp": 'void SimpsonsNativeSave(PPCContext& ctx,uint8_t* base) { }',
            "runtime/content.cpp": 'PPC_FUNC(__imp__XamContentCreateEx) { }',
            "runtime/engine_driver.cpp": 'bool SimpsonsNativeDraw(PPCContext& ctx,uint8_t* base) { return true; }',
            "runtime/engine_audio_output.cpp": 'void SimpsonsNativeAudio(PPCContext& ctx,uint8_t* base) { }',
            "runtime/engine_reflection_textures.cpp": 'NATIVE_REFLECTION(SimpsonsNativeReflectionLock, lock);',
            "runtime/engine_audio.cpp": '#define AUDIO_HEAP(pc) void SimpsonsAudioHeap##pc(PPCContext& ctx,uint8_t* base) { }\n'
                                        'AUDIO_HEAP(8268DDA0)\n',
            "runtime/engine_audio_reader.cpp": '#define READER_BOUNDARY(address) \\\n'
                                               'void SimpsonsAudioReader##address(PPCContext& ctx,uint8_t* base) { }\n'
                                               'READER_BOUNDARY(8233D644)\n',
            "runtime/kernel.cpp": 'PPC_FUNC(__imp__KeWaitForSingleObject) { } '
                                  'void test() { SimpsonsNativeDraw(ctx,base); }',
        }
        sections = profile.implementation_sections(sources)
        self.assertEqual(sections["__imp__NtReadFile"], "FileIO")
        self.assertEqual(sections["SimpsonsNativeSave"], "FileIO")
        self.assertEqual(sections["__imp__XamContentCreateEx"], "FileIO")
        self.assertEqual(sections["SimpsonsNativeDraw"], "Rendering")
        self.assertEqual(sections["SimpsonsNativeAudio"], "Audio")
        self.assertEqual(sections["SimpsonsNativeReflectionLock"], "Rendering")
        self.assertEqual(sections["SimpsonsAudioHeap8268DDA0"], "Audio")
        self.assertEqual(sections["SimpsonsAudioReader8233D644"], "Audio")
        self.assertEqual(sections["__imp__KeWaitForSingleObject"], "Runtime")

    def test_ambiguous_implementation_classification_fail_closed(self):
        with self.assertRaisesRegex(RuntimeError, "Ambiguous"):
            profile.implementation_sections({
                "runtime/filesystem.cpp": 'PPC_FUNC(__imp__Duplicate) { }',
                "runtime/kernel.cpp": 'PPC_FUNC(__imp__Duplicate) { }'})

    def test_pipeline_emits_forwarders_and_reproduces_the_same_outputs(self):
        sources = {"runtime/engine_driver.cpp":
                   'bool SimpsonsNativeDraw(PPCContext& ctx,uint8_t* base) { return true; } '
                   'void SimpsonsNativePresent(PPCContext& ctx,uint8_t* base) { }',
                   "runtime/filesystem.cpp": 'PPC_FUNC(__imp__NtReadFile) { }'}
        with tempfile.TemporaryDirectory(prefix="stall-profile-generation-") as directory:
            out = Path(directory)
            def original_outputs():
                (out / "ppc_recomp_shared.h").write_text(SHARED, encoding="utf-8")
                (out / "ppc_func_mapping.cpp").write_text(MAPPING, encoding="utf-8")
                (out / "ppc_recomp.0.cpp").write_text(CHUNK, encoding="utf-8")
            original_outputs()
            coverage = profile.instrument_generated(out, ["ppc_recomp.0.cpp"], sources)
            self.assertEqual(coverage, {"imports": 2, "native_hooks": 2, "guest_scopes": []})
            self.assertTrue((out / profile.HEADER_NAME).is_file())
            self.assertTrue((out / profile.SOURCE_NAME).is_file())
            first = {path.name: path.read_bytes() for path in out.iterdir()}
            original_outputs()
            profile.instrument_generated(out, ["ppc_recomp.0.cpp"], sources)
            self.assertEqual(first, {path.name: path.read_bytes() for path in out.iterdir()})

    def test_repository_configured_hooks_all_have_unambiguous_source_sections(self):
        sources = {name: (ROOT / name).read_text(encoding="utf-8")
                   for name in json.loads((ROOT / "config/native_sources.json").read_text(encoding="utf-8"))}
        config = tomllib.loads((ROOT / "config/simpsons.toml").read_text(encoding="utf-8"))
        names = {hook["name"] for hook in config.get("midasm_hook", [])}
        self.assertGreater(len(names), 700)
        sections = profile.implementation_sections(sources)
        self.assertFalse(names - set(sections), f"Unclassified configured hooks: {sorted(names - set(sections))}")
        self.assertEqual(sections["SimpsonsNativePresent"], "Rendering")
        self.assertEqual(sections["__imp__NtReadFile"], "FileIO")
        self.assertEqual(sections["SimpsonsNativeAudioProvider"], "Audio")
        self.assertEqual(sections["SimpsonsAudioReader8233D644"], "Audio")
        self.assertEqual(sections["SimpsonsNonlocalJumpTransfer"], "Runtime")

    def test_guest_scope_preserves_entire_body_alias_labels_accesses_and_returns(self):
        line = '  SIMPSONS_PROFILE_GUEST_SCOPE("Guest.fixture82230000");\n'
        result, coverage = profile.instrument_guest_scopes(GUEST_CHUNK, [GUEST_SELECTION])
        self.assertIn('  PPC_TRACE_ENTRY(ctx, 0x82230000);\n'+line+'  goto loc_82230000;', result)
        self.assertEqual(result.replace(line, '').replace('#include "ppc_stall_profile.h"\n', ''), GUEST_CHUNK)
        original_body = GUEST_CHUNK.split('PPC_FUNC_IMPL(', 1)[1].split('\n}\n', 1)[0]
        original_body = 'PPC_FUNC_IMPL('+original_body+'\n}'
        self.assertEqual(coverage, [{**GUEST_SELECTION, "body_sha256":hashlib.sha256(original_body.encode()).hexdigest()}])
        self.assertEqual(profile.instrument_guest_scopes(result, [GUEST_SELECTION]), (result, coverage))
        self.assertEqual(result.count('SIMPSONS_PROFILE_GUEST_SCOPE('), 1)

    def test_guest_scope_ignores_comment_and_string_decoys(self):
        decoys = ('/* PPC_FUNC_IMPL(__imp__sub_82230000) {\n'
                  '  PPC_TRACE_ENTRY(ctx, 0x82230000);\n}\n*/\n'
                  'const char* text = "PPC_FUNC_IMPL(__imp__sub_82230000) {";\n'
                  '// PPC_TRACE_ENTRY(ctx, 0x82230000);\n')
        source = GUEST_CHUNK+decoys
        result, _ = profile.instrument_guest_scopes(source, [GUEST_SELECTION])
        self.assertTrue(result.endswith(decoys))
        nested_decoy = GUEST_CHUNK.replace('  goto loc_82230000;',
            '  /* PPC_TRACE_ENTRY(ctx, 0x82230000); */\n  goto loc_82230000;')
        self.assertEqual(profile.instrument_guest_scopes(nested_decoy, [GUEST_SELECTION])[0].count('SIMPSONS_PROFILE_GUEST_SCOPE('), 1)

    def test_guest_scope_rejects_unknown_duplicate_late_and_changed_anchors(self):
        selection = [GUEST_SELECTION]
        bad = [GUEST_CHUNK.replace('0x82230000);', '0x82230004);'),
               GUEST_CHUNK.replace('  PPC_TRACE_ENTRY(ctx, 0x82230000);', '  if (ctx.r3.u32) { PPC_TRACE_ENTRY(ctx, 0x82230000); }'),
               GUEST_CHUNK.replace('  PPC_TRACE_ENTRY(ctx, 0x82230000);', '  ctx.r3.u32 = 1;\n  PPC_TRACE_ENTRY(ctx, 0x82230000);'),
               GUEST_CHUNK.replace('  PPC_TRACE_ENTRY(ctx, 0x82230000);', '  PPC_TRACE_ENTRY(ctx, 0x82230000);\n  PPC_TRACE_ENTRY(ctx, 0x82230000);'),
               GUEST_CHUNK+GUEST_CHUNK,
               GUEST_CHUNK.replace('  goto loc_82230000;', '  SIMPSONS_PROFILE_GUEST_SCOPE("Guest.changed");\n  goto loc_82230000;')]
        for source in bad:
            with self.subTest(source=source), self.assertRaises(RuntimeError):
                profile.instrument_guest_scopes(source, selection)
        with self.assertRaisesRegex(RuntimeError, 'exactly once'):
            profile.instrument_guest_scopes(GUEST_CHUNK, [{**GUEST_SELECTION, "entry":"82230010"}])
        with self.assertRaisesRegex(RuntimeError, 'Unclosed'):
            profile.instrument_guest_scopes(GUEST_CHUNK.replace('\n}', '\n'), selection)

    def test_guest_scope_inventory_rejects_duplicates_unsafe_names_and_unbounded_counts(self):
        for selections in ([GUEST_SELECTION, GUEST_SELECTION], [{**GUEST_SELECTION, "chunk":True}],
                [{**GUEST_SELECTION, "chunk":-1}], [{**GUEST_SELECTION, "entry":"not-an-entry"}],
                [{**GUEST_SELECTION, "label":'bad"); return; //'}]):
            with self.subTest(selections=selections), self.assertRaises(RuntimeError):
                profile.guest_selections(selections)
        bounded = [{"chunk":265, "entry":f"{0x82230000 + index * 16:08X}",
                    "label":f"Guest.bounded{index}"} for index in range(7)]
        self.assertEqual(len(profile.guest_selections(bounded[:6])['ppc_recomp.265.cpp']), 6)
        with self.assertRaisesRegex(RuntimeError, 'exceeds six'):
            profile.guest_selections(bounded)

    def test_guest_scope_compiled_out_expansion_has_no_state_or_context_evaluation(self):
        self.assertEqual(profile.instrument_guest_scopes(GUEST_CHUNK, [GUEST_SELECTION], enabled=False), (GUEST_CHUNK, []))
        header = profile.forwarding_header([], {}, {})
        enabled, disabled = header.split('#else\n', 1)
        self.assertIn('Section::Guest, label, &ctx', enabled)
        self.assertIn('#define SIMPSONS_PROFILE_GUEST_SCOPE(label)\n', disabled)
        self.assertNotIn('ctx', disabled)
        self.assertNotIn('StallProfiler', disabled)

    def test_guest_scope_pipeline_preserves_native_children_and_checks_global_chunk_identity(self):
        sources = {"runtime/filesystem.cpp":'PPC_FUNC(__imp__NtReadFile) { }'}
        with tempfile.TemporaryDirectory(prefix='selected-guest-profile-') as directory:
            out = Path(directory)
            (out/'ppc_recomp_shared.h').write_text(SHARED)
            (out/'ppc_func_mapping.cpp').write_text(MAPPING)
            (out/'ppc_recomp.0.cpp').write_text(GUEST_CHUNK)
            coverage = profile.instrument_generated(out, ['ppc_recomp.0.cpp'], sources, [GUEST_SELECTION])
            result = (out/'ppc_recomp.0.cpp').read_text()
            self.assertIn('SIMPSONS_PROFILE_IMPORT(__imp__NtReadFile)(ctx, base);', result)
            self.assertIn('Section::FileIO, "NtReadFile", &ctx', (out/profile.SOURCE_NAME).read_text())
            self.assertEqual(coverage['guest_scopes'][0]['entry'], '82230000')
            self.assertIn('{ 0x82230000, sub_82230000 }', (out/'ppc_func_mapping.cpp').read_text())
            first = {path.name:path.read_bytes() for path in out.iterdir()}
            # Existing mapping generation starts from raw generator output;
            # the selected guest body itself is independently idempotent.
            (out/'ppc_func_mapping.cpp').write_text(MAPPING)
            self.assertEqual(profile.instrument_generated(out, ['ppc_recomp.0.cpp'], sources, [GUEST_SELECTION]), coverage)
            self.assertEqual(first, {path.name:path.read_bytes() for path in out.iterdir()})
            (out/'ppc_recomp.1.cpp').write_text(GUEST_CHUNK)
            with self.assertRaisesRegex(RuntimeError, 'entry/chunk identity'):
                profile.instrument_generated(out, ['ppc_recomp.0.cpp','ppc_recomp.1.cpp'], sources, [GUEST_SELECTION])
            with self.assertRaisesRegex(RuntimeError, 'chunk absent'):
                profile.instrument_generated(out, ['ppc_recomp.0.cpp'], sources, [{**GUEST_SELECTION,"chunk":1}])

    def test_pure_selected_guest_chunk_receives_its_macro_header(self):
        source = GUEST_CHUNK.replace('  __imp__NtReadFile(ctx, base);\n', '')
        with tempfile.TemporaryDirectory(prefix='pure-selected-guest-') as directory:
            out = Path(directory)
            (out/'ppc_recomp_shared.h').write_text('#include "ppc_context.h"\n')
            (out/'ppc_func_mapping.cpp').write_text('#include "ppc_recomp_shared.h"\nPPCFuncMapping PPCFuncMappings[] = {};\n')
            (out/'ppc_recomp.0.cpp').write_text(source)
            profile.instrument_generated(out, ['ppc_recomp.0.cpp'], {}, [GUEST_SELECTION])
            result = (out/'ppc_recomp.0.cpp').read_text()
            self.assertEqual(result.count('#include "ppc_stall_profile.h"'), 1)
            self.assertIn('SIMPSONS_PROFILE_GUEST_SCOPE("Guest.fixture82230000");', result)

    def test_repository_six_guest_scopes_are_hash_gated_inputs_and_reversible(self):
        selections = json.loads((ROOT/'config/guest_profile_scopes.json').read_text())
        self.assertEqual([(row['chunk'], row['entry'], row['label']) for row in selections],
                         [(142, '82718B70', 'Guest.loader82718B70'),
                          (176, '8281E9B0', 'Guest.byteDecode8281E9B0'),
                          (181, '828389F0', 'Guest.vmOuter828389F0'),
                          (182, '82844AB0', 'Guest.vmSetup82844AB0'),
                          (265, '82B2ABD8', 'Guest.pairUpdate82B2ABD8'),
                          (265, '82B2A8D0', 'Guest.pairShift82B2A8D0')])
        recompile = (ROOT/'tools/recompile.py').read_text()
        self.assertIn("ROOT/'config/guest_profile_scopes.json'", recompile.split('def inputs():',1)[1].split('def verify(',1)[0])
        for selection in selections:
            source = (ROOT/f"build/generated/ppc_recomp.{selection['chunk']}.cpp").read_text()
            result, coverage = profile.instrument_guest_scopes(source, [selection])
            self.assertEqual(profile.instrument_guest_scopes(result, [selection]), (result, coverage))
            # A previously scoped generated file is still a valid idempotence
            # reference after coordinated regeneration; compare stripped bodies.
            line = '\tSIMPSONS_PROFILE_GUEST_SCOPE("'+selection['label']+'");\n'
            restored = result.replace(line, '')
            reference = source.replace(line, '')
            if '#include "ppc_stall_profile.h"' not in reference:
                restored = restored.replace('#include "ppc_stall_profile.h"\n', '')
            self.assertEqual(restored, reference)

    def assert_real_vm_scope(self, chunk, entry, label, body_sha256, calls, source=None):
        selection = {"chunk":chunk, "entry":entry, "label":label}
        if source is None:
            source = (ROOT/f"build/generated/ppc_recomp.{chunk}.cpp").read_text()
        trace = f'\tPPC_TRACE_ENTRY(ctx, 0x{entry});\n'
        line = f'\tSIMPSONS_PROFILE_GUEST_SCOPE("{label}");\n'
        prologue = f'PPC_FUNC_IMPL(__imp__sub_{entry}) {{\n\tPPC_FUNC_PROLOGUE();\n'
        result, coverage = profile.instrument_guest_scopes(source, [selection])
        self.assertEqual(result.count(line), 1)
        self.assertIn(prologue+trace+line+'\tuint32_t ea{};\n\tgoto loc_'+entry+';', result)
        self.assertEqual(profile.instrument_guest_scopes(result, [selection]), (result, coverage))
        # These whole-chunk equalities include every label, register assignment,
        # error/growth/indirect call and return, including unselected functions.
        reference, restored = source.replace(line, ''), result.replace(line, '')
        if '#include "ppc_stall_profile.h"' not in reference:
            restored = restored.replace('#include "ppc_stall_profile.h"\n', '')
        self.assertEqual(restored, reference)
        self.assertEqual(profile.instrument_guest_scopes(source, [selection], enabled=False), (source, []))
        start = reference.index(prologue)
        code = profile.code_only(reference)
        depth, end = 1, None
        for at in range(start+len(f'PPC_FUNC_IMPL(__imp__sub_{entry}) {{'), len(code)):
            depth += (code[at] == '{') - (code[at] == '}')
            if not depth:
                end = at+1; break
        self.assertIsNotNone(end)
        body = reference[start:end]
        # Scope coverage pins the actual generated body with only its canonical
        # scope removed, including any already qualified inline leaf names.
        self.assertEqual(coverage, [{**selection,
            "body_sha256":hashlib.sha256(body.encode()).hexdigest()}])
        normalized = body
        if entry == '82844AB0':
            operations = {'PPC_LOAD_U32':36, 'PPC_STORE_U32':17,
                          'PPC_LOAD_U16':2, 'PPC_LOAD_U8':6}
            reverse = {name.replace('PPC_', 'PPC_FAST_INLINE_', 1):name for name in operations}
            memory = list(re.finditer(r'\bPPC_(?:FAST_INLINE_)?(?:LOAD|STORE)_U\d+\b', profile.code_only(body)))
            observed = {name:sum(match.group() == name for match in memory)
                        for name in {match.group() for match in memory}}
            inlined = any(match.group() in reverse for match in memory)
            self.assertEqual(observed, {name.replace('PPC_', 'PPC_FAST_INLINE_', 1):count
                                       for name,count in operations.items()} if inlined else operations)
            self.assertEqual(len(memory), 61)
            # Reverse only these four proved VM identifier kinds, never other
            # scopes, callees, widths, effective addresses or instructions.
            for match in reversed(memory):
                if match.group() in reverse:
                    normalized = normalized[:match.start()]+reverse[match.group()]+normalized[match.end():]
        self.assertEqual(hashlib.sha256(normalized.encode()).hexdigest(), body_sha256)
        self.assertIn(f'__attribute__((alias("__imp__sub_{entry}"))) PPC_WEAK_FUNC(sub_{entry});', reference)
        for call in calls:
            self.assertIn(call, body)
        self.assertIn(f'PPC_RECOMP_FAILURE(ctx, 0x{entry}, "function fell through recovered control flow");', body)
        bad = {
            'wrong trace':reference.replace(trace, trace.replace(entry, f'{int(entry,16)+4:08X}'), 1),
            'duplicate trace':reference.replace(trace, trace+trace, 1),
            'late trace':reference.replace(trace, '\tctx.r3.u32 = 0;\n'+trace, 1),
            'changed scope':result.replace(line, '\tSIMPSONS_PROFILE_GUEST_SCOPE("Guest.changed");\n', 1),
        }
        for reason, text in bad.items():
            with self.subTest(entry=entry, reason=reason), self.assertRaises(RuntimeError):
                profile.instrument_guest_scopes(text, [selection])

    def test_vm_outer_scope_preserves_exact_original_body_and_rejects_changed_anchors(self):
        self.assert_real_vm_scope(181, '828389F0', 'Guest.vmOuter828389F0',
            '7143aea8a879c05474e4e72ebf9743be368e8ac5f560471a98cdd88dd764f6e7',
            ['sub_828371C8(ctx, base);', 'sub_82845530(ctx, base);'])

    def test_vm_setup_scope_preserves_exact_original_body_growth_and_indirect_calls(self):
        self.assert_real_vm_scope(182, '82844AB0', 'Guest.vmSetup82844AB0',
            '7cf373f8be651007627f714455e8c9a7479aa03312674d8ee2ee57a2360aebfb',
            ['__savegprlr_28(ctx, base);', 'sub_82A43D30(ctx, base);',
             'sub_82A41FA0(ctx, base);', 'sub_828449E0(ctx, base);',
             'PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);', '__restgprlr_28(ctx, base);'])

    def test_vm_setup_scope_hash_and_full_chunk_restoration_after_exact_inline_identifiers(self):
        source = (ROOT/'build/generated/ppc_recomp.182.cpp').read_text()
        start = source.index('PPC_FUNC_IMPL(__imp__sub_82844AB0) {')
        end = source.index('\n}', start)+2
        body = source[start:end]
        names = {'PPC_LOAD_U32', 'PPC_STORE_U32', 'PPC_LOAD_U16', 'PPC_LOAD_U8'}
        for match in reversed(list(re.finditer(r'\bPPC_(?:LOAD|STORE)_U\d+\b', profile.code_only(body)))):
            self.assertIn(match.group(), names)
            body = body[:match.start()]+match.group().replace('PPC_', 'PPC_FAST_INLINE_', 1)+body[match.end():]
        rewritten = source[:start]+body+source[end:]
        self.assert_real_vm_scope(182, '82844AB0', 'Guest.vmSetup82844AB0',
            '7cf373f8be651007627f714455e8c9a7479aa03312674d8ee2ee57a2360aebfb',
            ['__savegprlr_28(ctx, base);', 'sub_82A43D30(ctx, base);',
             'sub_82A41FA0(ctx, base);', 'sub_828449E0(ctx, base);',
             'PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);', '__restgprlr_28(ctx, base);'], rewritten)

    def test_pair_owner_and_shift_scopes_preserve_same_chunk_growth_and_full_bodies(self):
        selections = [row for row in json.loads((ROOT/'config/guest_profile_scopes.json').read_text())
                      if row['chunk'] == 265]
        self.assertEqual([(row['entry'], row['label']) for row in selections],
                         [('82B2ABD8', 'Guest.pairUpdate82B2ABD8'),
                          ('82B2A8D0', 'Guest.pairShift82B2A8D0')])
        source = (ROOT/'build/generated/ppc_recomp.265.cpp').read_text()
        result, coverage = profile.instrument_guest_scopes(source, selections)
        self.assertEqual([row['entry'] for row in coverage], ['82B2ABD8', '82B2A8D0'])
        self.assertEqual(profile.instrument_guest_scopes(result, selections), (result, coverage))
        restored, reference = result, source
        for selection in selections:
            line = '\tSIMPSONS_PROFILE_GUEST_SCOPE("'+selection['label']+'");\n'
            self.assertEqual(result.count(line), 1)
            restored, reference = restored.replace(line, ''), reference.replace(line, '')
        if '#include "ppc_stall_profile.h"' not in reference:
            restored = restored.replace('#include "ppc_stall_profile.h"\n', '')
        self.assertEqual(restored, reference)
        # Reversibility covers stack saves/restores, conditional growth, both
        # shifting loops, all accesses/flags and indirect owner callbacks.
        self.assertIn('sub_82B64600(ctx, base);', restored)
        self.assertIn('loc_82B2A954:', restored)
        self.assertIn('loc_82B2A9B4:', restored)
        self.assertEqual(profile.instrument_guest_scopes(source, selections, enabled=False), (source, []))


if __name__ == "__main__":
    unittest.main()
