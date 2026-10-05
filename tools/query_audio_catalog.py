#!/usr/bin/env python3
"""Search the original game's streamed, resident, AMX, and movie audio catalogs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def rows(streamed: dict, resident: dict, amx: dict):
    for stream in streamed["stream_inventory"]:
        yield {
            "kind": "stream",
            "source": stream["path"],
            "ordinal": stream["ordinal"],
            "offset": stream["header_offset"],
            "channels": stream["channels"],
            "rate": stream["sample_rate"],
            "samples": stream["samples"],
            "loop": stream["loop"],
        }
    for bank in resident["banks"]:
        for ordinal, cue in enumerate(bank["cues"]):
            yield {
                "kind": "resident",
                "source": f'{bank["container_path"]}::{bank["name"]}',
                "ordinal": ordinal,
                "offset": cue["header_offset"],
                "channels": cue["channels"],
                "rate": cue["playback_rate"],
                "samples": cue["frames"],
                "loop": cue["loop"],
            }
    for payload in amx["payloads"]:
        for occurrence in payload["occurrences"]:
            for cue in payload["cues"]:
                yield {
                    "kind": "amx",
                    "source": f'{occurrence["container_path"]}::{occurrence["name"]}',
                    "asset_source": occurrence["source_path"],
                    "ordinal": cue["ordinal"],
                    "offset": cue["header_offset"],
                    "channels": cue["channels"],
                    "rate": cue["playback_rate"],
                    "samples": cue["frames"],
                    "loop": cue["loop"],
                }
    for movie in streamed["embedded_and_movie_audio"]["movie_audio"]:
        yield {
            "kind": "movie",
            "source": movie["path"],
            "ordinal": 0,
            "offset": None,
            "channels": movie["channels"],
            "rate": None,
            "samples": movie["audio_samples"],
            "loop": False,
        }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query", nargs="?", default="", help="case-insensitive path, bank, or kind substring")
    parser.add_argument("--limit", type=int, default=40, help="maximum rows to print (default: 40)")
    parser.add_argument("--json", action="store_true", help="emit JSON lines instead of a compact text table")
    args = parser.parse_args()
    if args.limit < 1:
        parser.error("--limit must be positive")
    streamed = json.loads((ROOT / "analysis/audio_catalog_summary.json").read_text(encoding="utf-8"))
    resident = json.loads((ROOT / "analysis/resident_audio_catalog.json").read_text(encoding="utf-8"))
    amx = json.loads((ROOT / "analysis/amx_audio_catalog.json").read_text(encoding="utf-8"))
    needle = args.query.casefold()
    shown = total = 0
    for row in rows(streamed, resident, amx):
        if (needle not in row["source"].casefold() and needle not in row["kind"]
                and needle not in row.get("asset_source", "").casefold()):
            continue
        total += 1
        if shown >= args.limit:
            continue
        if args.json:
            print(json.dumps(row, sort_keys=True))
        else:
            duration = f'{row["samples"] / row["rate"]:.2f}s' if row["rate"] else "?"
            print(f'{row["kind"]:8} {row["channels"]}ch {duration:>10} '
                  f'#{row["ordinal"]} @{row["offset"]} '
                  f'{"loop " if row["loop"] else ""}{row["source"]}')
        shown += 1
    print(f"{total} matching audio entries; showing {shown}",
          file=sys.stderr if args.json else sys.stdout)


if __name__ == "__main__":
    main()
