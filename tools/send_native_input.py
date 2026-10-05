"""Append one explicit controller tap to an enabled native game's command file."""
import argparse
import os
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--file', type=Path, required=True)
parser.add_argument('--hold', action='store_true', help='Hold for 250 ms after the game consumes the command')
parser.add_argument('button', choices=('START', 'A', 'B', 'BACK', 'UP', 'DOWN', 'LEFT', 'RIGHT'))
args = parser.parse_args()
# Never create or truncate a channel: the launch must opt into an existing file.
# Restrict to a regular file to avoid following directories/special paths.
resolved = args.file.resolve()
if not resolved.is_file() or resolved.is_symlink():
    parser.error('--file must identify an existing regular command file')
descriptor = os.open(resolved, os.O_WRONLY | os.O_APPEND | os.O_BINARY | getattr(os, 'O_NOFOLLOW', 0))
try:
    command = (args.button + ('_HOLD' if args.hold else '') + '\n').encode('ascii')
    if os.write(descriptor, command) != len(command):
        raise OSError('Incomplete controller command write')
finally:
    os.close(descriptor)
print(f'Queued {args.button}; confirm delivery in the game log.')
