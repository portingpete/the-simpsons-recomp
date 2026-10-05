"""Measure contiguous accepted presentation intervals between menu capture boundaries."""
import argparse
import csv
import json
from pathlib import Path
import statistics

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('boot',type=int)
p.add_argument('phase',choices=('title','saved','saved-steady'))
def main():
    a=p.parse_args()
    if a.boot < 0 or a.boot > 100000:
        p.error('boot number out of range')
    root=Path(__file__).resolve().parents[1]
    captures=root/f'build/captures/native-loading-{a.boot}'
    anchor_dir=captures.with_name(captures.name+('-after-profile' if a.phase=='title' else '-saved-games'))
    anchors=[path.with_suffix('.json') for path in anchor_dir.glob('native-frame-*.rgb10a2')]
    if len(anchors)!=1:raise ValueError('Expected exactly one explicit anchor capture')
    try:
        anchor=json.loads(anchors[0].read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ValueError(f'Unreadable anchor capture: {exc}')
    try:
        all_captures=[json.loads(path.with_suffix('.json').read_text(encoding='utf-8')) for path in captures.glob('native-frame-*.rgb10a2')]
    except (OSError, ValueError) as exc:
        raise ValueError(f'Unreadable menu capture: {exc}')
    if not anchor.get('display_accepted'):raise ValueError('Anchor was not visibly presented')
    try:
        anchor_presentation=int(anchor['presentation'])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f'Invalid anchor presentation: {exc}')
    first=anchor_presentation+21
    if a.phase=='title':
        following=[meta for meta in all_captures if isinstance(meta.get('presentation'), int) and meta['presentation']>anchor_presentation]
        if not following:raise ValueError('No following capture to bound the title interval')
        end=min(following,key=lambda meta:meta['presentation'])
        if not end.get('display_accepted'):raise ValueError('End anchor was not visibly presented')
        if end.get('movie_draws')!=anchor.get('movie_draws'):raise ValueError('Title interval includes movie playback; reject this sample')
        last=end['presentation']-21
    else:
        if a.phase=='saved-steady':first+=240
        last=first+(1199 if a.phase=='saved-steady' else 239)
    if last<first:raise ValueError('No clean measurement interval between captures')
    if any(isinstance(meta.get('presentation'), int) and first-20<=meta['presentation']<=last+20 for meta in all_captures):
        raise ValueError('A renderer capture overlaps the measurement exclusion margin')
    with (root/f'build/boot-{a.boot}-frames.csv').open(newline='',encoding='utf-8') as f:
        reader=csv.DictReader(f)
        rows=[row for row in reader if row.get('presentation') and
              row['presentation'].lstrip('-').isdigit() and
              first<=int(row['presentation'])<=last]
    if [int(row['presentation']) for row in rows]!=list(range(first,last+1)):
        raise ValueError('Timing interval is incomplete, duplicated, or not contiguous')
    if any(row.get('display_accepted')!='1' or not row.get('frame_ms') for row in rows):
        raise ValueError('Timing interval contains an unaccepted or incomplete frame')
    ms=[float(row['frame_ms']) for row in rows]
    if min(ms)<=0:raise ValueError('Timing interval contains a nonpositive duration')
    report=dict(boot=a.boot,phase=a.phase,first=first,last=last,count=len(rows),
                fps=1000/statistics.mean(ms),median_ms=statistics.median(ms),max_ms=max(ms),
                over_25_ms=sum(value>25 for value in ms),anchor_presentation=anchor_presentation)
    if a.phase=='title':
        count=end['presentation']-anchor_presentation
        if count<=0:raise ValueError('Invalid title interval length')
        report['end_capture_presentation']=end['presentation']
        report['capture_counter_rates']={key:(end[key]-anchor[key])/count for key in
            ('draws','im2d_draws','im2d_native_draw_calls','native_buffer_upload_calls','im2d_textured_draws') if key in end and key in anchor}
        for key in ('state_commit_attempts','state_empty_commits','state_scalar_entries','state_stage_entries'):
            if key in anchor and key in end:report['capture_counter_rates'][key]=(end[key]-anchor[key])/count
    output=root/f'build/native-process-sampling/boot-{a.boot}-{a.phase}-timing.json'
    with output.open('x',encoding='utf-8') as f:json.dump(report,f,indent=2)
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
