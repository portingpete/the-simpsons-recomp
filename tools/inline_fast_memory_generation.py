"""Inline existing fast scalar access only in exact, hash-qualified guest bodies."""
from __future__ import annotations

import hashlib
from pathlib import Path
import re

from stall_profile_generation import code_only

HEADER = '#include "runtime/aot_inline_fast_memory.h"'
OPERATIONS = {"PPC_LOAD_U32":"PPC_FAST_INLINE_LOAD_U32",
              "PPC_STORE_U32":"PPC_FAST_INLINE_STORE_U32",
              "PPC_LOAD_U8":"PPC_FAST_INLINE_LOAD_U8",
              "PPC_STORE_U8":"PPC_FAST_INLINE_STORE_U8",
              "PPC_LOAD_U16":"PPC_FAST_INLINE_LOAD_U16",
              "PPC_STORE_U16":"PPC_FAST_INLINE_STORE_U16"}
MEMORY = re.compile(r"\bPPC_(?:FAST_INLINE_)?(?:LOAD|STORE)_U\d+\b")
BYTE_ENTRY = "8281E9B0"
BYTE_PROLOGUE = ("PPC_FUNC_IMPL(__imp__sub_8281E9B0) {\n"
                 "\tPPC_FUNC_PROLOGUE();\n\tPPC_TRACE_ENTRY(ctx, 0x8281E9B0);\n")
BYTE_SCOPE = '\tSIMPSONS_PROFILE_GUEST_SCOPE("Guest.byteDecode8281E9B0");\n'
PAIR_ENTRY = "82B2A8D0"
PAIR_PROLOGUE = ("PPC_FUNC_IMPL(__imp__sub_82B2A8D0) {\n"
                 "\tPPC_FUNC_PROLOGUE();\n\tPPC_TRACE_ENTRY(ctx, 0x82B2A8D0);\n")
PAIR_SCOPE = '\tSIMPSONS_PROFILE_GUEST_SCOPE("Guest.pairShift82B2A8D0");\n'
VM_ENTRY = "82844AB0"
VM_PROLOGUE = ("PPC_FUNC_IMPL(__imp__sub_82844AB0) {\n"
               "\tPPC_FUNC_PROLOGUE();\n\tPPC_TRACE_ENTRY(ctx, 0x82844AB0);\n")
VM_SCOPE = '\tSIMPSONS_PROFILE_GUEST_SCOPE("Guest.vmSetup82844AB0");\n'
VERIFIED_GUEST_SCOPES = {BYTE_ENTRY:(BYTE_PROLOGUE,BYTE_SCOPE),
                         PAIR_ENTRY:(PAIR_PROLOGUE,PAIR_SCOPE),
                         VM_ENTRY:(VM_PROLOGUE,VM_SCOPE)}


def function_span(text: str, entry: str) -> tuple[int,int]:
    code = code_only(text)
    matches = list(re.finditer(r"\bPPC_FUNC_IMPL\(__imp__sub_"+re.escape(entry)+r"\)\s*\{",code))
    if len(matches)!=1:
        raise RuntimeError(f"Expected exactly one original {entry} body")
    match = matches[0]
    depth = 1
    for offset in range(match.end(),len(code)):
        depth += (code[offset]=="{")-(code[offset]=="}")
        if depth==0:
            return match.start(),offset+1
    raise RuntimeError(f"Unclosed original {entry} body")


def without_verified_guest_scope(body: str, entry: str) -> tuple[str,str]:
    # Profiling is applied after memory rewriting. Accept only each exact,
    # separately qualified scope at its original trace boundary; production
    # keeps it, while hashes and external-fast references use original code.
    scopes = re.findall(r"\bSIMPSONS_PROFILE_GUEST_SCOPE\b",code_only(body))
    if not scopes:
        return body,""
    qualified = VERIFIED_GUEST_SCOPES.get(entry)
    if qualified is None or len(scopes)!=1 or not body.startswith(qualified[0]+qualified[1]):
        raise RuntimeError(f"Inline fast memory unexpected guest scope: {entry}")
    prologue,scope = qualified
    return body.replace(prologue+scope,prologue,1),scope


