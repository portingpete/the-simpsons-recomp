"""Reproducible profiler forwarding at generated guest/native boundaries.

Only generated call expressions and import mapping entries are redirected. Native
declarations, hook identities, recovered guest code, and the real functions stay
unchanged. The generated macros expand to the original calls when compiled out.
"""
from __future__ import annotations

from pathlib import Path
import re


HEADER_NAME = "ppc_stall_profile.h"
SOURCE_NAME = "ppc_stall_profile.cpp"
EXCLUDED_HOOKS = frozenset(("SimpsonsNativeFrameWaitBegin", "SimpsonsNativeFrameWaitEnd"))
_IMPORT = re.compile(r"PPC_EXTERN_FUNC\s*\(\s*(__imp__\w+)\s*\)")
_HOOK_DECL = re.compile(
    r"\bextern\s+(void|bool)\s+(Simpsons\w+)\s*\(\s*"
    r"PPCContext\s*&\s*ctx\s*,\s*uint8_t\s*\*\s*base\s*\)\s*;")
_HOOK_CALL = re.compile(r"\b(Simpsons\w+)\s*\(\s*ctx\s*,\s*base\s*\)")
_IMPORT_CALL = re.compile(r"\b(__imp__\w+)\s*\(\s*ctx\s*,\s*base\s*\)")
_MAPPING = re.compile(r"(\{\s*0x[0-9a-fA-F]+\s*,\s*)(__imp__\w+)(\s*\})")
_NON_CODE = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
_HOOK_DEF = re.compile(r"\b(Simpsons\w+)\s*\([^;{}]*\)\s*\{")
_IMPORT_DEF = re.compile(r"PPC_FUNC\s*\(\s*(__imp__\w+)\s*\)\s*\{")
_NATIVE_NAME = re.compile(r"\b(Simpsons\w+|__imp__\w+)\b")
_DEFINE = re.compile(r"^[ \t]*#\s*define\s+(\w+)\(([^)\n]+)\)[ \t]*(.*)$", re.M)
_PASTED_HOOK = re.compile(r"\b(Simpsons\w+)\s*##\s*(\w+)\s*\(")


def code_only(text: str) -> str:
    """Mask comments/literals while preserving offsets for safe substitutions."""
    return _NON_CODE.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), text)


def source_section(path: str | Path) -> str:
    """Classify by the implementation unit, rather than ambiguous hook names."""
    stem = Path(path).stem.lower()
    if "audio" in stem:
        return "Audio"
    if stem.startswith(("filesystem", "save", "native_save", "content", "native_content")):
        return "FileIO"
    if stem.startswith("engine_") or stem in (
            "character_mesh", "static_shadow_mesh", "zprepass_vertices",
            "rigid_vertices", "skin_vertices", "sky_vertices"):
        return "Rendering"
    return "Runtime"


def implementation_sections(sources: dict[str, str]) -> dict[str, str]:
    definitions: dict[str, set[str]] = {}
    mentions: dict[str, set[str]] = {}
    for path, text in sources.items():
        code = code_only(text)
        section = source_section(path)
        for name in _NATIVE_NAME.findall(code):
            mentions.setdefault(name, set()).add(section)
        for pattern in (_HOOK_DEF, _IMPORT_DEF):
            for name in pattern.findall(code):
                definitions.setdefault(name, set()).add(section)
        # The audio boundary units generate hooks with token-pasting macros.
        # Resolve the actual suffix argument from each invocation instead of
        # guessing the section from the generated function's name.
        logical_lines = re.sub(r"\\\r?\n", " ", code)
        for macro, parameters, body in _DEFINE.findall(logical_lines):
            parameters = [parameter.strip() for parameter in parameters.split(',')]
            for prefix, suffix_parameter in _PASTED_HOOK.findall(body):
                if suffix_parameter not in parameters:
                    continue
                index = parameters.index(suffix_parameter)
                invocations = re.findall(r"\b" + re.escape(macro) + r"\s*\(([^()\n]*)\)", code)
                for invocation in invocations:
                    arguments = [argument.strip() for argument in invocation.split(',')]
                    if index >= len(arguments):
                        continue
                    suffix = arguments[index]
                    if suffix == suffix_parameter or not re.fullmatch(r"\w+", suffix):
                        continue
                    name = prefix + suffix
                    definitions.setdefault(name, set()).add(section)
                    mentions.setdefault(name, set()).add(section)
    result = {}
    for name, owners in mentions.items():
        # Macro-defined hooks still have their full identity at the macro call.
        owners = definitions.get(name, owners)
        if len(owners) != 1:
            raise RuntimeError(f"Ambiguous profiler implementation section for {name}: {sorted(owners)}")
        result[name] = next(iter(owners))
    return result


