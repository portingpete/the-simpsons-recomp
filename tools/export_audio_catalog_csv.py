#!/usr/bin/env python3
"""Export one browsable row per catalogued original audio entry."""

from __future__ import annotations

import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "analysis/audio_clips.csv"
FIELDS = (
    "kind", "source", "resource_entry_index", "ordinal", "header_offset", "channels",
    "playback_rate_hz", "samples", "duration_seconds", "loop",
    "block_count", "codec", "source_sha256",
)


def main() -> None:
    streamed = json.loads((ROOT / "analysis/audio_catalog_summary.json").read_text(encoding="utf-8"))
    resident = json.loads((ROOT / "analysis/resident_audio_catalog.json").read_text(encoding="utf-8"))
    amx = json.loads((ROOT / "analysis/amx_audio_catalog.json").read_text(encoding="utf-8"))
    counts = {"stream": 0, "resident": 0, "amx": 0, "movie": 0}
    with OUT.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        for stream in streamed["stream_inventory"]:
            rate = stream["sample_rate"]
            writer.writerow({
                "kind": "stream", "source": stream["path"], "ordinal": stream["ordinal"],
                "header_offset": stream["header_offset"], "channels": stream["channels"],
                "playback_rate_hz": rate, "samples": stream["samples"],
                "duration_seconds": f'{stream["samples"] / rate:.3f}',
                "loop": int(stream["loop"]), "block_count": stream["blocks"],
                "codec": "EA-XMA", "source_sha256": "",
            })
            counts["stream"] += 1
        for bank in resident["banks"]:
            source = f'{bank["container_path"]}::{bank["name"]}'
            for ordinal, cue in enumerate(bank["cues"]):
                rate = cue["playback_rate"]
                writer.writerow({
                    "kind": "resident", "source": source,
                    "resource_entry_index": bank["entry_index"], "ordinal": ordinal,
                    "header_offset": cue["header_offset"], "channels": cue["channels"],
                    "playback_rate_hz": rate, "samples": cue["frames"],
                    "duration_seconds": f'{cue["frames"] / rate:.3f}',
                    "loop": int(cue["loop"]), "block_count": len(cue["blocks"]),
                    "codec": "EA-XMA", "source_sha256": bank["payload_sha256"],
                })
                counts["resident"] += 1
        for payload in amx["payloads"]:
            for occurrence in payload["occurrences"]:
                source = f'{occurrence["container_path"]}::{occurrence["name"]}'
                for cue in payload["cues"]:
                    rate = cue["playback_rate"]
                    writer.writerow({
                        "kind": "amx", "source": source,
                        "resource_entry_index": occurrence["entry_index"],
                        "ordinal": cue["ordinal"],
                        "header_offset": cue["header_offset"], "channels": cue["channels"],
                        "playback_rate_hz": rate, "samples": cue["frames"],
                        "duration_seconds": f'{cue["frames"] / rate:.3f}',
                        "loop": int(cue["loop"]), "block_count": len(cue["blocks"]),
                        "codec": "EA-XMA", "source_sha256": payload["payload_sha256"],
                    })
                    counts["amx"] += 1
        for movie in streamed["embedded_and_movie_audio"]["movie_audio"]:
            writer.writerow({
                "kind": "movie", "source": movie["path"], "ordinal": 0,
                "header_offset": "", "channels": movie["channels"],
                "playback_rate_hz": "", "samples": movie["audio_samples"],
                "duration_seconds": "", "loop": 0, "block_count": movie["audio_packets"],
                "codec": movie["codec"], "source_sha256": movie["source_sha256"],
            })
            counts["movie"] += 1
    assert counts == {"stream": 9470, "resident": 45382, "amx": 1999, "movie": 42}, counts
    print(f"Wrote {OUT}: {sum(counts.values())} audio entries ({counts})")


if __name__ == "__main__":
    main()
