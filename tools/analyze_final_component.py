from pathlib import Path
import argparse

import cv2
import numpy as np
from PIL import Image


def main():
    p = argparse.ArgumentParser()
    p.add_argument("captures", type=Path)
    p.add_argument("frame", type=Path)
    p.add_argument("--area", type=int, default=9215)
    a = p.parse_args()
    if a.area <= 0:
        p.error("--area must be positive")
    if not a.frame.is_file():
        raise SystemExit(f"frame not found: {a.frame}")
    if not a.captures.is_dir():
        raise SystemExit(f"captures directory not found: {a.captures}")

    w, h = 1280, 720
    with Image.open(a.frame) as im:
        final = np.asarray(im.convert("RGB"))
    near_black = np.max(final, axis=2) < 25
    _, labels = cv2.connectedComponents(near_black.astype(np.uint8), connectivity=8)
    component = None
    for label in range(1, int(labels.max()) + 1):
        mask = labels == label
        if int(mask.sum()) == a.area:
            component = mask
            break
    if component is None:
        raise SystemExit(f"component area {a.area} not found")

    ys, xs = np.nonzero(component)
    print(f"component={int(component.sum())} bbox={int(xs.min())},{int(ys.min())}..{int(xs.max())},{int(ys.max())}")
    ever = np.zeros((h, w), dtype=bool)
    hits = []
    for meta in sorted(a.captures.glob("recording-*.json")):
        parts = meta.stem.split("-")
        if len(parts) < 2 or not parts[1]:
            continue
        payload = parts[1]
        before_path = a.captures / f"recording-{payload}-before.rgb10a2"
        after_path = a.captures / f"recording-{payload}-after.rgb10a2"
        if not before_path.exists() or not after_path.exists():
            continue

        def black(path: Path):
            try:
                words = np.fromfile(path, dtype="<u4").reshape(h, w)
            except (OSError, ValueError):
                return np.zeros((h, w), dtype=bool)
            rgb = np.stack((words & 1023, (words >> 10) & 1023, (words >> 20) & 1023), axis=2)
            return np.max(rgb, axis=2) < 100

        painted = black(before_path) & ~black(after_path)
        count = int((painted & component).sum())
        if count:
            hits.append((count, payload))
        ever |= painted

    print("hits=" + " ".join(f"{payload}:{count}" for count, payload in sorted(hits, reverse=True)))
    covered = int((ever & component).sum())
    print(f"ever_painted={covered} never_painted={int(component.sum()) - covered}")


if __name__ == "__main__":
    main()
