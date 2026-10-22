#!/usr/bin/env python3
"""Convert the banner GLB and preserve its complete 360-degree Y rotation."""

import math
import sys
from pathlib import Path

import gltflib


EXPECTED_BONE = "PixelnauteSpin"
EXPECTED_FRAMES = (0.0, 120.0, 240.0, 360.0, 480.0)


def main(pycgfx_path, input_glb, output_cgfx):
    sys.path.insert(0, str(Path(pycgfx_path)))
    import main as pycgfx

    gltf = gltflib.GLTF.load(input_glb, load_file_resources=True)
    cgfx = pycgfx.convert_gltf(gltf)
    model = cgfx.data.models["COMMON"]
    animation = cgfx.data.skeletal_animations["COMMON"]
    if EXPECTED_BONE not in animation.member_animations_data:
        raise RuntimeError(f"Missing rotation track for {EXPECTED_BONE}")

    track = animation.member_animations_data[EXPECTED_BONE].rot_y
    if track is None or len(track.segments) != 1:
        raise RuntimeError("Expected exactly one Y-axis rotation segment")
    keys = track.segments[0].keys
    if tuple(key.frame for key in keys) != EXPECTED_FRAMES:
        raise RuntimeError(f"Unexpected rotation keyframes: {[key.frame for key in keys]}")

    for key, frame in zip(keys, EXPECTED_FRAMES):
        key.value = frame / 120.0 * (math.pi / 2)

    bones = list(model.skeleton.bones)
    materials = list(model.materials)
    mesh_names = []
    for mesh in model.meshes.data.contents:
        shape = model.shapes.data.contents[mesh.shape_index]
        bone_index = shape.primitive_sets.data.contents[0].related_bones.data.contents[0]
        if bone_index >= len(bones) or mesh.material_index >= len(materials):
            raise RuntimeError("Banner mesh references an invalid bone or material")
        mesh.mesh_node_name = bones[bone_index]
        mesh.name = f"PIXELNAUTE_{materials[mesh.material_index]}"
        shape.name = mesh.name
        mesh_names.append(mesh.name)
    if mesh_names != ["PIXELNAUTE_LetterFaces", "PIXELNAUTE_LetterSides"]:
        raise RuntimeError(f"Unexpected banner meshes: {mesh_names}")

    data = pycgfx.write(cgfx)
    if len(data) > 512 * 1024:
        raise RuntimeError(f"CGFX exceeds the HOME Menu limit: {len(data)} bytes")
    Path(output_cgfx).write_bytes(data)
    print(f"Wrote {output_cgfx}: {len(data)} bytes")
    print("Y rotation:", [(key.frame, round(key.value, 3)) for key in keys])
    print("Meshes:", ", ".join(mesh_names))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(
            f"Usage: {Path(sys.argv[0]).name} pycgfx-directory input.glb output.cgfx"
        )
    main(*sys.argv[1:])
