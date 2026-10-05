"""Validate game-authored facts tied to one completed renderer capture.

Only fields that the native port marks available enter a Jev decision. An
unavailable or mismatched snapshot is diagnostic data, never inferred state.
"""
from __future__ import annotations

import math
from collections.abc import Mapping


def _number(value: object) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def _vector3(value: object) -> list[float] | None:
    # Coordinates outside this generous game-space bound are more likely a
    # misread guest pointer than a useful navigation fact.
    if (not isinstance(value, list) or len(value) != 3 or
            not all(_number(v) and abs(v) <= 1_000_000 for v in value)):
        return None
    return [round(float(v), 4) for v in value]


def _player(value: object, presentation: int) -> tuple[dict | None, str]:
    if not isinstance(value, Mapping) or type(value.get('available')) is not bool:
        return None, 'invalid_player'
    if 'presentation' in value and (type(value['presentation']) is not int or
                                    value['presentation'] != presentation):
        return None, 'player_presentation_mismatch'
    if not value['available']:
        return None, 'player_unavailable'
    position = _vector3(value.get('position'))
    if position is None:
        return None, 'invalid_player_position'
    player: dict[str, object] = {'position': position}
    if 'velocity' in value:
        velocity = _vector3(value['velocity'])
        if velocity is None:
            return None, 'invalid_player_velocity'
        player['velocity'] = velocity
    if 'heading_radians' in value:
        heading = value['heading_radians']
        if not _number(heading):
            return None, 'invalid_player_heading'
        player['heading_radians'] = round(float(heading), 4)
    if 'grounded' in value:
        if type(value['grounded']) is not bool:
            return None, 'invalid_player_grounded'
        player['grounded'] = value['grounded']
    source = value.get('source')
    if isinstance(source, str) and 0 < len(source) <= 100:
        player['source'] = source
    return player, 'available'


def _safe_world(value: object, presentation: int) -> tuple[dict | None, str]:
    """Expose only the camera eye that the native world probe has qualified."""
    if not isinstance(value, Mapping) or type(value.get('available')) is not bool:
        return None, 'invalid_world'
    if 'presentation' in value and (type(value['presentation']) is not int or
                                    value['presentation'] != presentation):
        return None, 'world_presentation_mismatch'
    if not value['available']:
        return None, 'world_unavailable'
    camera_eye = _vector3(value.get('camera_eye'))
    if camera_eye is None:
        return None, 'invalid_camera_eye'
    world: dict[str, object] = {'camera_eye': camera_eye}
    source = value.get('source')
    if isinstance(source, str) and 0 < len(source) <= 100:
        world['source'] = source
    return world, 'available'


def _scene_depth_grid(value: object, presentation: int) -> tuple[dict | None, str]:
    """Validate a small screen-space depth observation, not a collision map."""
    if not isinstance(value, Mapping) or type(value.get('available')) is not bool:
        return None, 'invalid_scene_depth_grid'
    if type(value.get('presentation')) is not int or value['presentation'] != presentation:
        return None, 'scene_depth_grid_presentation_mismatch'
    if not value['available']:
        return None, 'scene_depth_grid_unavailable'
    if (type(value.get('width')) is not int or value['width'] != 16 or
            type(value.get('height')) is not int or value['height'] != 9 or
            value.get('encoding') != 'reversed_depth_log_u8_0_far_255_near'):
        return None, 'invalid_scene_depth_grid_format'
    rows = value.get('values')
    if (not isinstance(rows, list) or len(rows) != 9 or
            any(not isinstance(row, list) or len(row) != 16 or
                any(type(cell) is not int or not 0 <= cell <= 255 for cell in row)
                for row in rows)):
        return None, 'invalid_scene_depth_grid_values'
    grid: dict[str, object] = {
        'encoding': 'reversed_depth_log_u8_0_far_255_near',
        'width': 16, 'height': 9,
        'values': rows,
    }
    source = value.get('source')
    if isinstance(source, str) and 0 < len(source) <= 100:
        grid['source'] = source
    return grid, 'available'


def capture_telemetry(metadata: Mapping[str, object], presentation: int) -> tuple[dict | None, dict]:
    """Return qualified player/world/depth facts and component diagnostics.

    The native writer puts telemetry in the *same* metadata file as its raw
    pixels. Matching the renderer presentation prevents use of a stale menu
    state or an unrelated later frame.
    """
    raw = metadata.get('telemetry')
    if raw is None:
        return None, {'status': 'missing'}
    if not isinstance(raw, Mapping) or type(raw.get('schema_version')) is not int or raw['schema_version'] != 1:
        return None, {'status': 'unsupported_schema'}
    if type(raw.get('presentation')) is not int or raw['presentation'] != presentation:
        return None, {'status': 'presentation_mismatch'}
    player, player_status = _player(raw.get('player'), presentation)
    world, world_status = (_safe_world(raw.get('world'), presentation)
                           if 'world' in raw else (None, 'missing_world'))
    depth, depth_status = (_scene_depth_grid(raw.get('scene_depth_grid'), presentation)
                           if 'scene_depth_grid' in raw else (None, 'missing_scene_depth_grid'))
    result = {'player': player, 'world': world, 'scene_depth_grid': depth}
    status = {'status': 'available' if player or world or depth else 'unavailable',
              'player': player_status, 'world': world_status,
              'scene_depth_grid': depth_status,
              'presentation': presentation}
    return (result if player or world or depth else None), status


def movement_result(previous: dict | None, current: dict | None) -> dict | None:
    """Measure observed displacement after a delivered action.

    Camera motion is reported separately; it is not proof of player motion.
    """
    result: dict[str, object] = {}
    before = previous and previous.get('player')
    after = current and current.get('player')
    if before and after:
        delta = [round(b - a, 4) for a, b in zip(before['position'], after['position'])]
        distance = round(math.sqrt(sum(component * component for component in delta)), 4)
        result.update(player_delta=delta, player_distance=distance,
                      position_unchanged=distance < 0.01)
    before_world = previous and previous.get('world')
    after_world = current and current.get('world')
    if before_world and after_world:
        delta = [round(b - a, 4) for a, b in zip(before_world['camera_eye'], after_world['camera_eye'])]
        result['camera_delta'] = delta
        result['camera_distance'] = round(math.sqrt(sum(component * component for component in delta)), 4)
    return result or None