def original_body(body: str, entry: str|None = None) -> str:
    if entry is not None:
        body,_ = without_verified_guest_scope(body,entry)
    # Reversing only distinct code identifiers makes a second transform
    # idempotent, and supplies a mechanically unchanged test reference body.
    replacements = []
    reverse = {value:key for key,value in OPERATIONS.items()}
    for match in MEMORY.finditer(code_only(body)):
        if match.group() in reverse:
            replacements.append((match.start(),match.end(),reverse[match.group()]))
    for start,end,value in reversed(replacements):
        body = body[:start]+value+body[end:]
    return body


def body_digest(body: str) -> str:
    return hashlib.sha256(body.encode("utf-8")).hexdigest()


def transform(text: str, selections: list[dict]) -> tuple[str,list[dict]]:
    coverage = []
    replacements = []
    seen = set()
    for selection in selections:
        entry = selection["entry"]
        if not re.fullmatch(r"[0-9A-F]{8}",entry) or entry in seen:
            raise RuntimeError(f"Invalid or duplicate inline function: {entry}")
        seen.add(entry)
        start,end = function_span(text,entry)
        unprofiled,scope = without_verified_guest_scope(text[start:end],entry)
        before = original_body(unprofiled)
        if body_digest(before)!=selection["body_sha256"]:
            raise RuntimeError(f"Inline fast memory original body changed: {entry}; requalify selection")
        operations = list(MEMORY.finditer(code_only(before)))
        if not operations or any(match.group() not in OPERATIONS for match in operations):
            raise RuntimeError(f"Inline fast memory unsupported access in {entry}")
        # Keep the two existing U32 qualifications and coverage unchanged.
        counts = {name:count for name in OPERATIONS
                  if (count:=sum(match.group()==name for match in operations))}
        if counts!=selection["operations"]:
            raise RuntimeError(f"Inline fast memory access counts changed: {entry}")
        after = before
        for match in reversed(operations):
            after = after[:match.start()]+OPERATIONS[match.group()]+after[match.end():]
        if original_body(after)!=before:
            raise RuntimeError(f"Inline fast memory changed original instruction body: {entry}")
        if scope:
            prologue = VERIFIED_GUEST_SCOPES[entry][0]
            after = after.replace(prologue,prologue+scope,1)
        replacements.append((start,end,after))
        coverage.append({"entry":entry,"body_sha256":selection["body_sha256"],"operations":counts})
    for start,end,after in sorted(replacements,reverse=True):
        text = text[:start]+after+text[end:]
    if selections and HEADER not in text:
        shared = '#include "ppc_recomp_shared.h"'
        if text.count(shared)!=1:
            raise RuntimeError("Inline fast memory requires one generated shared header")
        text = text.replace(shared,shared+"\n"+HEADER,1)
    return text,coverage


def instrument_generated(directory: Path,chunks: list[str],selections: list[dict]) -> list[dict]:
    selected = {}
    for selection in selections:
        chunk = selection.get("chunk")
        if not isinstance(chunk,int) or chunk<0:
            raise RuntimeError("Invalid inline fast memory chunk")
        name = f"ppc_recomp.{chunk}.cpp"
        if name not in chunks:
            raise RuntimeError(f"Inline fast memory chunk missing: {name}")
        selected.setdefault(name,[]).append(selection)
    coverage = []
    for name,entries in selected.items():
        path = directory/name
        before = path.read_text(encoding="utf-8")
        after,rows = transform(before,entries)
        if after!=before:
            path.write_text(after,encoding="utf-8")
        coverage.extend({"chunk":name,**row} for row in rows)
    return coverage
