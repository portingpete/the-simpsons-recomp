"""Validate one completed front readback; never infer gameplay from input delivery.

This is a receipt check, not a claim of visual fidelity or complete emulation.
Scene counts are emitted by engine_driver.cpp for rigid, skin, and sky draws.
Cumulative menu/movie counters and alpha-only black pixels are insufficient.
"""
from __future__ import annotations

from array import array
import hashlib
from collections.abc import Mapping
import sys

WIDTH, HEIGHT = 1280, 720
SCENE_COUNTERS = ('rigid_mesh_draws', 'skin_mesh_draws', 'sky_mesh_draws')


class EvidenceError(ValueError):
    """The capture does not establish the first-presented-gameplay criterion."""


def _integer(meta: Mapping[str, object], key: str) -> int:
    value = meta.get(key)
    if type(value) is not int or value < 0:
        raise EvidenceError(f'Missing or invalid nonnegative integer: {key}')
    return value


def verify_gameplay_frame(data: bytes, meta: Mapping[str, object], *,
                          minimum_presentation: int,
                          allow_occluded: bool = False) -> dict[str, object]:
    """Return evidence only when all criteria hold, otherwise raise EvidenceError.

    The caller must establish that Continue Game was delivered and either the
    opening-movie skip completed or a fresh direct-resume route was observed.
    A route signal alone does not verify gameplay or character control.
    minimum_presentation is the verified main-menu presentation.
    The per-frame scene count prevents old draws from qualifying a later menu.
    allow_occluded accepts an occluded display only with a completed front
    readback; the returned display_accepted field retains the actual status.
    """
    if type(minimum_presentation) is not int or minimum_presentation < 0:
        raise EvidenceError('Invalid main-menu presentation boundary')
    if not isinstance(meta, Mapping):
        raise EvidenceError('Capture metadata must be an object')
    if _integer(meta, 'width') != WIDTH or _integer(meta, 'height') != HEIGHT:
        raise EvidenceError('Unexpected native frame dimensions')
    if meta.get('format') != 'R10G10B10A2_UNORM_LE' or len(data) != WIDTH * HEIGHT * 4:
        raise EvidenceError('Truncated or incorrectly formatted packed frame')
    if (meta.get('capture_source') != 'completed_front_renderer_readback' or
            meta.get('front_copy_completed') is not True):
        raise EvidenceError('No completed front-renderer readback')
    display_accepted = meta.get('display_accepted')
    if display_accepted is not True and not (allow_occluded is True and display_accepted is False):
        raise EvidenceError('Presentation was not accepted by the display')
    presentation = _integer(meta, 'presentation')
    if presentation <= minimum_presentation:
        raise EvidenceError('Capture is not newer than the verified main menu')
    counts = {name: _integer(meta, name) for name in SCENE_COUNTERS}
    scene = _integer(meta, 'scene_geometry_draws')
    frame_scene = _integer(meta, 'frame_scene_geometry_draws')
    if scene != sum(counts.values()) or scene > _integer(meta, 'draws'):
        raise EvidenceError('Scene geometry counters disagree with the draw receipt')
    if not 0 < frame_scene <= scene:
        raise EvidenceError('This presentation has no new scene geometry draws')
    words = array('I')
    if words.itemsize != 4:
        raise EvidenceError('Packed-frame verifier requires 32-bit unsigned words')
    words.frombytes(data)
    if sys.byteorder != 'little':
        words.byteswap()
    nonzero_rgb = sum(1 for word in words if word & 0x3FFFFFFF)
    if not nonzero_rgb:
        raise EvidenceError('Completed front pixels contain no nonzero RGB')
    return {
        'presentation': presentation,
        'display_accepted': display_accepted,
        'front_copy_completed': True,
        'scene_geometry_draws': scene,
        'frame_scene_geometry_draws': frame_scene,
        **counts,
        'nonzero_rgb_pixels': nonzero_rgb,
        'pixel_sha256': hashlib.sha256(data).hexdigest(),
    }
