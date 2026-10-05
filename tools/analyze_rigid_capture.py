"""Inspect original rigid vertex coverage against captured replay matrices.

This is a geometric diagnostic, not an original shader executor or a claim of
GPU coverage. Dot products use Python double precision. The original raw files
are unchanged; the report distinguishes trivial clip rejection from candidates.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct


def analyze(directory: Path, staging: Path, prefix: str = "rigid-first", ps_staging: Path | None = None):
    inputs = {}

    def read(name):
        data = (directory / name).read_bytes()
        inputs[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        return data

    geometry = read(f"{prefix}-geometry.bin")
    vertices = read(f"{prefix}-vertices.bin")
    indices = read(f"{prefix}-indices.bin")
    elements = read(f"{prefix}-elements.bin")
    submeshes = read(f"{prefix}-submeshes.bin")
    constants = staging.read_bytes()
    inputs[str(staging)] = {"bytes": len(constants), "sha256": hashlib.sha256(constants).hexdigest()}
    if len(geometry) != 0x78 or len(constants) != 56 * 16:
        raise ValueError("Expected original geometry header and full 56-register VS bank")
    header = struct.unpack(">30I", geometry)
    stride = header[1]
    if not stride or len(vertices) != header[0] or len(vertices) % stride:
        raise ValueError("Original vertex byte extent/stride differs")
    if len(indices) != header[5] or len(indices) % 2 or len(elements) != header[2] * 12:
        raise ValueError("Original index/declaration extent differs")
    declaration = list(struct.iter_unpack(">III", elements))
    if declaration[-1] != (0x00FF0000, 0xFFFFFFFF, 0):
        raise ValueError("Original declaration terminator differs")
    position = [(stream, kind) for stream, kind, semantic in declaration[:-1] if semantic == 0]
    if len(position) != 1 or position[0][0] >> 16 or position[0][1] != 0x002A23B9:
        raise ValueError("Expected one original stream0 position0 FLOAT3")
    offset = position[0][0] & 0xFFFF
    semantics = {}
    for stream, kind, semantic in declaration[:-1]:
        usage, index = (semantic >> 16) & 0xFF, (semantic >> 8) & 0xFF
        semantics[(usage, index)] = (stream & 0xFFFF, kind)
    if semantics.get((3, 0), (None, None))[1] != 0x002A2187 or semantics.get((10, 0), (None, None))[1] != 0x00182886:
        raise ValueError("Expected original packed NORMAL0 and byte4 COLOR0")
    normal_offset, color_offset = semantics[(3, 0)][0], semantics[(10, 0)][0]
    if offset + 12 > stride or len(submeshes) % 36:
        raise ValueError("Position or submesh extent differs")
    xyz = [struct.unpack_from(">3f", vertices, at + offset) for at in range(0, len(vertices), stride)]
    def unpack_normal(at):
        packed = struct.unpack_from(">I", vertices, at + normal_offset)[0]
        out = []
        for lane in range(3):
            raw = (packed >> (10 * lane)) & 1023
            signed = raw - (1024 if raw & 512 else 0)
            out.append(max(-1.0, signed / 511.0))
        return tuple(out)
    normals = [unpack_normal(at) for at in range(0, len(vertices), stride)]
    colors = []
    for at in range(0, len(vertices), stride):
        color = struct.unpack_from(">I", vertices, at + color_offset)[0]
        colors.append(((color >> 16 & 255) / 255.0, (color >> 8 & 255) / 255.0,
                       (color & 255) / 255.0, (color >> 24) / 255.0))
    matrix = list(struct.iter_unpack(">4f", constants))[:4]
    if not all(math.isfinite(x) for row in [*xyz, *matrix] for x in row):
        raise ValueError("Nonfinite source position or consumed matrix")
    clip = [tuple(sum(m[j] * (*v, 1.0)[j] for j in (2, 0, 1, 3)) for m in matrix) for v in xyz]
    all_vs = list(struct.iter_unpack(">4f", constants))
    normal_matrix = all_vs[12:15]
    world_normals = [
        tuple(sum(n[j] * m[j] for j in (2, 0, 1)) for m in normal_matrix)
        for n in normals
    ]
    unit_normals = []
    for n in world_normals:
        length = math.sqrt(sum(x * x for x in n))
        unit_normals.append(tuple(x / length for x in n) if length else (0.0, 0.0, 0.0))
    light = None
    if ps_staging is not None:
        ps_bytes = ps_staging.read_bytes()
        inputs[str(ps_staging)] = {"bytes": len(ps_bytes), "sha256": hashlib.sha256(ps_bytes).hexdigest()}
        if len(ps_bytes) < 37 * 16 or len(ps_bytes) % 16:
            raise ValueError("Expected captured PS constant bank containing c36")
        pc36 = list(struct.iter_unpack(">4f", ps_bytes))[36]
        light = [max(0.0, min(1.0, sum(n[i] * pc36[i] for i in range(3)))) for n in unit_normals]
    index_values = list(struct.unpack(f">{len(indices)//2}H", indices))
    if any(i != 0xFFFF and i >= len(xyz) for i in index_values):
        raise ValueError("Original index exceeds the captured vertex allocation")

    def distances(v):
        x, y, z, w = v
        return (x + w, w - x, y + w, w - y, z, w - z)

    planes = [distances(v) for v in clip]
    results = []
    for number, row in enumerate(struct.iter_unpack(">9I", submeshes)):
        _, _, _, primitive, base_vertex, first_index, index_count, _, _ = row
        if primitive != 6 or base_vertex or first_index + index_count > len(index_values):
            raise ValueError("Only original primitive6/base0 bounded strips are qualified")
        counts = {"strip_triangles": 0, "degenerate_index_triangles": 0,
                  "trivially_clipped_triangles": 0, "fully_inside_triangles": 0,
                  "partially_clipped_candidates": 0, "all_nonpositive_w_triangles": 0}
        by_plane = [0] * 6
        window = []
        used = set()
        signed_areas = {"positive": 0, "negative": 0, "zero": 0}
        parity = 0
        for index in index_values[first_index:first_index + index_count]:
            if index == 0xFFFF:
                window = []
                parity = 0
                continue
            used.add(index)
            window.append(index)
            if len(window) < 3:
                continue
            window = window[-3:]
            triangle = window.copy()
            if parity & 1:
                triangle[0], triangle[1] = triangle[1], triangle[0]
            parity += 1
            counts["strip_triangles"] += 1
            if len(set(triangle)) != 3:
                counts["degenerate_index_triangles"] += 1
                continue
            outside = [all(planes[i][p] < 0 for i in triangle) for p in range(6)]
            for p, rejected in enumerate(outside):
                by_plane[p] += rejected
            if any(outside):
                counts["trivially_clipped_triangles"] += 1
            elif all(all(d >= 0 for d in planes[i]) for i in triangle):
                counts["fully_inside_triangles"] += 1
            else:
                counts["partially_clipped_candidates"] += 1
            if all(clip[i][3] <= 0 for i in triangle):
                counts["all_nonpositive_w_triangles"] += 1
            if all(clip[i][3] > 0 for i in triangle):
                ndc = [(clip[i][0] / clip[i][3], clip[i][1] / clip[i][3]) for i in triangle]
                a, b, c = ndc
                area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
                signed_areas["positive" if area > 0 else "negative" if area < 0 else "zero"] += 1
        results.append({"submesh_index": number, "original_words": list(row), "counts": counts,
                        "rejected_by_plane_left_right_bottom_top_near_far": by_plane,
                        "positive_w_ndc_triangle_area_signs": signed_areas,
                        "position_bounds": [[min(xyz[i][j] for i in used), max(xyz[i][j] for i in used)]
                                            for j in range(3)] if used else [],
                        "clip_bounds": [[min(clip[i][j] for i in used), max(clip[i][j] for i in used)]
                                        for j in range(4)] if used else [],
                        "attribute_stats": ({
                            "referenced_vertices": len(used),
                            "color_min": [min(colors[i][j] for i in used) for j in range(4)],
                            "color_max": [max(colors[i][j] for i in used) for j in range(4)],
                            "color_mean": [sum(colors[i][j] for i in used) / len(used) for j in range(4)],
                            "color_blue_ge_0_9": sum(colors[i][2] >= 0.9 for i in used),
                            "world_normal_length_min": min(math.sqrt(sum(x*x for x in world_normals[i])) for i in used),
                            "world_normal_length_max": max(math.sqrt(sum(x*x for x in world_normals[i])) for i in used),
                            "light_term_min": min(light[i] for i in used) if light is not None else None,
                            "light_term_max": max(light[i] for i in used) if light is not None else None,
                            "light_term_mean": (sum(light[i] for i in used) / len(used)) if light is not None else None,
                        } if used else {})})
    return {"capture_source": "original_rigid_geometry_and_replay_vs",
            "method": "double precision geometric projection; no rasterization, cull or depth-test simulation",
            "gameplay_verified": False, "vertex_count": len(xyz), "stride": stride,
            "matrix_rows_c0_to_c3": matrix, "submeshes": results, "inputs": inputs}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--staging", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--prefix", default="rigid-first")
    parser.add_argument("--ps-staging", type=Path)
    args = parser.parse_args()
    report = analyze(args.directory, args.staging or args.directory / "recording-first-staging-vs.bin",
                     args.prefix, args.ps_staging)
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
