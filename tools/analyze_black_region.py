from pathlib import Path
import argparse

import cv2
import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument("--threshold", type=int, default=25)
    args = parser.parse_args()
    if not 0 < args.threshold <= 255:
        parser.error("--threshold must be between 1 and 255")

    for image_path in args.images:
        if not image_path.is_file():
            raise SystemExit(f"image not found: {image_path}")
        with Image.open(image_path) as im:
            pixels = np.asarray(im.convert("RGB"))
        mask = (np.max(pixels, axis=2) < args.threshold).astype(np.uint8)
        count, _, stats, _ = cv2.connectedComponentsWithStats(mask, 8)
        components = []
        for index in range(1, count):
            x, y, width, height, area = (int(v) for v in stats[index])
            if area >= 100:
                components.append((area, x, y, x + width - 1, y + height - 1))

        h, w = mask.shape
        upper_right = int(mask[: h // 2, w // 2 :].sum())
        print(image_path)
        print(f"near_black={int(mask.sum())} upper_right={upper_right}")
        for component in sorted(components, reverse=True)[:30]:
            print(component)


if __name__ == "__main__":
    main()
