#!/usr/bin/env python3
"""Extract one exact original SBK for the native resident catalog admission test."""

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
    source = root / "bargainbin/bargainbin.str"
    payload, provenance = select_payload(source, 2, "bargainbin.sbk", root)
    expected = "6a1430b0594ddb67f762b8e82963856250b1681d6c3dc246c5d4efc0f2867f77"
    if (hashlib.sha256(payload).hexdigest() != expected or
            provenance["resource"]["payload_sha256"] != expected):
        raise ValueError("Original Bargain Bin audio payload changed")
    output = project / "build/resident-audio-catalog-fixture/bargainbin.sbk"
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != payload:
        output.write_bytes(payload)
    return subprocess.run([str(args.test.resolve(strict=True)),
                           str(args.catalog.resolve(strict=True)), str(output)],
                          check=False, timeout=60).returncode


if __name__ == "__main__":
    sys.exit(main())
