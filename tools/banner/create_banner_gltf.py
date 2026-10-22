#!/usr/bin/env python3
"""Create an animated, transparent 3D wordmark GLB for the 3DS HOME Menu."""

import json
import math
import struct
import sys
from pathlib import Path


GLYPHS = {
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "N": ("10001", "11001", "11001", "10101", "10011", "10011", "10001"),
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
}

CELL = 0.62
LETTER_GAP = 0.20
DEPTH = 0.72
CAMERA_Y = 1.0
WORD_SCALE = 0.58
TURN_SECONDS = 8
CAMERA_VIEW_HEIGHT = 24.0


def make_word_quads():
    rows = []
    cursor = 0
    for letter in "PIXELNAUTE":
        pattern = GLYPHS[letter]
        for row, line in enumerate(pattern):
            for column, filled in enumerate(line):
                if filled == "1":
                    rows.append((cursor + column, row))
        cursor += len(pattern[0]) + LETTER_GAP / CELL

    width = (cursor - LETTER_GAP / CELL) * CELL
    height = 7 * CELL
    origin_x = -width / 2
    origin_y = -height / 2
    cells = {(x, y) for x, y in rows}
    front = []
    sides = []
    half_depth = DEPTH / 2

    def add_quad(target, vertices, normal):
        a, b, c = vertices[:3]
        ab = tuple(b[i] - a[i] for i in range(3))
        ac = tuple(c[i] - a[i] for i in range(3))
        cross = (
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
        if sum(cross[i] * normal[i] for i in range(3)) < 0:
            vertices = list(reversed(vertices))
        target.append((vertices, normal))

    for column, row in cells:
        x0 = origin_x + column * CELL
        x1 = x0 + CELL
        y1 = origin_y + height - row * CELL
        y0 = y1 - CELL
        add_quad(front, [
            (x0, y0, half_depth), (x1, y0, half_depth),
            (x1, y1, half_depth), (x0, y1, half_depth),
        ], (0, 0, 1))
        add_quad(front, [
            (x0, y0, -half_depth), (x1, y0, -half_depth),
            (x1, y1, -half_depth), (x0, y1, -half_depth),
        ], (0, 0, -1))

        if (column - 1, row) not in cells:
            add_quad(sides, [
                (x0, y0, half_depth), (x0, y0, -half_depth),
                (x0, y1, -half_depth), (x0, y1, half_depth),
            ], (-1, 0, 0))
        if (column + 1, row) not in cells:
            add_quad(sides, [
                (x1, y0, half_depth), (x1, y0, -half_depth),
                (x1, y1, -half_depth), (x1, y1, half_depth),
            ], (1, 0, 0))
        if (column, row - 1) not in cells:
            add_quad(sides, [
                (x0, y1, half_depth), (x0, y1, -half_depth),
                (x1, y1, -half_depth), (x1, y1, half_depth),
            ], (0, 1, 0))
        if (column, row + 1) not in cells:
            add_quad(sides, [
                (x0, y0, half_depth), (x0, y0, -half_depth),
                (x1, y0, -half_depth), (x1, y0, half_depth),
            ], (0, -1, 0))

    return front, sides, width, height


def pack_primitive(quads, binary):
    positions = []
    normals = []
    indices = []
    for vertices, normal in quads:
        first = len(positions) // 3
        for vertex in vertices:
            positions.extend(vertex)
            normals.extend(normal)
        indices.extend((first, first + 1, first + 2, first, first + 2, first + 3))

    def f32(values):
        return struct.pack("<" + "f" * len(values), *values)

    def u16(values):
        return struct.pack("<" + "H" * len(values), *values)

    position_data = f32(positions)
    normal_data = f32(normals)
    index_data = u16(indices)

    while len(binary) % 4:
        binary.append(0)
    position_offset = len(binary)
    binary.extend(position_data)
    position_view = len(buffer_views)
    buffer_views.append({
        "buffer": 0, "byteOffset": position_offset,
        "byteLength": len(position_data), "target": 34962,
    })

    while len(binary) % 4:
        binary.append(0)
    normal_offset = len(binary)
    binary.extend(normal_data)
    normal_view = len(buffer_views)
    buffer_views.append({
        "buffer": 0, "byteOffset": normal_offset,
        "byteLength": len(normal_data), "target": 34962,
    })

    while len(binary) % 4:
        binary.append(0)
    index_offset = len(binary)
    binary.extend(index_data)
    index_view = len(buffer_views)
    buffer_views.append({
        "buffer": 0, "byteOffset": index_offset,
        "byteLength": len(index_data), "target": 34963,
    })

    position_values = list(zip(*(iter(positions),) * 3))
    position_accessor = len(accessors)
    accessors.append({
        "bufferView": position_view, "componentType": 5126,
        "count": len(positions) // 3, "type": "VEC3",
        "min": [min(point[i] for point in position_values) for i in range(3)],
        "max": [max(point[i] for point in position_values) for i in range(3)],
    })
    normal_accessor = len(accessors)
    accessors.append({
        "bufferView": normal_view, "componentType": 5126,
        "count": len(normals) // 3, "type": "VEC3",
    })
    index_accessor = len(accessors)
    accessors.append({
        "bufferView": index_view, "componentType": 5123,
        "count": len(indices), "type": "SCALAR",
    })
    return {
        "attributes": {"POSITION": position_accessor, "NORMAL": normal_accessor},
        "indices": index_accessor,
        "mode": 4,
    }


def build_glb(output):
    global buffer_views, accessors
    buffer_views = []
    accessors = []
    binary = bytearray()
    front, sides, width, height = make_word_quads()
    projected_height = math.hypot(width, height + DEPTH) * WORD_SCALE
    if projected_height > CAMERA_VIEW_HEIGHT * 0.82:
        raise ValueError("Wordmark exceeds the HOME Menu camera's visible area")

    front_data = pack_primitive(front, binary)
    sides_data = pack_primitive(sides, binary)

    keys = []
    rotations = []
    for step in range(5):
        angle = 2 * math.pi * step / 4
        keys.append(step * TURN_SECONDS / 4)
        rotations.extend((0.0, math.sin(angle / 2), 0.0, math.cos(angle / 2)))

    while len(binary) % 4:
        binary.append(0)
    time_offset = len(binary)
    time_data = struct.pack("<5f", *keys)
    binary.extend(time_data)
    time_view = len(buffer_views)
    buffer_views.append({
        "buffer": 0, "byteOffset": time_offset,
        "byteLength": len(time_data),
    })
    time_accessor = len(accessors)
    accessors.append({
        "bufferView": time_view, "componentType": 5126,
        "count": len(keys), "type": "SCALAR",
        "min": [keys[0]], "max": [keys[-1]],
    })

    while len(binary) % 4:
        binary.append(0)
    rotation_offset = len(binary)
    rotation_data = struct.pack("<20f", *rotations)
    binary.extend(rotation_data)
    rotation_view = len(buffer_views)
    buffer_views.append({
        "buffer": 0, "byteOffset": rotation_offset,
        "byteLength": len(rotation_data),
    })
    rotation_accessor = len(accessors)
    accessors.append({
        "bufferView": rotation_view, "componentType": 5126,
        "count": len(keys), "type": "VEC4",
    })

    primitives = []
    for material, data in enumerate((front_data, sides_data)):
        primitives.append({
            "attributes": data["attributes"],
            "indices": data["indices"],
            "material": material,
            "mode": data["mode"],
        })

    document = {
        "asset": {"version": "2.0", "generator": "Pixelnaute banner generator"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "BannerPosition", "translation": [0, CAMERA_Y, 0], "children": [1]},
            {"name": "PixelnauteSpin", "mesh": 0, "scale": [WORD_SCALE] * 3},
        ],
        "meshes": [{"name": "PIXELNAUTE", "primitives": primitives}],
        "materials": [
            {
                "name": "LetterFaces",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.92, 0.94, 1.0, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.65,
                },
            },
            {
                "name": "LetterSides",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.30, 0.43, 0.68, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.8,
                },
            },
        ],
        "animations": [{
            "name": "SlowTurn",
            "samplers": [{"input": time_accessor, "output": rotation_accessor, "interpolation": "LINEAR"}],
            "channels": [{
                "sampler": 0,
                "target": {"node": 1, "path": "rotation"},
            }],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }

    json_chunk = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * ((-len(json_chunk)) % 4)
    binary.extend(b"\0" * ((-len(binary)) % 4))
    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary)
    with Path(output).open("wb") as glb:
        glb.write(struct.pack("<III", 0x46546C67, 2, total_length))
        glb.write(struct.pack("<II", len(json_chunk), 0x4E4F534A))
        glb.write(json_chunk)
        glb.write(struct.pack("<II", len(binary), 0x004E4942))
        glb.write(binary)
    print(f"Wrote {output}: {width:.1f} x {height:.1f} world units, {len(binary)} geometry bytes")


