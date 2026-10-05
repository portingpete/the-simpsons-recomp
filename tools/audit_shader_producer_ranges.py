#!/usr/bin/env python3
"""Pin original shader selectors and separate caller scope from catalog support.

No native activation or draw executes here. Whole original caller spans,
instruction words and shader/constant-map qualification are verified against
the user's unchanged original image. Public alpha reachability is kept
distinct from the currently ported Burp route and from complete lifetimes.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_effect_admission as effects
import audit_mission_asset_support as audit
import analyze_mono_shader as mono

SPANS = (
    (0x8273A878, 0xF0, '3849c12b0f0ae1ed74c10b9b03725e441257b8f33dba399ef4f52c5da33c0522', 'mono_world_callback'),
    (0x827400F8, 0x1F8, '2cb10edca77889fe0620370b1249f08777bff13b40d6385ef5ebfb7d611efa8e', 'immediate_packet_loop'),
    (0x82740420, 0x260, '904be6cc4701d6df6fa8785590dec7e523c638fb1b8b8fdce90a4d67f1f1f7ad', 'recording_packet_build'),
    (0x82740680, 0x56C, 'be51c268553e3962c9c7f32fc53a358053de4b9c77cd9d28aed5b27f4b7463b4', 'whole_scene_dispatch'),
    (0x8273B4D0, 0x30, '5a398af4e130a1e062bc2010d4cbf880bdb27b7fdeb6f80b0ef96dea5bcf4495', 'public_packet_wrapper'),
    (0x826F3258, 0xC8, '17d48e91d72addbff4de7f575f3f9700a0c103fbb43f89111f870dfad5acdb21', 'shared_pool_usage_query'),
    (0x82701448, 0x1EC, '629da0eae626e35a340da1490b5e97a6372a6b261a2cddd5abe6ab40aa8d0e8e', 'deferred_static_material_mesh_loop'),
    (0x823FB3A8, 0x4C, '8b8067c3ba04bbcc14e2a7da0cebf6d983d337ddc8c34ad80e3a273335b512fc', 'registered_atomic_plugin_destructor_walk'),
    (0x827374B0, 0x50, '00dfdd1c5f58f2a4611ab7d38254812eee79b6af8d5f5cb11eef51241346b371', 'recording_cache_plugin_retirement'),
    (0x826F3908, 0x80, '2033d71dc48fd25fbf72cafae6aaca4eda34f11d45d2355a78a86c3bd19f820b', 'recording_manager_destructor'),
    (0x8270A7A0, 0x3C0, 'd1ba68cdd3e2cfe65aa0b77ffb406215ccb3a2fa69aab88da18432a07d438786', 'combined_matrix_callback_direct_staging'),
    (0x826B54D0, 0x148, '988b9881d6f4d8c99243d364abf70c98f8b150415342d844abfc2d078ee00ac9', 'immediate_material_parameter_walk'),
    (0x826B2F20, 0xB4, '6afa10c931fd6089518248bde4486c86b4fe443fefacfb59b1811403bbbc15c7', 'private_shared_dirty_mask_filter'),
    (0x82C1E5D0, 0x118, 'f2ec04bb76a03079270628e9dc1e77a602f9037d28333b18b749a753dfd9a710', 'selected_shared_constant_dirty_upload_loop'),
    (0x826F4F58, 0x194, '0aa336386b592f02a9ea54b33b396de45181b0fd7e8d8a54694761294cec1dfb', 'recording_finish_preserves_history_and_counts_success'),
    (0x82737400, 0xAC, 'ad7cbbf910da2b884b338a697fabaee3df9b6b9f3e0943606b7c474370f8677a', 'cached_payload_unlink_and_pool_return'),
    (0x826F4BE8, 0x88, '22ea5d74194faf1fb4e8319075323cee9c14027b235f4ae3ab63182f9e0a43c9', 'completed_payload_accounting_and_lru_unlink'),
    (0x826FF6D8, 0x24, 'a6f3eb96edd63a90ac7d4069c593b72585364b915d86d350e6ef3bfefeed09bf', 'nonzero_original_mesh_context_publication'),
)
INSTRUCTIONS = (
    (0x8273A894, 0x7CFF3B78, 'mr r31,r7: preserve incoming world-callback flag'),
    (0x8273A8A0, 0x57EA063E, 'clrlwi r10,r31,24: predicate uses only the low byte'),
    (0x8273A8A4, 0x2B0A0000, 'cmplwi cr6,r10,0'),
    (0x8273A928, 0x419A000C, 'beq cr6,8273A934: low byte zero selects opaque'),
    (0x8273A92C, 0x809E00A8, 'lwz r4,168(r30): nonzero low byte selects typed +A8 alpha handle'),
    (0x8273A930, 0x48000008, 'b 8273A938'),
    (0x8273A934, 0x809E00AC, 'lwz r4,172(r30): low byte zero selects typed +AC opaque handle'),
    (0x8273A938, 0x4BF7B741, 'bl 826B6078: LR8273A93C preserves original selected handle'),
    (0x82740108, 0x7C9E2378, 'mr r30,r4: immediate caller preserves incoming flag'),
    (0x82740110, 0x7FC7F378, 'mr r7,r30: immediate caller supplies original flag'),
    (0x82740114, 0x807F0018, 'lwz r3,24(r31): immediate caller uses packet +18 typed owner'),
    (0x82740130, 0x4E800421, 'bctrl: immediate world callback returns to82740134'),
    (0x82740430, 0x7C9A2378, 'mr r26,r4: recording caller preserves incoming flag'),
    (0x82740460, 0x7F47D378, 'mr r7,r26: recording caller supplies preserved incoming flag'),
    (0x82740478, 0x4E800421, 'bctrl: recording world callback returns to8274047C'),
    (0x82740B60, 0x4E800421, 'bctrl: current Burp world callback returns to82740B64'),
    (0x827406BC, 0x808B00AC, 'lwz r4,172(r11): the current original Z-prepass route always selects +AC'),
    (0x827406C4, 0x4BF759B5, 'bl 826B6078: Z-prepass returns to827406C8'),
    (0x8273B4DC, 0x480051A5, 'bl 82740680: public wrapper executes the whole dispatcher'),
    (0x8274090C, 0x9BDF000C, 'stb r30,12(r31): computed alpha clears the actual packet recording-eligible byte'),
    (0x82740918, 0x5556F7FE, 'metadata bit2 supplies the original recording r6 Boolean'),
    (0x82740A18, 0x896B0BE8, 'lbz original recording enable at82CF0BE8'),
    (0x82740A54, 0x7EE4BB78, 'recording r4 is the actual computed r23, not a forged callback input'),
    (0x82740A5C, 0x4BFFF9C5, 'the whole dispatcher calls the genuine recording builder82740420'),
    (0x8270AB14, 0x386BC0D0, 'combined callback destination is the actual82D6C0D0 staging bank'),
    (0x8270AB44, 0x484695CD, 'combined matrix callback tail copies directly through82B74110'),
    (0x826B5538, 0x419A00CC, 'an empty material bypasses all parameter setters'),
    (0x826B5608, 0x4BFFD919, 'immediate material tail enters original dirty filter826B2F20'),
    (0x826B2FD0, 0x4856ABD0, 'dirty filter tail enters original selective commit82C1DBA0'),
    (0x82C1E5EC, 0x7D25882A, 'shared dirty bitmap is read from the original shared pool owner'),
    (0x82C1E5FC, 0x7D275038, 'shared constants are selected by dirty bitmap AND selected-pass usage'),
    (0x826F501C, 0x914B0050, 'successful recording increments owner+50 independently of retained payload count'),
    (0x826F4C00, 0x9163004C, 'payload deletion updates owner+4C byte accounting without clearing owner+50 or history'),
    (0x8273743C, 0x4BD0A2CD, 'cached destructor deletes the actual retained SDK payload'),
    (0x82737494, 0x4BFBD755, 'cached destructor unlinks completed payload LRU/accounting'),
    (0x827374A0, 0x4BFBC511, 'cached destructor returns the actual CPU record to its original pool'),
    (0x826F394C, 0x480571C5, 'manager destructor retires its pool without clearing retained last-record history'),
    (0x8274069C, 0x4BFBF03D, 'whole scene dispatcher invokes the original mesh-context setter outside a recording session'),
    (0x826FF6F0, 0x906B3028, 'nonzero original setter publishes its actual context into82D63028 without a paired restoration'),
)


def pin_producers(image):
    audit.require(len(image) == 15466496 and audit.sha(image) == effects.IMAGE_SHA,
                  'Original image identity changed')
    take = lambda address, size: image[address - effects.BASE:address - effects.BASE + size]
    for address, size, digest, name in SPANS:
        audit.require(audit.sha(take(address, size)) == digest, 'Original producer span changed: ' + name)
    for address, word, text in INSTRUCTIONS:
        audit.require(struct.unpack('>I', take(address, 4))[0] == word,
                      'Original producer instruction changed: ' + text)
    # This literal belongs to the same hashed whole dispatcher. It prevents a
    # test from changing the Burp route's zero into a fabricated alpha input.
    audit.require(struct.unpack('>I', take(0x82740B44, 4))[0] == 0x38E00000,
                  'Original Burp callback literal flag changed')
    return dict(spans=[dict(address=f'{a:08X}', bytes=n, sha256=h, meaning=name) for a,n,h,name in SPANS],
                instructions=[dict(address=f'{a:08X}', word=f'{w:08X}', meaning=text) for a,w,text in INSTRUCTIONS])


def inspect(root=ROOT, effect_report=None, texture_report=None):
    image = (root / 'analysis/simpsons.pe').read_bytes()
    producers = pin_producers(image)
    shader, _ = mono.inspect(image)
    engine = root / 'runtime/engine_effects.cpp'
    native = engine.read_text()
    recording_engine = root / 'runtime/engine_recording.cpp'
    recording_native = recording_engine.read_text()
    destroy = recording_native.index('void EngineRecordingOwners::destroyBegin(')
    destroy_end = recording_native.index('\nvoid EngineRecordingOwners::destroyCommit(', destroy)
    zero_history_guard = 's.emptyFields(r.owner,base)' in recording_native[destroy:destroy_end]
    retired_history_guard = ('s.retiredGraph(base)' in recording_native[destroy:destroy_end] and
        'void retiredGraph(uint8_t* base) const' in recording_native and
        'session||replay||touch||touched||reset||quota||pluginDeletion||deletion' in recording_native and
        '!receiptCache.empty()||!nodes.empty()||!lru.empty()||freeOrder.size()!=2000' in recording_native and
        'graph(base); // Includes owner+50 == successCount and zero LRU/bytes.' in recording_native and
        'idleHistory(base);' in recording_native)
    audit.require(zero_history_guard or retired_history_guard,
                  'Reinspect changed original recording-manager retirement admission')
    first = native.index('void EngineEffects::beginMono(')
    last = native.index('\nvoid EngineEffects::beginZPrepass(', first)
    guard = native[first:last]
    restricted = 'c.r5.u32==0x0003FFFC' in guard and '!c.r31.u32' in guard and 'passes.front()' in guard
    continuation = ('c.r31.u32&255' in guard and 'c.r5.u32==technique' in guard and 'passes[alpha?1u:0u]' in guard and
                    'void EngineEffects::monoImmediateOperation(' in native and
                    'void EngineEffects::monoImmediateMeshOperation(' in native and
                    'void EngineEffects::monoImmediateTextureOperation(' in native)
    audit.require(restricted or continuation,
                  'Reinspect changed mono admission before retaining this restriction finding')
    finding = dict(id='mono_public_alpha', status='original_valid_unported_public_path' if restricted else 'native_source_continuation_pending_test',
        source='0x8211F480', valid_flag='Full incoming DWORD is accepted by the original callback; selection depends on r7 & 255, not a0/1 whitelist',
        selections=[dict(predicate='(r7 & 255)==0', typed_offset='0xAC', technique_handle='0x0003FFFC',
                         pass_handle='0x0003FFFE', vertex='0x82120C04', pixel='0x82122BD4'),
                    dict(predicate='(r7 & 255)!=0', typed_offset='0xA8', technique_handle='0x0007FFFC',
                         pass_handle='0x0007FFFE', vertex='0x82121BE8', pixel='0x82122D38')],
        representative_inputs=[0,1,255,256,257,0xffffffff],
        original_callers=[dict(address='827400F8', callback_lr='82740134', input='r4 ->r30 ->r7; packet +18 typed owner'),
                          dict(address='82740420', callback_lr='8274047C', input='r4 ->r26 ->r7; packet +18 typed owner')],
        current_route=dict(address='8273B4D0', dispatcher='82740680', mode='82D6CCA8==2',
                           callback_lr='82740B64', input='literal r7=0; packet +1C typed owner', alpha_reachable=False),
        qualified_whole_public_producer=dict(mode='82D6CCA8==0',packet_typed_offset='0x18',
            immediate_flags=[0,1],recording_flags=[0],recording_metadata=[0,4],
            recording_enable='actual byte82CF0BE8==1',recording_eligible='actual packet byte+0C==1',
            recording_original_caller='82740A60',recording_bit2='metadata+8 bit2 ->r6; valid cases0 and1',
            qualification='For the qualified static no-palette producer, alpha metadata bits0/1 cause the original dispatcher to clear recording eligibility and choose immediate use. The callback full-DWORD domain does not prove alpha recording is produced by this caller.'),
        native_rejection=dict(file='runtime/engine_effects.cpp', line=native[:first].count('\n')+1,
            expression='c.r5.u32==0x0003FFFC; !c.r31.u32; passes.front()' if restricted else 'Immediate and recording source continuations prepared; independent original immediate alpha/opaque and recording default/bit2 complete-lifetime native receipts are required',
            qualification='Public original alpha is valid; Burp supplies zero. Native source changes alone do not establish original immediate or recording caller lifetimes'),
        shader_qualification=shader,
        shared_pool_usage_contract=dict(original_query='826F3258', immediate_call='82740044', recorded_call='826F3ABC',
            handles=['001C000D', '0020000F', '00240011'], namespaces='handle & 1', leaf='(handle >> 1) & 0x1FFFF',
            bitmap='eight64-bit selected-context bitmap words, each bounded by serialized owner',
            descriptors='original usage-only query does not require a local parameter descriptor',
            observed_frontier='Both native original immediate variants rejected reflection descriptor handle after begin/callback commit',
            repair_status='bounded usage-only source continuation; independent native rerun required'),
        material_constant_inheritance=dict(callback='8270A7A0', actual_staging='82D6C0D0',
            material='826B54D0', dirty_filter='826B2F20', selective_commit='82C1DBA0',
            producer_contract='The combined callback stages and uploads its matrix directly. An empty material has no setter; selective dirty/usage commit preserves inherited uploaded matrix rows rather than assigning the zero shared-pool default.',
            observed_frontier='Both original immediate variants reached native draw, then failed the independent white-export pixel oracle; the later native material commit replaced verified identity staging with zero shared defaults',
            repair_status='Immediate material retains verified completed staging; independent native rerun required'),
        recording_lifetime_contract=dict(original_mesh='82701448', material='826B5618', replay='827402F0',
            plugin_walker='823FB3A8', registered_atomic_registry='82CD1678', cache_destructor='827374B0',
            manager_destructor='826F3908',
            success_count='Original finish826F501C increments owner+50. Completed-payload deletion826F4BE8 updates byte accounting and LRU links but does not reset that count.',
            history='Original manager destructor does not clear the last recording fields54..78; stable dead history is distinct from retained payload ownership',
            idle_context_publication=dict(producer='8274069C ->826FF6D8', store='826FF6F0 ->82D63028',
                native_observation='826FF6F4 after the actual original store',
                contract='The original dispatcher publishes its current device context before entering any recording path. A null mesh alias observed at manager construction can legitimately become the live device alias. The qualified native poststore observation updates host expectations only; foreign/reverted aliases, invalid caller/frame and active recording operations remain guarded.',
                source_prepared='void EngineRecordingOwners::observeIdleContextPublication(' in recording_native,
                tested=False),
            retirement_guard=dict(file='runtime/engine_recording.cpp', line=recording_native[:destroy].count('\n')+1,
                expression='s.emptyFields(r.owner,base)' if zero_history_guard else 's.retiredGraph(base): complete empty graph/accounting and unchanged saved history',
                status='original_retained_history_requires_native_retirement_repro' if zero_history_guard else 'native_empty_graph_saved_history_pending_lifetime_receipt',
                required_scope='No retained payload or CPU node/LRU ownership, all2000 original pool slots returned, no active record/replay/delete/reset operation, unchanged saved history and original success count, exact registration/context ownership'),
            repair_status='Deferred mono source and genuine default/bit2 complete-lifetime fixture prepared; no native lifetime credit yet'),
        next_case='Execute whole original8273B4D0/82740680 immediate alpha/opaque and independent genuine recording default/bit2 producers with real registered mono owners, pixels/cache reuse/reset, original retirement and malformed owner/handle/cache rejection; never change the Burp literal flag or manually invoke a fabricated recording world callback',
        draw_lifecycle_tested=False)
    report = dict(schema=1, authority=dict(image=dict(path='analysis/simpsons.pe',sha256=effects.IMAGE_SHA),
        engine=dict(path='runtime/engine_effects.cpp',sha256=audit.sha(engine.read_bytes())),
        recording_engine=dict(path='runtime/engine_recording.cpp',sha256=audit.sha(recording_engine.read_bytes())), original_producers=producers),
        findings=[finding, dict(id='zprepass_current_route', status='retained_original_caller_selection',
            source='0x821490E0', selected='typed +AC opaque handle', original_calls=['827406BC','827406C4'],
            qualification='The traced current route loads +AC unconditionally; alpha catalog presence does not prove this caller requests it')],
        limits=['Verified original instructions establish selection inputs, not completed native draw/lifecycle or gameplay encounters.',
                'Shader code/literal/constant-map equivalence and source continuation do not establish tested immediate or recording caller lifetimes.',
                'Original public low-byte semantics do not imply that every caller supplies every DWORD value.'])
    if effect_report:
        data, reference = audit.load(effect_report)
        missing = [p for p in data['passes'] if p['missing_native_artifacts']]
        report['authority']['effect_report'] = reference
        report['missing_artifact_groups'] = dict(sorted(Counter(p['effect'] for p in missing).items()))
        report['missing_artifact_queue'] = [dict(p, admission_validity='catalog_presence_only',
            next_case='Trace original selector, then run this pass independently through create/use/release and malformed input') for p in missing]
    if texture_report:
        data, reference = audit.load(texture_report)
        report['authority']['texture_report'] = reference
        report['texture_scope'] = dict(dictionaries=len(data['dictionaries']), occurrences=len(data['textures']),
            admitted=sum(t['admission']=='admitted_metadata' for t in data['textures']),
            rejected=sum(t['admission']!='admitted_metadata' for t in data['textures']),
            formats=dict(sorted(Counter(t['format'] for t in data['textures']).items())),
            qualification='Every dictionary runs independently through source metadata admission; these observed shipped ranges do not establish the original format/dimension producer maximum or all pixel/owner lifetimes')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--effect-report',type=Path)
    parser.add_argument('--texture-report',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'build/restrictive-check-audit/shader-producer-ranges.json')
    args=parser.parse_args()
    report=inspect(effect_report=args.effect_report,texture_report=args.texture_report)
    output=args.output.resolve()
    audit.require(output.is_relative_to(ROOT/'build') and output not in [p.resolve() for p in
        (args.effect_report,args.texture_report) if p], 'Invalid original producer report output')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(findings=len(report['findings']),missing_artifact_groups=report.get('missing_artifact_groups'),
                         texture_scope=report.get('texture_scope')),sort_keys=True))


if __name__=='__main__':
    main()
