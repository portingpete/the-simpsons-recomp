from pathlib import Path
import argparse

import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("payloads", nargs="+")
    parser.add_argument("--threshold", type=int, default=25)
    args = parser.parse_args()
    if not 1 <= args.threshold <= 255:
        parser.error("--threshold must be between 1 and 255")
    if not args.directory.is_dir():
        raise SystemExit(f"directory not found: {args.directory}")

    width = 1280
    height = 720
    regions = {
        "target": (950, 1190, 300, 535),
        "badbox": (969, 1172, 311, 522),
    }


    def load(path: Path):
        try:
            words = np.fromfile(path, dtype="<u4").reshape(height, width)
        except (OSError, ValueError) as exc:
            raise SystemExit(f"Cannot load {path}: {exc}")
        rgb = np.stack((words & 1023, (words >> 10) & 1023, (words >> 20) & 1023), axis=2)
        return words, rgb


    for payload in args.payloads:
        if "/" in payload or "\\" in payload or ".." in payload:
            raise SystemExit(f"Invalid payload name: {payload}")
        before_words, before_rgb = load(args.directory / f"recording-{payload}-before.rgb10a2")
        after_words, after_rgb = load(args.directory / f"recording-{payload}-after.rgb10a2")
        changed = before_words != after_words
        ys, xs = np.nonzero(changed)
        print(f"{payload} changed={int(changed.sum())}")
        if len(xs):
            print(f"bbox={int(xs.min())},{int(ys.min())}..{int(xs.max())},{int(ys.max())}")
        threshold10 = args.threshold * 4
        before_black = np.max(before_rgb, axis=2) < threshold10
        after_black = np.max(after_rgb, axis=2) < threshold10
        for name, (x0, x1, y0, y1) in regions.items():
            sl = np.s_[y0:y1 + 1, x0:x1 + 1]
            region_changed = changed[sl]
            from_black = region_changed & before_black[sl] & ~after_black[sl]
            to_black = region_changed & ~before_black[sl] & after_black[sl]
            print(
                f"{name} changed={int(region_changed.sum())} "
                f"from_black={int(from_black.sum())} to_black={int(to_black.sum())} "
                f"near_black={int(before_black[sl].sum())}->{int(after_black[sl].sum())}"
            )


if __name__ == "__main__":
    main()
