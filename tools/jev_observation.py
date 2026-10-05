"""Compact text observation from a qualified native gameplay capture.

The renderer's packed RGB10A2 bytes remain the source of truth. This module
samples a fixed 32 by 18 grid for a quick visual summary; the coarse symbols
cannot establish character control or visual fidelity on their own.
"""

from __future__ import annotations

import struct
from collections.abc import Mapping

from gameplay_evidence import EvidenceError, HEIGHT, WIDTH, verify_gameplay_frame


GRID_WIDTH = 32
GRID_HEIGHT = 18
CELL_SIZE = 40
SAMPLE_OFFSETS = (4, 12, 20, 28, 36)
LUMINANCE_SYMBOLS = " .:-=+*#%@"
FRAME_BYTES = WIDTH * HEIGHT * 4


def _rgb(word: int) -> tuple[int, int, int]:
    return word & 1023, (word >> 10) & 1023, (word >> 20) & 1023


def _luminance(r: int, g: int, b: int) -> int:
    return (2126 * r + 7152 * g + 722 * b + 5000) // 10000


def _color_symbol(r: int, g: int, b: int) -> str:
    maximum = max(r, g, b)
    if maximum < 64:
        return "."
    active = (r * 5 >= maximum * 3, g * 5 >= maximum * 3, b * 5 >= maximum * 3)
    symbol = {
        (True, False, False): "R",
        (False, True, False): "G",
        (False, False, True): "B",
        (True, True, False): "Y",
        (False, True, True): "C",
        (True, False, True): "M",
    }.get(active, "W")
    return symbol if maximum >= 512 else symbol.lower()


def observe_frame(
    data: bytes,
    meta: Mapping[str, object],
    *,
    minimum_presentation: int,
    previous_data: bytes | None = None,
    allow_occluded: bool = False,
) -> dict[str, object]:
    """Summarize a qualified gameplay capture and its change from a prior frame.

    ``minimum_presentation`` is the verified main-menu presentation. The
    previous frame must come from the caller's own qualified capture stream.
    ``frame_change_fraction`` is the mean absolute RGB code change across the
    sampled pixels, normalized to 0..1; it is ``None`` for the first frame.
    Alpha is ignored because the display ignores it.
    """
    evidence = verify_gameplay_frame(data, meta, minimum_presentation=minimum_presentation,
                                     allow_occluded=allow_occluded)
    if previous_data is not None and len(previous_data) != FRAME_BYTES:
        raise EvidenceError("Previous packed frame has an invalid size")

    color_rows: list[str] = []
    luminance_rows: list[str] = []
    total_difference = 0
    samples_per_cell = len(SAMPLE_OFFSETS) ** 2

    for cell_y in range(GRID_HEIGHT):
        colors: list[str] = []
        luminances: list[str] = []
        for cell_x in range(GRID_WIDTH):
            red = green = blue = 0
            for offset_y in SAMPLE_OFFSETS:
                y = cell_y * CELL_SIZE + offset_y
                for offset_x in SAMPLE_OFFSETS:
                    x = cell_x * CELL_SIZE + offset_x
                    byte_offset = (y * WIDTH + x) * 4
                    current = _rgb(struct.unpack_from("<I", data, byte_offset)[0])
                    red += current[0]
                    green += current[1]
                    blue += current[2]
                    if previous_data is not None:
                        prior = _rgb(struct.unpack_from("<I", previous_data, byte_offset)[0])
                        total_difference += sum(abs(a - b) for a, b in zip(current, prior))
            r, g, b = (red // samples_per_cell, green // samples_per_cell, blue // samples_per_cell)
            colors.append(_color_symbol(r, g, b))
            level = (_luminance(r, g, b) * (len(LUMINANCE_SYMBOLS) - 1) + 511) // 1023
            luminances.append(LUMINANCE_SYMBOLS[level])
        color_rows.append("".join(colors))
        luminance_rows.append("".join(luminances))

    frame_change = None
    if previous_data is not None:
        sample_count = GRID_WIDTH * GRID_HEIGHT * samples_per_cell
        frame_change = round(total_difference / (sample_count * 3 * 1023), 6)

    return {
        "presentation": evidence["presentation"],
        "display_accepted": evidence["display_accepted"],
        "pixel_sha256": evidence["pixel_sha256"],
        "frame_scene_geometry_draws": evidence["frame_scene_geometry_draws"],
        "grid_width": GRID_WIDTH,
        "grid_height": GRID_HEIGHT,
        "color_grid": color_rows,
        "luminance_grid": luminance_rows,
        "frame_change_fraction": frame_change,
    }
