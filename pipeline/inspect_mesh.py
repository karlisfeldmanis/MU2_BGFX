"""Reports what MU actually shipped for one item, measured inside Blender.

Run headlessly:

    Blender --background --python pipeline/inspect_mesh.py -- source/items/Sword01/Sword01.obj

The first thing the pipeline needs is not an opinion about the model but a description of
it. A high-poly bake is a decision about where to spend triangles and texels, and both of
those are answered by numbers this prints: how big the thing is in world units, how many
triangles it already has, whether its UVs are laid out or overlapping, and how much of the
UV square it actually uses.

MU's item art is small and old enough that several of these come back surprising, and it
is cheaper to be surprised here than three stages later with a bake that will not lay
down.
"""

import sys
from pathlib import Path

import bmesh
import bpy


def clear() -> None:
    """An empty file, since Blender starts with a cube in it."""
    bpy.ops.wm.read_factory_settings(use_empty=True)


def load(path: Path):
    """Imports the OBJ and returns its mesh objects."""
    bpy.ops.wm.obj_import(filepath=str(path), forward_axis="NEGATIVE_Z", up_axis="Y")
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def uv_coverage(mesh) -> tuple[float, float]:
    """What fraction of the UV square the shells cover, and how much overlaps itself.

    Measured by rasterising the UV triangles into a coarse grid rather than by summing
    triangle areas: the sum answers a different question, because two shells stacked on the
    same texels have twice the area and half the resolution. MU stacks a great deal — a
    sword's two faces are usually the same texels — and that matters for a bake, which
    cannot write two different results to one texel.
    """
    size = 256
    seen = [0] * (size * size)

    bm = bmesh.new()
    bm.from_mesh(mesh)
    layer = bm.loops.layers.uv.active

    if layer is None:
        bm.free()
        return 0.0, 0.0

    for face in bm.faces:
        points = [loop[layer].uv for loop in face.loops]
        if len(points) < 3:
            continue

        us = [p.x for p in points]
        vs = [p.y for p in points]

        # A bounding-box stamp rather than a true rasterisation. It overstates coverage a
        # little on diagonal shells and is far simpler; what it is used for is a rough
        # "is this laid out or is it a mess", which it answers either way.
        u0 = max(0, min(size - 1, int(min(us) * size)))
        u1 = max(0, min(size - 1, int(max(us) * size)))
        v0 = max(0, min(size - 1, int(min(vs) * size)))
        v1 = max(0, min(size - 1, int(max(vs) * size)))

        for v in range(v0, v1 + 1):
            for u in range(u0, u1 + 1):
                seen[(v * size) + u] += 1

    bm.free()

    covered = sum(1 for c in seen if c > 0) / (size * size)
    overlapped = sum(1 for c in seen if c > 1) / max(1, sum(1 for c in seen if c > 0))
    return covered, overlapped


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        print("usage: ... --python inspect_mesh.py -- <mesh.obj>")
        return

    path = Path(argv[0])
    clear()
    objects = load(path)

    if not objects:
        print(f"nothing imported from {path}")
        return

    print(f"\n=== {path.name} ===")

    for obj in objects:
        mesh = obj.data
        mesh.calc_loop_triangles()

        size = obj.dimensions
        triangles = len(mesh.loop_triangles)
        covered, overlapped = uv_coverage(mesh)

        # Non-manifold and loose geometry decide whether this can be subdivided cleanly or
        # wants rebuilding first. MU's meshes are drawn, not modelled: they are triangle
        # soup with split vertices wherever the UV seams are, and a subdivision surface
        # over that pulls the seams apart.
        bm = bmesh.new()
        bm.from_mesh(mesh)
        loose = sum(1 for v in bm.verts if not v.link_faces)
        open_edges = sum(1 for e in bm.edges if len(e.link_faces) < 2)
        bad_edges = sum(1 for e in bm.edges if len(e.link_faces) > 2)
        bm.free()

        print(f"  object      {obj.name}")
        print(f"  triangles   {triangles}")
        print(f"  vertices    {len(mesh.vertices)}")
        print(f"  size        {size.x:.1f} x {size.y:.1f} x {size.z:.1f} units")
        print(f"  uv covered  {covered:.1%} of the square")
        print(f"  uv overlap  {overlapped:.1%} of what is covered is stacked")
        print(f"  loose verts {loose}")
        print(f"  open edges  {open_edges}   non-manifold edges {bad_edges}")

        materials = [s.material.name for s in obj.material_slots if s.material]
        print(f"  materials   {materials or 'none'}")


main()
