"""Offline catalog of the second, game-specific 24-row FX table (not all post FX).

Reuses analyze_effect_catalog as an unchanged library. The only new metadata
grammar is the bounded context tail, whose contents remain opaque. No original
code or shader executes; --disassemble only invokes the existing disassembler.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
import analyze_effect_catalog as C
import analyze_poststart_integration as E

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/post-effect-catalog"
RESULT = ROOT / "analysis/native-post-effect-catalog.json"
TABLE = 0x82CD1448
TABLE_SHA = "94f5100ca31a244de3d1a64a203fdcf191a395a80e3a868fb288826a1b8ad324"
# callback: vtable, vtable store PC, slot+C finalizer, slot0 destructor.
TYPES = {
    0x823C7FB8: (0x82061430, 0x823C8014, 0x823C8290, 0x823C7F58),
    0x823C8698: (0x82061494, 0x823C86F4, 0x823C8720, 0x823C8638),
    0x823C89A8: (0x820614BC, 0x823C8A04, 0x823C8A30, 0x823C8948),
    0x823C8D90: (0x820614E4, 0x823C8DEC, 0x823C8F68, 0x823C8D30),
    0x823C96A0: (0x82061598, 0x823C96FC, 0x823C9868, 0x823C9640),
    0x823CA6B0: (0x820616C0, 0x823CA714, 0x823CA7C0, 0x823CA650),
    0x823CA960: (0x82061714, 0x823CA9C8, 0x823CAA98, 0x823CA900),
    0x823CABE8: (0x82061758, 0x823CAC48, 0x823CACB8, 0x823CAB88),
}
QUERY = {0x823C7CA0: "technique", 0x823C7B20: "parameter"}
ABI = set(range(0x82A3C380, 0x82A3C440, 4))
PINS = {
    0x823C708C: 0x3D6082CD, 0x823C7090: 0x38800018,
    0x823C7094: 0x386B1448, 0x823C7098: 0x4833A951,
    0x82701B80: 0x3D6082CF, 0x82701B84: 0x38800019,
    0x82701B88: 0x386BFD20, 0x82701B8C: 0x4BFFFE5D,
    0x826B7540: 0x4BFFDC29, 0x826B5178: 0x817F001C,
    0x826B517C: 0x814B02AC, 0x826B5180: 0x824B020C,
    0x823CA364: 0x817E99A0, 0x823CA36C: 0x409A0014,
    0x823CA374: 0x386B1674, 0x823CA378: 0x4807B569,
    0x823CA37C: 0x907E99A0, 0x823CA3D0: 0x807F99A0,
    0x823CA3DC: 0x4807732D, 0x823CA3E4: 0x917F99A0,
    0x82C16EA0: 0x81440054, 0x82C16EA8: 0x7C6B5214,
    0x82C17AC0: 0x81640050, 0x82C17ADC: 0x91640050,
    0x82C17AE0: 0x4BFFF371, 0x82C17AEC: 0x7C832214,
}


def contexts(v, arrays, copy_prefix):
    """Reference parser geometry plus the original +54-sized opaque tail.

    82C16E50 returns aligned base geometry + context[54]. The relocated +50
    pointer equals the end of base geometry in this corpus, not the next context.
    Only observed tail lengths 0 and 16 are admitted. No tail record semantics.
    """
    at, count, size = v.words(0x224, 3)
    C.require(at == copy_prefix, "copy-prefix/context start disagreement")
    ar = v.span(at, size, "contexts", 16)
    C.require(1 <= count <= 16384, "context count", "bounds")
    stride = 0x58 + 64*(v.u32(0x120)+v.u32(0x124))
    for field in (0x130, 0x134):
        if v.u32(field):
            stride = C.align(stride)+16*v.u32(field)
    stride = C.align(stride)
    cursor, end, result = at, at+size, {}
    entries = {a["label"].split()[0]: {e["offset"]: e for e in a["entries"]} for a in arrays[:2]}
    for i in range(count):
        C.require(cursor+0x58 <= end, "context header exceeds array", "bounds")
        h = v.words(cursor, 22)
        C.require(h[21] in (0, 16), "unqualified context tail length", "unrecognized_framing")
        n = stride+h[21]
        C.require(cursor+n <= end, "context stride exceeds array", "bounds")
        C.require(h[20] == cursor+stride, "context base-end link / geometry mismatch")
        links = []
        for p in h[:18]:
            if p != C.NULL:
                C.require(cursor+0x58 <= p < cursor+stride, "context inner link outside base", "bounds")
            links.append(None if p == C.NULL else p)
        inner = cursor+0x58
        for group, field in ((0, 0x120), (8, 0x124)):
            width = 8*v.u32(field)
            for j in range(8):
                C.require(h[group+j] == inner+j*width, "context bookkeeping link geometry")
                v.take(h[group+j], width, "context bookkeeping slice")
            inner += 8*width
        for j, field in ((16, 0x130), (17, 0x134)):
            words = v.u32(field)
            if words:
                inner = C.align(inner-cursor)+cursor
                C.require(h[j] == inner, "context vector-cache link geometry")
                v.take(inner, words*16, "context vector-cache slice")
                inner += words*16
            else:
                C.require(h[j] == C.NULL, "empty context vector-cache link")
        C.require(C.align(inner-cursor)+cursor == cursor+stride, "context inner extent")
        associated = {}
        for j, stage in ((18, "vertex"), (19, "pixel")):
            C.require(h[j] in entries[stage], "context shader link not an entry boundary", "bounds")
            associated[stage] = dict(entry_offset=h[j], shader_sha256=entries[stage][h[j]]["shader"])
        row = v.span(cursor, n, "context", 16)
        row.update(index=i, end_link=h[20], extension_bytes=h[21], inner_links=links, shaders=associated)
        if h[21]:
            tail = v.span(h[20], h[21], "opaque context tail", 16)
            tail.update(words=[C.hx(w) for w in v.words(h[20], h[21]//4)],
                        qualification="framing and identity only; tail semantics/application unqualified")
            row["opaque_tail"] = tail
        result[cursor] = row
        cursor += n
    C.require(cursor == end, "context count/totalBytes mismatch")
    return ar, result


def inspect_blob(blob, va=0):
    """Compose unchanged metadata decoders with the bounded contexts above."""
    envelope = C.View(blob, va)
    magic, n, prefix = envelope.words(0, 3)
    C.require(magic == 0xA3D70141, "FX magic/version", "unrecognized_framing")
    C.require(n >= 0x310 and n+12 == len(blob) and 0x310 <= prefix <= n, "exact FX envelope length/copy prefix")
    v = C.View(envelope.take(12, n), va+12)
    arrays, shaders, states = C.resource_arrays(v)
    context_array, ctx = contexts(v, arrays, prefix)
    tech = C.techniques(v, ctx, states)
    params = C.parameters(v)
    auxiliary = C.auxiliary_bounds(v)
    counts = Counter()
    for t in tech:
        for p in t["passes"]:
            counts["scalar"] += len(p["scalar_states"])
            counts["sampler"] += len(p["sampler_states"])
    cache_scalar = sum(len(t["passes"][0]["scalar_states"]) for t in tech)
    cache_sampler = sum(len(t["passes"][0]["sampler_states"]) for t in tech)
    return dict(blob_va=C.hx(va), body_va=C.hx(va+12), bytes=len(blob), sha256=C.sha(blob),
                body_bytes=n, copy_prefix_bytes=prefix, resource_arrays=arrays, contexts=list(ctx.values()),
                context_array=context_array, shaders=shaders, techniques=tech, parameters=params, auxiliary_bounds=auxiliary,
                state_profile="literal_only", pass_state_counts=dict(counts),
                engine_cache=dict(technique_rows=len(tech), pass_selection="first pass of each technique (826B4828)",
                                  scalar_rows=cache_scalar, sampler_rows=cache_sampler,
                                  bytes=24*len(tech)+12*cache_scalar+16*cache_sampler,
                                  saved_previous_values="unwritten by reflection"),
                has_shared_parameters=v.u32(0x114) > 0,
                pool_lifetime_requirement="lease required if created against a nonnull root, including zero shared-name profiles",
                checked_spans=v.spans, opaque_uninterpreted_body_intervals=C.opaque_gaps(v),
                unrecognized_framing=[], render_readiness="not_assessed")


def constant(b, start, pc, reg, depth=0):
    """Bounded immediate def-use slice; no loads, execution or memory model.

    Used only on byte-pinned straight-line query-name/vtable argument setup.
    Volatile registers stop at calls; unsupported definitions return unknown.
    """
    if depth > 8:
        return None
    for pos in range(pc-4, start-1, -4):
        w = E.word(b, pos)
        op, rt, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        edge = E.branch(pos, w)
        if ((edge and edge[1]) or w == 0x4E800421) and reg < 14:
            return None
        if op in (14, 15) and rt == reg:
            imm = w & 0xFFFF
            if imm & 0x8000:
                imm -= 0x10000
            val = 0 if ra == 0 else constant(b, start, pos, ra, depth+1)
            return None if val is None else (val+(imm << (16 if op == 15 else 0))) & 0xFFFFFFFF
        if op in (24, 25) and ra == reg:
            val = constant(b, start, pos, rt, depth+1)
            return None if val is None else val | ((w & 0xFFFF) << (16 if op == 25 else 0))
        if op == 31 and ((w >> 1) & 1023) == 444 and ra == reg:
            return constant(b, start, pos, rt, depth+1) if rt == ((w >> 11) & 31) else None
        if op in (32, 33, 34, 35, 40, 41, 42, 43, 58) and rt == reg:
            return None
    return None


def edges(b, start, n):
    result = []
    for pc in range(start, start+n, 4):
        edge = E.branch(pc, E.word(b, pc))
        if edge and (edge[1] or not start <= edge[0] < start+n):
            result.append(dict(pc=C.hx(pc), target=C.hx(edge[0]), linked=edge[1], abi_save_restore=edge[0] in ABI))
    return result


def finalizer_memory_audit(b, start, n):
    """Allow only the observed scalar query/publication body and stack saves.

    This is an instruction-shape audit, not an executing CPU model. In particular
    r30 may hold an FX ID, but no data access through r30/r3 is admitted here.
    The separately reported 826B74C8 callee remains an unqualified FX reader.
    """
    publications = []
    for pc in range(start, start+n, 4):
        w = E.word(b, pc)
        op, rt, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        off = w & 0xFFFF
        signed = off-0x10000 if off & 0x8000 else off
        if op == 32:
            C.require((ra == 31 and off in (8, 0x1C)) or ra == 1,
                      "new finalizer data load outside CPU wrapper/stack", "unrecognized_framing")
        elif op == 36:
            if ra == 1:
                continue
            if ra == 31:
                C.require(off in (0xA8, 0xAC, 0xB0, 0xB4), "new typed publication field")
                destination = dict(kind="typed_CPU_object", offset=off)
            else:
                base = constant(b, start, pc, ra)
                address = None if base is None else (base+signed) & 0xFFFFFFFF
                C.require(address is not None and 0x82D098FC <= address <= 0x82D0999C,
                          "unqualified finalizer publication destination", "unrecognized_framing")
                destination = dict(kind="CPU_global", va=C.hx(address))
            publications.append(dict(pc=C.hx(pc), word=C.hx(w), source_register=rt, destination=destination))
        elif op in (37, 58, 62):
            C.require(ra == 1, "new non-stack wide/update access", "unrecognized_framing")
        elif op == 31:
            xo = (w >> 1) & 1023
            C.require(w in (0x7D8802A6, 0x7D8803A6) or (xo == 444 and rt == ((w >> 11) & 31)),
                      "new finalizer extended instruction", "unrecognized_framing")
        elif op == 19:
            C.require(w == 0x4E800020, "new finalizer indirect branch", "unrecognized_framing")
        else:
            C.require(op in (10, 14, 15, 16, 18), "new finalizer instruction class", "unrecognized_framing")
    return dict(publications=publications, non_stack_loads="typed CPU object +8 name / +1C FX identity only",
                own_body_inline_FX_reads_or_setters=False, callee_caveat="826B74C8 reaches raw SDK FX reader 826B5168")


def cpu_evidence(b):
    _, pd = E.layout(b)
    for pc, word in PINS.items():
        C.require(E.word(b, pc) == word, f"CPU semantic pin {pc:08X}", "identity")
    iv = C.View(b, C.BASE)
    type_rows = []
    starts = {0x823C7080, 0x82701B70, 0x827019E8, 0x823CA338, 0x823CA3A8,
              0x826B4F60, 0x826B74C8, 0x826B5168, 0x82C17568}
    for callback, (vt, store, finalizer, destructor) in TYPES.items():
        C.require(E.word(b, store) == 0x917F0000 and constant(b, callback, store, 11) == vt,
                  "typed constructor vtable store", "identity")
        C.require(E.word(b, vt+12) == finalizer and E.word(b, vt) == destructor,
                  "vtable finalizer/destructor slots", "identity")
        direct = edges(b, finalizer, pd[finalizer][0])
        targets = {int(e["target"], 16) for e in direct if not e["abi_save_restore"]}
        C.require(targets == {0x826B74C8} | (set(QUERY) & targets) and 0x823C7CA0 in targets,
                  "new finalizer non-query callee", "unrecognized_framing")
        C.require(all(E.word(b, pc) != 0x4E800421 for pc in range(finalizer, finalizer+pd[finalizer][0], 4)),
                  "new finalizer indirect call", "unrecognized_framing")
        queries = []
        for edge in direct:
            target, pc = int(edge["target"], 16), int(edge["pc"], 16)
            if target in QUERY:
                address = constant(b, finalizer, pc, 4)
                C.require(address is not None, "unresolved finalizer query name", "unrecognized_framing")
                name = iv.string(address-C.BASE)
                queries.append(dict(pc=edge["pc"], api=edge["target"], kind=QUERY[target], name=name,
                                    name_va=C.hx(address), name_sha256=C.sha(E.span(b, address, len(name)+1))))
        ctor_edges = edges(b, callback, pd[callback][0])
        ctor_targets = {int(e["target"], 16) for e in ctor_edges if not e["abi_save_restore"]}
        base = 0x823CA338 if callback < 0x823CA000 else 0x826B4F60
        C.require(ctor_targets == {0x8269BF70, base}, "new constructor callee", "unrecognized_framing")
        type_rows.append(dict(callback=C.hx(callback), vtable=C.hx(vt), vtable_store_pc=C.hx(store),
                              destructor=C.hx(destructor), finalizer=C.hx(finalizer),
                              constructor_calls=ctor_edges, finalizer_calls=direct, queries=queries,
                              memory_audit=finalizer_memory_audit(b, finalizer, pd[finalizer][0]),
                              constructor_base=C.hx(base), creates_common_declaration_if_empty=base == 0x823CA338,
                              query_hooks_alone_sufficient=False,
                              boundary="826B74C8 -> 826B5168 raw FX+2AC/+20C reader; own finalizer body only queries/CPU publication"))
        starts.update((callback, finalizer, destructor))
    C.require(constant(b, 0x823CA338, 0x823CA378, 3) == 0x82061674, "declaration source argument", "identity")
    spans = []
    for va in sorted(starts | {0x82C16E50}):
        n = 0x60 if va == 0x82C16E50 else pd[va][0]
        raw = E.span(b, va, n)
        spans.append(dict(va=C.hx(va), bytes=n, sha256=C.sha(raw), extent="leaf" if va == 0x82C16E50 else "pdata",
                          pdata_va=None if va == 0x82C16E50 else C.hx(pd[va][1]),
                          direct_edges=edges(b, va, n)))
    # Independent original SDK pins used by the unchanged parser.
    for va, n, digest, _ in C.SDK_SPANS:
        C.require(C.sha(E.span(b, va, n)) == digest, "original parser SDK evidence", "identity")
    declaration = E.span(b, 0x82061674, 36)
    C.require(C.sha(declaration) == "0433df1ef161551bfd46cb5edf8e33da819182bb79db90bd1ee610ffe757f473",
              "post-family declaration bytes", "identity")
    return dict(types=type_rows, spans=spans, semantic_word_pins={C.hx(k): C.hx(v) for k, v in PINS.items()},
                word_count=sum(s["bytes"]//4 for s in spans),
                callers=[dict(pc="0x823C7098", target="0x827019E8", table=C.hx(TABLE), count=24),
                         dict(pc="0x82701B8C", target="0x827019E8", table=C.hx(C.TABLE), count=25)],
                declaration=dict(source_va="0x82061674", bytes=36, sha256=C.sha(declaration),
                                 words=[C.hx(w) for w in struct.unpack(">9I", declaration)],
                                 create_pc="0x823CA378", create_api="0x824458E0", cache_va="0x82D099A0",
                                 release_pc="0x823CA3DC", release_api="0x82441708", clear_pc="0x823CA3E4",
                                 ownership="one global cached declaration across rows2..6; destructor releases if nonzero then clears; native coverage not established"),
                bounded_closure="New constructors/finalizers/destructors and shared CPU bases are byte-pinned. Stop at existing CPU allocator/string/cleanup services, query APIs, raw-FX reader and declaration create/release. No runtime safety claim.")


def shared_compatibility(rows, common):
    base = common["parameters"]["shared"]
    by_name = {p["name"]: base["descriptors"][p["descriptor_index"]] for p in base["named_parameters"]}
    groups, conflicts, new_names = {}, [], []
    for row in rows:
        s = row["parameters"]["shared"]
        groups.setdefault(s["named_count"], []).append(row["index"])
        for p in s["named_parameters"]:
            d, old = s["descriptors"][p["descriptor_index"]], by_name.get(p["name"])
            if old is None:
                new_names.append(dict(row=row["index"], name=p["name"]))
            elif any(d[k] != old[k] for k in ("words", "handle", "index", "name")) or d["default"]["sha256"] != old["default"]["sha256"]:
                conflicts.append(dict(row=row["index"], name=p["name"]))
        C.require(s["descriptors"][0]["words"] == base["descriptors"][0]["words"], "shared descriptor sentinel differs")
    return dict(reference_name="littextured", reference_blob_va=common["blob_va"], reference_sha256=common["sha256"],
                profiles=[dict(named_count=n, rows=indices) for n, indices in sorted(groups.items())],
                new_names=new_names, descriptor_handle_default_conflicts=conflicts,
                common_eleven_or_exact_four_prefix=not new_names and not conflicts and set(groups) <= {4, 11},
                conclusion="No new shared namespace/storage is required by these serialized profiles. Reuse requires validating the actual existing pool's common-11 metadata and retaining live values/ownership. Do not invoke 82C181E8 on a nonempty pool; this is not execution or proof of the general 82C18530 merge.")


def catalog(data):
    C.validate_image(data)
    iv = C.View(data, C.BASE)
    table = iv.span(TABLE-C.BASE, 24*16, "second registration table", 4)
    C.require(table["sha256"] == TABLE_SHA, "second registration table identity", "identity")
    cpu = cpu_evidence(data)
    type_map = {t["callback"]: t for t in cpu["types"]}
    rows, unique = [], {}
    for i in range(24):
        at = TABLE-C.BASE+16*i
        blob, name_va, output, callback = iv.words(at, 4)
        C.require(output == 0 and callback in TYPES, "registration output/callback identity", "identity")
        name = iv.string(name_va-C.BASE)
        n = iv.u32(blob-C.BASE+4)+12
        raw = iv.take(blob-C.BASE, n, "exact FX envelope")
        row = inspect_blob(raw, blob)
        row.update(index=i, name=name, row_va=C.hx(TABLE+16*i), row_sha256=C.sha(iv.take(at, 16)),
                   name_va=C.hx(name_va), name_bytes=len(name)+1, name_sha256=C.sha(E.span(data, name_va, len(name)+1)),
                   body_sha256=C.sha(raw[12:]),
                   output_initial=output, typed_callback_va=C.hx(callback),
                   typed_finalizer_va=type_map[C.hx(callback)]["finalizer"])
        try:
            old = C.inspect_blob(raw, blob)
        except C.CatalogError as error:
            row["reference_parser"] = dict(status="rejected", kind=error.kind, message=str(error))
        else:
            C.require(old == {k: row[k] for k in old}, "reference parser disagreement")
            row["reference_parser"] = dict(status="accepted_identical_metadata")
        known = {"technique": {t["name"] for t in row["techniques"]},
                 "parameter": {p["name"] for s in row["parameters"].values() for p in s["named_parameters"]}}
        row["finalizer_query_membership"] = [dict(q, serialized_name_present=q["name"] in known[q["kind"]]) for q in type_map[C.hx(callback)]["queries"]]
        row["remaining_semantic_gaps"] = (["nonzero context tail application semantics"] if any(x["extension_bytes"] for x in row["contexts"]) else [])
        rows.append(row)
        for s in row["shaders"]:
            key = (s["stage"], s["bytes"], s["sha256"])
            unique.setdefault(key, dict(stage=s["stage"], bytes=s["bytes"], sha256=s["sha256"], occurrences=[]))["occurrences"].append(dict(row=i, name=name, va=s["va"]))
    physical = sorted(rows, key=lambda r: int(r["blob_va"], 16))
    for r, nxt in zip(physical, physical[1:]):
        gap = int(nxt["blob_va"], 16)-int(r["blob_va"], 16)-r["bytes"]
        C.require(gap >= 0, "overlapping second-table FX", "bounds")
        r.update(physical_next_row=nxt["index"], gap_to_next_blob_bytes=gap)
    baseline = C.catalog(data)
    shared = shared_compatibility(rows, baseline["rows"][1])
    C.require(shared["common_eleven_or_exact_four_prefix"], "second-table shared namespace conflict", "unrecognized_framing")
    old_shaders = {(s["stage"], s["bytes"], s["sha256"]) for s in baseline["unique_shaders"]}
    for key, shader in unique.items():
        shader["complete_identity_seen_in_first25"] = key in old_shaders
    summary = dict(rows=len(rows), blob_bytes=sum(r["bytes"] for r in rows),
                   techniques=sum(len(r["techniques"]) for r in rows),
                   passes=sum(len(t["passes"]) for r in rows for t in r["techniques"]),
                   shader_occurrences=sum(len(r["shaders"]) for r in rows), unique_shaders=len(unique),
                   unique_shaders_by_stage=dict(Counter(s["stage"] for s in unique.values())),
                   shader_identities_shared_with_first25=sum(k in old_shaders for k in unique),
                   private_named_parameters=sum(r["parameters"]["private"]["named_count"] for r in rows),
                   private_leaves=sum(r["parameters"]["private"]["leaf_count"] for r in rows),
                   shared_named_parameter_occurrences=sum(r["parameters"]["shared"]["named_count"] for r in rows),
                   scalar_state_pass_references=sum(r["pass_state_counts"]["scalar"] for r in rows),
                   sampler_state_pass_references=sum(r["pass_state_counts"]["sampler"] for r in rows),
                   literal_only_rows=list(range(24)), nonzero_context_tail_rows=[r["index"] for r in rows if r["remaining_semantic_gaps"]],
                   reference_parser_accepted_rows=[r["index"] for r in rows if r["reference_parser"]["status"] == "accepted_identical_metadata"],
                   distinct_typed_callbacks=len(TYPES), distinct_typed_finalizers=len(TYPES))
    return dict(schema=1, source=baseline["source"], registration_table=table, rows=rows,
                unique_shaders=list(unique.values()), shared_pool=shared, cpu_evidence=cpu, summary=summary,
                library=dict(path="tools/analyze_effect_catalog.py", sha256=C.sha((ROOT/"tools/analyze_effect_catalog.py").read_bytes()),
                             modified=False, extension="local context framing; no monkey-patching or relaxed main parser"),
                qualification="Offline game-specific second table; not all postprocessing. No SDK execution, shader instruction interpretation, build, native compilation or rendering readiness.")


def selftests(data, result):
    """Faults hit structural/semantic checks separately from whole-image identity."""
    passed = []
    def rejects(label, action, reason):
        try:
            action()
        except (C.CatalogError, ValueError) as error:
            C.require(reason in str(error), f"{label}: unrelated rejection {error}")
            passed.append(dict(name=label, outcome="rejected", actual=str(error)))
            return
        raise C.CatalogError("test_failure", f"{label} unexpectedly accepted")
    def mutate(raw, at, value):
        m = bytearray(raw)
        struct.pack_into(">I", m, at, value)
        return bytes(m)
    def blob(index):
        r = result["rows"][index]
        return E.span(data, int(r["blob_va"], 16), r["bytes"])
    first, edge = blob(0), blob(4)
    r0, r4 = result["rows"][0], result["rows"][4]
    rejects("whole image modified", lambda: C.validate_image(mutate(data, TABLE-C.BASE+8, 1)), "identity")
    rejects("truncated FX", lambda: inspect_blob(first[:-4]), "exact FX envelope")
    rejects("bad FX magic", lambda: inspect_blob(mutate(first, 0, 0)), "FX magic")
    rejects("resource count overrun", lambda: inspect_blob(mutate(first, 12+0x234, 0xFFFFFFFF)), "resource count")
    shader = r0["shaders"][0]
    rejects("wrong shader stage", lambda: inspect_blob(mutate(first, 12+shader["offset"], 0)), "shader stage tag")
    context = r0["contexts"][1]["offset"]
    rejects("shader interior link", lambda: inspect_blob(mutate(first, 12+context+0x48, r0["contexts"][1]["shaders"]["vertex"]["entry_offset"]+4)), "not an entry boundary")
    state = r0["resource_arrays"][2]["entries"][1]["offset"]
    literals = struct.unpack_from(">I", first, 12+state+0x18)[0]
    C.require(literals > 0, "mutation requires a literal state")
    parameter_state = mutate(mutate(first, 12+state+0x10, 1), 12+state+0x18, literals-1)
    rejects("parameter-driven state category", lambda: inspect_blob(parameter_state), "parameter-driven state")
    desc = r0["parameters"]["private"]["descriptor_offset"]
    rejects("descriptor sentinel changed", lambda: inspect_blob(mutate(first, 12+desc, 1)), "descriptor sentinel")
    cell = struct.unpack_from(">I", first, 12+0x10C)[0]
    rejects("shared pointer cell out of bounds", lambda: inspect_blob(mutate(first, 12+cell, len(first)+16)), "outside")
    last = r4["contexts"][1]["offset"]
    rejects("context +50 points after tail", lambda: inspect_blob(mutate(edge, 12+last+0x50, r4["contexts"][1]["end_link"]+16)), "base-end")
    rejects("tail erased without fixing array extent", lambda: inspect_blob(mutate(edge, 12+last+0x54, 0)), "count/totalBytes")
    rejects("unqualified tail length", lambda: inspect_blob(mutate(edge, 12+last+0x54, 32)), "tail length")
    rejects("original parser still rejects extension", lambda: C.inspect_blob(edge), "context end link / stride mismatch")
    rejects("second caller count changed", lambda: cpu_evidence(mutate(data, 0x823C7090-C.BASE, 0x38800017)), "semantic pin")
    rejects("vtable finalizer changed", lambda: cpu_evidence(mutate(data, TYPES[0x823CA960][0]+12-C.BASE, 0)), "vtable finalizer")
    rejects("constructor vtable changed", lambda: cpu_evidence(mutate(data, TYPES[0x823CA960][1]-C.BASE, 0x60000000)), "vtable store")
    rejects("declaration source changed", lambda: cpu_evidence(mutate(data, 0x82061674-C.BASE, 1)), "declaration bytes")
    rejects("inline load through FX identity", lambda: cpu_evidence(mutate(data, 0x823CA7F4-C.BASE, 0x83DE02AC)), "data load outside")
    altered = json.loads(json.dumps(r0))
    altered["parameters"]["shared"]["descriptors"][1]["default"]["sha256"] = "0"*64
    _, va, n, _ = C.PROFILES[1]
    common = C.inspect_blob(E.span(data, va, n), va)
    C.require(shared_compatibility([altered], common)["descriptor_handle_default_conflicts"], "shared changed default not detected")
    passed.append(dict(name="shared default conflict", outcome="detected"))
    changed_tail = inspect_blob(mutate(edge, 12+r4["contexts"][1]["end_link"], 11))
    C.require(changed_tail["contexts"][1]["opaque_tail"]["sha256"] != r4["contexts"][1]["opaque_tail"]["sha256"], "opaque tail identity not tracked")
    passed.append(dict(name="opaque tail content mutation", outcome="identity changed; intentionally not assigned invented semantics"))
    for name, va, n, _ in C.PROFILES:
        raw = E.span(data, va, n)
        C.require(inspect_blob(raw, va) == C.inspect_blob(raw, va), f"old corpus metadata differs: {name}")
    passed.append(dict(name="all first25 reference equivalence", outcome="25 complete metadata trees identical"))
    return dict(cases=len(passed), passed=passed, runtime_execution=False)


def disassemble(data, source, result, compare=False):
    output = []
    for span in result["cpu_evidence"]["spans"]:
        va, n = int(span["va"], 16), span["bytes"]
        text = subprocess.check_output([str(ROOT/"build/generator-ninja/SimpsonsDisasm.exe"), str(source), hex(C.BASE), hex(va), str(n//4)], text=True)
        E.checked_decode(data, va, n, text)
        output.append(f"{span['va']} {span['extent']} bytes={n} SHA256={span['sha256']}\n{text.rstrip()}\n\n")
    final = "".join(output)
    path = OUT/"original-disassembly.txt"
    if compare:
        C.require(path.read_text(encoding="utf-8") == final, "saved disassembly differs", "identity")
    else:
        path.write_text(final, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT/"analysis/simpsons.pe")
    parser.add_argument("--check", action="store_true", help="reproduce complete JSON and checks; write nothing")
    parser.add_argument("--disassemble", action="store_true", help="write/compare bounded original-word-checked CPU disassembly")
    args = parser.parse_args()
    data = args.image.read_bytes()
    result = catalog(data)
    checks = selftests(data, result)
    result["self_tests"] = checks
    text = json.dumps(result, indent=2, sort_keys=True)+"\n"
    if args.check:
        C.require(RESULT.read_text(encoding="utf-8") == text, "saved catalog differs", "identity")
        C.require(json.loads((OUT/"checks.json").read_text(encoding="utf-8")) == checks, "saved checks differ", "identity")
    else:
        OUT.mkdir(parents=True, exist_ok=True)
        RESULT.write_text(text, encoding="utf-8")
        (OUT/"checks.json").write_text(json.dumps(checks, indent=2)+"\n", encoding="utf-8")
    if args.disassemble:
        disassemble(data, args.image, result, args.check)
    print(json.dumps(dict(summary=result["summary"], offline_test_cases=checks["cases"], cpu_words=result["cpu_evidence"]["word_count"]), sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (C.CatalogError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
