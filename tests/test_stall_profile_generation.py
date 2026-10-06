"""Guest/native forwarding retains call semantics and complete profiler coverage."""
from __future__ import annotations

import importlib.util
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
                                  '#define SIMPSONS_PROFILE_HOOK(name) name\n#endif\n')
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
            self.assertEqual(coverage, {"imports": 2, "native_hooks": 2})
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


if __name__ == "__main__":
    unittest.main()
