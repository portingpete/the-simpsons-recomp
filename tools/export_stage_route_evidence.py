#!/usr/bin/env python3
"""Export authoritative native captures and bounded route/resource evidence.

Input delivery, original map startup, rendered frames and semantic gameplay
outcomes are independent. Only native-frame JSON with its exact raw renderer
readback is a capture; unrelated diagnostic recordings are never screenshots.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_mission_asset_support as audit

INPUT=re.compile(r'local command tap buttons=([0-9A-F]{4}) hold_ms=(\d+); left=\((-?\d+),(-?\d+)\) rt=(\d+); delivered')
FAMILIES=('screen','im2d','movie','edge','aa','edgeaa','shadowmesh','zprepass','postfilter','mono','rigid','skin','sky')
UNPROVED=['ability_hit','enemy_attack','pickup_consumed','destructible_destroyed','dialogue_audible',
          'environmental_effect_complete_lifetime','checkpoint_reload','death_and_respawn','later_cutscene_playback',
          'later_cutscene_skip','mission_exit']


def reference(path):
    return dict(path=str(path.resolve()),sha256=audit.sha(path.read_bytes()),bytes=path.stat().st_size)


def read_capture(path):
    audit.require(re.fullmatch(r'native-frame-\d+\.json',path.name) is not None,'Not a native frame metadata file')
    data=json.loads(path.read_bytes())
    width,height=data.get('width'),data.get('height')
    audit.require(type(width) is int and type(height) is int and 0<width<=16384 and 0<height<=16384,
                  'Invalid native frame extent')
    raw=path.with_suffix('.rgb10a2')
    audit.require(data.get('format')=='R10G10B10A2_UNORM_LE' and raw.is_file() and raw.stat().st_size==width*height*4,
                  'Native raw renderer readback is absent or differs from metadata')
    player=data.get('telemetry',{}).get('player',{})
    position=player.get('position')
    if not (player.get('available') is True and isinstance(position,list) and len(position)==3
            and all(type(v) in (int,float) for v in position)):
        position=None
    return dict(metadata=reference(path),pixels=reference(raw),width=width,height=height,
        renderer_front_completed=data.get('capture_source')=='completed_front_renderer_readback' and
            data.get('front_copy_completed') is True and data.get('display_accepted') is True,
        cumulative_draws={key:data.get(key) for key in FAMILIES if type(data.get(key)) is int},
        frame_scene_geometry=data.get('frame_scene_geometry'),player_position=position,
        player_source=player.get('source'),
        qualification='Original renderer readback and cumulative draw counters; no linked ability hit, target, objective, cue audibility or complete resource lifetime')


def export(summary_path,expected_stages=None):
    summary=json.loads(summary_path.read_bytes())
    audit.require(summary.get('schema')==1 and isinstance(summary.get('results'),list),'Invalid stage sweep summary')
    stages=[]
    for result in summary['results']:
        stage=result['stage'];run=Path(result['run']).resolve()
        audit.require(run.is_relative_to(summary_path.parent.resolve()),'Stage run escaped its sweep directory')
        log=run/'game.log';text=log.read_text(encoding='utf-8',errors='replace')
        receipts=[(int(m[1],16),*map(int,m.groups()[1:])) for m in INPUT.finditer(text)]
        available=Counter(receipts);actions=[]
        for item in result.get('actions',[]):
            receipt=tuple(item.get('receipt') or [])
            verified=item.get('delivered') is True and available[receipt]>0
            if verified:available[receipt]-=1
            actions.append(dict(name=item['action']['name'],action=item['action'],elapsed=item.get('elapsed'),
                reported_delivered=item.get('delivered') is True,verified_delivery=verified,
                receipt=list(receipt),semantic_outcome='unproved'))
        captures=[];capture_errors=[]
        for path in sorted((run/'captures').glob('native-frame-*.json')):
            if re.fullmatch(r'native-frame-\d+\.json',path.name) is None:
                continue
            try:captures.append(read_capture(path))
            except (ValueError,TypeError,KeyError,json.JSONDecodeError) as error:
                capture_errors.append(dict(path=str(path),error=str(error)))
        positions=[c['player_position'] for c in captures if c['player_position'] is not None]
        changed=len({tuple(p) for p in positions})>1
        resources=run/'resources.jsonl';events,errors=audit.parse_encounters(resources.read_text(encoding='utf-8',errors='replace'))
        substantive=[e for e in events if e['event']=='failure' and
            e.get('reason') not in ('Native window closed','Native runtime shutdown')]
        lifecycle=[e for e in events if e['event']=='lifecycle']
        launch=run/'launch.json'
        stages.append(dict(stage=stage,run=str(run),game_log=reference(log),launch=reference(launch),
            launch_metadata=json.loads(launch.read_bytes()),reported_success=result.get('success'),
            original_map_ready=('[STAGE AUDIT] original map initialized stage='+stage+' ') in text,
            reported_primary_unchanged=result.get('primary_unchanged'),actions=actions,
            delivered_actions=sum(a['verified_delivery'] for a in actions),native_input_receipts=len(receipts),
            captures=captures,capture_errors=capture_errors,observed_player_position_changed=changed,
            resources=dict(input=reference(resources),events=len(events),event_types=dict(Counter(e['event'] for e in events)),
                kinds=dict(Counter(e['kind'] for e in events)),substantive_failures=substantive,
                lifecycle_boundaries=lifecycle,rejected_lines=errors),
            movie_starts=result.get('movie_starts',[]),movie_retirements=result.get('movie_retirements',[]),
            semantic_outcomes_unproved=UNPROVED))
    names=[s['stage'] for s in stages]
    audit.require(len(names)==len(set(names)),'Duplicate stage evidence')
    missing=sorted(set(expected_stages or [])-set(names));unexpected=sorted(set(names)-set(expected_stages or names))
    return dict(schema=1,authority=dict(summary=reference(summary_path)),
        summary=dict(stages=len(stages),missing_stages=missing,unexpected_stages=unexpected,
            reported_successes=sum(s['reported_success'] is True for s in stages),
            original_map_ready=sum(s['original_map_ready'] for s in stages),
            stages_with_native_captures=sum(bool(s['captures']) for s in stages),
            native_captures=sum(len(s['captures']) for s in stages),
            verified_input_deliveries=sum(s['delivered_actions'] for s in stages),
            stages_with_position_change=sum(s['observed_player_position_changed'] for s in stages),
            substantive_failures=sum(len(s['resources']['substantive_failures']) for s in stages)),
        stages=stages,limits=['Historical launch hashes identify the executed build; rebuilding does not update old run coverage.',
            'Native input receipts prove command delivery only. Position changes and draws are independent observations.',
            'Map lifecycle records are retained independently and grant no asset encounter or rejection credit.',
            'Opening routes do not establish the listed gameplay interactions or transition lifetimes.',
            'Capture metadata is joined only to its exact full renderer raw readback, not diagnostic recording JSON.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    expected=re.findall(r'"([a-z_]+)"',(ROOT/'runtime/audit_stage.h').read_text())
    report=export(args.summary.resolve(),expected)
    output=args.output.resolve()
    audit.require(output.is_relative_to(ROOT/'build') and output!=args.summary.resolve(),'Invalid route evidence output')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report['summary'],sort_keys=True))
    if report['summary']['missing_stages'] or any(s['capture_errors'] or s['resources']['rejected_lines'] for s in report['stages']):
        raise SystemExit(1)


if __name__=='__main__':
    main()