def import_names(header: str) -> list[str]:
    return sorted(set(_IMPORT.findall(code_only(header))))


def hook_declarations(text: str) -> dict[str, str]:
    declarations: dict[str, str] = {}
    for result, name in _HOOK_DECL.findall(code_only(text)):
        if name in declarations and declarations[name] != result:
            raise RuntimeError(f"Conflicting generated native hook signature: {name}")
        declarations[name] = result
    return declarations


def _insert_include(text: str) -> str:
    shared = '#include "ppc_recomp_shared.h"'
    if shared not in text:
        raise RuntimeError("Generated profiler boundary has no shared header include")
    if f'#include "{HEADER_NAME}"' in text:
        return text
    return text.replace(shared, shared + f'\n#include "{HEADER_NAME}"', 1)


def _replace_spans(text: str, replacements: list[tuple[int, int, str]]) -> str:
    # Replacement offsets refer to the unmodified text and must remain stable.
    for start, end, value in reversed(replacements):
        text = text[:start] + value + text[end:]
    return text


def instrument_mapping(text: str, names: list[str], *, enabled: bool = True) -> str:
    if not enabled:
        return text
    known = set(names)
    seen = set()
    replacements = []
    for match in _MAPPING.finditer(code_only(text)):
        name = match.group(2)
        if name not in known:
            raise RuntimeError(f"Generated mapping references undeclared import {name}")
        seen.add(name)
        replacements.append((match.start(2), match.end(2), f"SIMPSONS_PROFILE_IMPORT({name})"))
    if seen != known:
        raise RuntimeError(f"Generated imports missing mapping coverage: {sorted(known - seen)}")
    return _insert_include(_replace_spans(text, replacements))


def instrument_hooks(text: str, declarations: dict[str, str], *, enabled: bool = True) -> str:
    if not enabled:
        return text
    replacements = []
    for match in _HOOK_CALL.finditer(code_only(text)):
        name = match.group(1)
        if name not in declarations:
            raise RuntimeError(f"Generated native call has no supported declaration: {name}")
        if name in EXCLUDED_HOOKS:
            continue
        replacements.append((match.start(1), match.end(1), f"SIMPSONS_PROFILE_HOOK({name})"))
    if not replacements:
        return text
    return _insert_include(_replace_spans(text, replacements))


def instrument_direct_imports(text: str, names: list[str], *, enabled: bool = True) -> str:
    if not enabled:
        return text
    known = set(names)
    replacements = []
    for match in _IMPORT_CALL.finditer(code_only(text)):
        name = match.group(1)
        if name in known:
            replacements.append((match.start(1), match.end(1), f"SIMPSONS_PROFILE_IMPORT({name})"))
        elif not name.startswith("__imp__sub_"):
            raise RuntimeError(f"Generated direct call references undeclared import {name}")
    if not replacements:
        return text
    return _insert_include(_replace_spans(text, replacements))


