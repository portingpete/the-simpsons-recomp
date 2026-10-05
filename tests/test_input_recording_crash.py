"""Check the actual JSONL survives process death without recorder cleanup."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='simpsons-input-crash-') as directory:
    result = subprocess.run([sys.argv[1], directory], capture_output=True, timeout=20)
    assert result.returncode == 23, result.stderr.decode(errors='replace')
    paths = list(Path(directory).glob('inputs-*.jsonl'))
    assert len(paths) == 1, paths
    records = [json.loads(line) for line in paths[0].read_text(encoding="utf-8").splitlines()]
    assert records[0]['type'] == 'header' and records[0]['version'] == 1
    assert len(records) == 4, records
    first, second, disconnected = records[1:]
    assert first['buttons'] == 0x9000 and first['packet'] == 77
    assert [first[key] for key in ('lt', 'rt', 'lx', 'ly', 'rx', 'ry')] == [1, 255, -32768, 32767, -123, 456]
    assert first['seq'] == 0 and second['seq'] == 1 and disconnected['seq'] == 2
    assert first['t_us'] <= second['t_us'] <= disconnected['t_us']
    assert second['packet'] == first['packet'] and second['consumer'] == 'game'
    assert disconnected['slot'] == 3 and disconnected['status'] == 1167
    assert all(disconnected[key] == 0 for key in ('packet', 'buttons', 'lt', 'rt', 'lx', 'ly', 'rx', 'ry'))
    print('PASS: valid JSONL, full analog/button state, repeated polls, disconnects, monotonic timing; survives abrupt process death.')