def build_svg_preview(output):
    rows = []
    cursor = 0
    for letter in "PIXELNAUTE":
        pattern = GLYPHS[letter]
        for row, line in enumerate(pattern):
            for column, filled in enumerate(line):
                if filled == "1":
                    rows.append((cursor + column, row))
        cursor += len(pattern[0]) + 0.35

    width = cursor - 0.35
    scale = 620 / width
    cell = scale
    height = 7 * cell
    x_offset = (768 - width * scale) / 2
    y_offset = (384 - height) / 2
    path = []
    for column, row in rows:
        x = x_offset + column * scale
        y = y_offset + row * cell
        path.append(f"M{x:.2f},{y:.2f}h{cell:.2f}v{cell:.2f}h-{cell:.2f}z")
    d = "".join(path)
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" width="768" height="384" '
        'viewBox="0 0 768 384">'
        f'<path d="{d}" transform="translate(4 5)" fill="#405a83"/>'
        f'<path d="{d}" transform="translate(2 3)" fill="#7188ad"/>'
        f'<path d="{d}" fill="#f4f0ef"/>'
        "</svg>"
    )
    Path(output).write_text(svg, encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} output.glb [preview.svg]")
    build_glb(sys.argv[1])
    if len(sys.argv) == 3:
        build_svg_preview(sys.argv[2])