def forwarding_header(names: list[str], hooks: dict[str, str], sections: dict[str, str]) -> str:
    text = ('// Generated by tools/recompile.py; edit the generator, not this file.\n'
            '#pragma once\n#include "ppc_context.h"\n'
            '#ifndef SIMPSONS_STALL_PROFILER\n#define SIMPSONS_STALL_PROFILER 1\n#endif\n'
            '#if SIMPSONS_STALL_PROFILER\n#include "runtime/stall_profiler.h"\n'
            '#define SIMPSONS_PROFILE_IMPORT(name) SimpsonsProfile##name\n'
            '#define SIMPSONS_PROFILE_HOOK(name) SimpsonsProfile_##name\n')
    for name in names:
        text += f'PPC_EXTERN_FUNC(SimpsonsProfile{name});\n'
    for name, result in sorted(hooks.items()):
        if name in EXCLUDED_HOOKS:
            continue
        section = sections.get(name, "Runtime")
        # Inline supplies one ODR/COMDAT definition across generated units;
        # noinline keeps profiler control flow outside recovered guest CFGs.
        text += (f'extern {result} {name}(PPCContext& ctx, uint8_t* base);\n'
                 f'__declspec(noinline) inline {result} SimpsonsProfile_{name}(PPCContext& ctx, uint8_t* base) {{\n'
                 f'  if (!Simpsons::StallProfiler::enabled) return {name}(ctx, base);\n'
                 f'  Simpsons::StallProfiler::Scope scope(Simpsons::StallProfiler::Section::{section}, "{name}", &ctx);\n')
        if name == "SimpsonsNativePresent":
            if result != "void":
                raise RuntimeError("Native Present profiler boundary must return void")
            text += (f'  {name}(ctx, base);\n  scope.finish();\n'
                     '  Simpsons::StallProfiler::frameBoundary(&ctx);\n')
        else:
            # Returning a void expression is valid C++; bool hook decisions retain
            # their exact value without reevaluating the real hook.
            text += f'  return {name}(ctx, base);\n'
        text += '}\n'
    text += ('#else\n#define SIMPSONS_PROFILE_IMPORT(name) name\n'
             '#define SIMPSONS_PROFILE_HOOK(name) name\n#endif\n')
    return text


def forwarding_source(names: list[str], sections: dict[str, str]) -> str:
    text = ('// Generated by tools/recompile.py; edit the generator, not this file.\n'
            '#include "ppc_recomp_shared.h"\n'
            f'#include "{HEADER_NAME}"\n#if SIMPSONS_STALL_PROFILER\n')
    for name in names:
        section = sections.get(name, "Runtime")
        label = name.removeprefix("__imp__")
        text += (f'PPC_FUNC(SimpsonsProfile{name}) {{\n'
                 f'  if (!Simpsons::StallProfiler::enabled) return {name}(ctx, base);\n'
                 f'  Simpsons::StallProfiler::Scope scope(Simpsons::StallProfiler::Section::{section}, "{label}", &ctx);\n'
                 f'  {name}(ctx, base);\n}}\n')
    return text + '#endif\n'


def instrument_generated(out: Path, chunks: list[str], sources: dict[str, str]) -> dict[str, int]:
    """Postprocess original generator output before calculating the AOT hashes."""
    names = import_names((out / "ppc_recomp_shared.h").read_text(encoding="utf-8"))
    sections = implementation_sections(sources)
    hooks: dict[str, str] = {}
    for chunk in chunks:
        for name, result in hook_declarations((out / chunk).read_text(encoding="utf-8")).items():
            if name in hooks and hooks[name] != result:
                raise RuntimeError(f"Conflicting generated native hook signature: {name}")
            hooks[name] = result
    (out / HEADER_NAME).write_text(forwarding_header(names, hooks, sections), encoding="utf-8")
    (out / SOURCE_NAME).write_text(forwarding_source(names, sections), encoding="utf-8")
    mapping = out / "ppc_func_mapping.cpp"
    mapping.write_text(instrument_mapping(mapping.read_text(encoding="utf-8"), names), encoding="utf-8")
    for chunk in chunks:
        path = out / chunk
        original = path.read_text(encoding="utf-8")
        modified = instrument_hooks(original, hooks)
        modified = instrument_direct_imports(modified, names)
        if modified != original:
            path.write_text(modified, encoding="utf-8")
    return {"imports": len(names), "native_hooks": len(set(hooks) - EXCLUDED_HOOKS)}
