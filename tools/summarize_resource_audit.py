#!/usr/bin/env python3
"""Stream native encounters into exact provenance groups and cardinality stats.

The original logs remain the authority for every occurrence. This report
keeps the full first event per stable signature, count and log-line spans.
It neither erases pointers from grouping nor invents a normalized identity;
field cardinality exposes inputs that may need producer-side separation.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit

SIGNATURE = ('kind', 'asset', 'caller', 'parameters', 'ownership', 'mission', 'last_action')


def summarize(paths):
    groups, failures, rejected, references = {}, {}, [], []
    counts, assets, fields, reasons, event_types = Counter(), defaultdict(set), defaultdict(lambda: defaultdict(set)), Counter(), Counter()
    for path in paths:
        digest = hashlib.sha256();bytes_read = 0;records = 0
        with path.open('rb') as source:
            for line, raw in enumerate(source, 1):
                digest.update(raw);bytes_read += len(raw)
                events, errors = audit.parse_encounters(raw.decode('utf-8', errors='replace'))
                if errors:
                    rejected += [dict(log=str(path), line=line, error=e['error'], raw=e['raw']) for e in errors]
                    continue
                if not events:
                    continue
                event = events[0];event['log_line'] = line;event['log_path'] = str(path);records += 1
                event_types[event['event']] += 1
                kind = event['kind'];counts[kind] += 1;assets[kind].add(event['asset'])
                signature = {key: event[key] for key in SIGNATURE}
                key = audit.sha(json.dumps(signature, sort_keys=True, separators=(',', ':')).encode())
                group = groups.setdefault(key, dict(signature=signature, first=event, count=0,
                    events=Counter(), log_line_spans={}))
                group['count'] += 1;group['events'][event['event']] += 1
                span = group['log_line_spans'].setdefault(str(path), dict(first=line, last=line, count=0))
                span['last'] = line;span['count'] += 1
                if event['event'] == 'failure':
                    failure = failures.setdefault(key, dict(signature=signature, first=event, count=0, reasons=Counter()))
                    failure['count'] += 1
                    reason = event.get('reason', 'unreported')
                    failure['reasons'][reason] += 1;reasons[reason] += 1
                for section in ('parameters', 'ownership'):
                    for field, value in audit.key_values(event[section]).items():
                        fields[kind][section + '.' + field].add(str(value))
        references.append(dict(path=str(path), sha256_prefix=digest.hexdigest(), bytes_read=bytes_read, records=records,
                               qualification='Digest covers the bytes read; an active log may append later'))
    kinds = {}
    for kind, count in counts.most_common():
        candidates = sorted(((len(values), field, sorted(values)[:3]) for field, values in fields[kind].items()), reverse=True)
        kinds[kind] = dict(events=count, unique_assets=len(assets[kind]),
            unique_signatures=sum(g['signature']['kind'] == kind for g in groups.values()),
            field_cardinality=[dict(field=field, distinct=count, examples=examples) for count, field, examples in candidates])
    return dict(schema=1, inputs=references,
        summary=dict(events=sum(counts.values()), signature_groups=len(groups), failure_groups=len(failures),
                     failed_occurrences=sum(f['count'] for f in failures.values()), rejected_lines=len(rejected),
                     terminal_context_occurrences=sum(reasons[r] for r in ('Native window closed', 'Native runtime shutdown'))),
        event_types=dict(event_types),
        failure_reasons=dict(reasons),
        kinds=kinds, groups=groups, failures=failures, rejected=rejected,
        limits=['Grouping retains all stable raw asset/caller/parameters/ownership/mission/action fields.',
                'Instance addresses/generations and scene context remain in first receipts and the original logs.',
                'High field cardinality is diagnostic evidence, not proof that a field may safely be removed.',
                'Window-close/shutdown failures retain last thread context; they do not prove an unsupported asset admission.',
                'Only complete source logs establish a complete run; active logs produce a bounded snapshot.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, action='append', default=[])
    parser.add_argument('--run-directory', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/restrictive-check-audit/encounter-groups.json')
    args = parser.parse_args()
    paths = [p.resolve() for p in args.log]
    if args.run_directory:
        paths += sorted(args.run_directory.resolve().glob('*/resources.jsonl'))
    audit.require(paths and len(paths) == len(set(paths)), 'Require distinct native resource logs')
    report = summarize(paths)
    output = args.output.resolve()
    audit.require(output.is_relative_to(ROOT / 'build') and output not in paths, 'Invalid encounter summary output')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report['summary'], sort_keys=True))
    print(json.dumps({key: {k: v for k, v in value.items() if k != 'field_cardinality'} for key, value in report['kinds'].items()}))
    print('Report:', output)
    if report['rejected']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
