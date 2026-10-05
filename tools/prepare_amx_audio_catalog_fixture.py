#!/usr/bin/env python3
"""Extract one exact original AMX payload for the native catalog admission test."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

from extract_resource import select_payload


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    args = parser.parse_args(argv)
    project = Path(__file__).resolve().parents[1]
    root = project / "Simpsons Game, The (USA)"
    payload, provenance = select_payload(
        root / "loc/loc_global.str", 1, "char_poles_and_ladders.amx", root)
    expected = "5243a51f37376f5b1bb8969db761b2a60982a55d792fb145a439ac46763b9412"
    if (hashlib.sha256(payload).hexdigest() != expected or
            provenance["resource"]["payload_sha256"] != expected):
        raise ValueError("Original AMX payload changed")
    output = project / "build/amx-audio-catalog-fixture/char_poles_and_ladders.amx"
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != payload:
        output.write_bytes(payload)
    return subprocess.run([str(args.test.resolve(strict=True)),
                           str(args.catalog.resolve(strict=True)), str(output)],
                          check=False, timeout=60).returncode


if __name__ == "__main__":
    sys.exit(main())
