"""Renders an MU2 asset from every angle that answers a question about it.

    Blender --background --python pipeline/view.py -- \
        workshop/Sword01_low.blend workshop/Sword01_albedo.png workshop/view 900

Working on a model without looking at it is how the layout came back as sixty loose
triangles and the numbers still read fine. So this exists to be looked at, and each tile
answers something specific rather than being another pretty angle:

*Textured, three-quarter and edge-on.* What it actually looks like, and whether the texture
lands where the geometry says it should.

*Wireframe over the silhouette.* Where the triangles went, and — the question that matters
for a bake — whether the outline is faceted. A normal map can fake a surface and can do
nothing at all about an outline, so a blocky silhouette here means the low-poly wants
rebuilding rather than more texture.

*Matcap, untextured.* The form alone. Paint hides a bad shape and a neutral shaded pass
does not; this is where a flat blade that should be lenticular gives itself away.

*Normals.* Whether the mesh agrees about which way is out, which decides where a bake's
cage sits and is invisible in every other view.

Rendered through Workbench rather than a path tracer. This is a diagnostic instrument, not
a beauty shot: Workbench is immediate, deterministic, and its matcap is a better read of
form than a lit render, which flatters. The lit look is what the game engine is for.
"""

import json
import math
import sys
from pathlib import Path

import bmesh
import bpy

#: The layout everything is baked and textured through. See clean_lowpoly.
BAKE_UV = "bake"


def frame(obj, camera, angle_deg: float, elevation_deg: float, margin: float = 1.28) -> None:
    """Puts the camera on an orbit at a fixed framing, whatever the model's size.

    Distance from the object's own bounding sphere rather than a constant, so a dagger and
    a two-handed sword are both filled to the frame and can be compared with each other.
    """
    corners = [obj.matrix_world @ v.co for v in obj.data.vertices]
    if not corners:
        return

    centre = sum(corners, corners[0] * 0.0) / len(corners)
    radius = max((c - centre).length for c in corners) or 1.0

    angle = math.radians(angle_deg)
    elevation = math.radians(elevation_deg)
    distance = radius * margin / math.tan(camera.data.angle * 0.5)

    camera.location = (
        centre.x + (distance * math.cos(elevation) * math.sin(angle)),
        centre.y + (distance * math.cos(elevation) * math.cos(angle) * -1.0),
        centre.z + (distance * math.sin(elevation)),
    )

    direction = centre - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def textured(obj, albedo: Path | None):
    """A material that shows the albedo through the baked layout, unlit."""
    material = bpy.data.materials.new("mu2_view")
    material.use_nodes = True
    tree = material.node_tree
    tree.nodes.clear()

    output = tree.nodes.new("ShaderNodeOutputMaterial")
    shader = tree.nodes.new("ShaderNodeBsdfDiffuse")

    if albedo and albedo.exists():
        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = bpy.data.images.load(str(albedo))
        texture.interpolation = "Closest"

        coords = tree.nodes.new("ShaderNodeUVMap")
        coords.uv_map = BAKE_UV

        tree.links.new(coords.outputs["UV"], texture.inputs["Vector"])
        tree.links.new(texture.outputs["Color"], shader.inputs["Color"])

    tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])

    obj.data.materials.clear()
    obj.data.materials.append(material)
    return material


def workbench(shading: str, colour: str, matcap: str | None = None) -> None:
    """Sets Workbench up for one kind of read. See the module note on why not a path tracer."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"

    display = scene.display.shading
    display.light = shading                 # STUDIO, MATCAP or FLAT
    display.color_type = colour             # TEXTURE, MATERIAL, SINGLE or VERTEX
    display.show_object_outline = False
    display.show_backface_culling = False
    display.show_specular_highlight = shading != "FLAT"

    # Matcaps are named from the set Blender ships and assigning anything else throws
    # rather than falling back — "basic_1.exr" is not one of them.
    if shading == "MATCAP" and matcap:
        display.studio_light = matcap


def wireframe_over(obj):
    """A copy of the mesh turned into visible wire, since a render draws no overlays.

    obj.show_wire is a viewport overlay: it draws in the editor and does not exist in a
    render, so the "wireframe" tile came back as a bare silhouette with no wires on it at
    all — a view that answers nothing while looking like it answers something. A Wireframe
    modifier turns the edges into real geometry, which a render can see.
    """
    wire = obj.copy()
    wire.data = obj.data.copy()
    wire.name = obj.name + "_wire"
    bpy.context.scene.collection.objects.link(wire)

    modifier = wire.modifiers.new("wire", "WIREFRAME")

    # Thin relative to the model, so the wires read as lines rather than as tubes. Scaled
    # off the model's own size for the same reason the camera distance is.
    modifier.thickness = max(obj.dimensions) * 0.004
    modifier.use_replace = True

    material = bpy.data.materials.new("wire")
    material.diffuse_color = (0.95, 0.62, 0.15, 1.0)
    wire.data.materials.clear()
    wire.data.materials.append(material)

    return wire


def render_to(path: Path, size: int) -> None:
    scene = bpy.context.scene
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.filepath = str(path)
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)


def stats(obj) -> dict:
    """The numbers worth having beside the pictures."""
    mesh = obj.data
    mesh.calc_loop_triangles()

    bm = bmesh.new()
    bm.from_mesh(mesh)
    open_edges = sum(1 for e in bm.edges if len(e.link_faces) < 2)
    bad_edges = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    seams = sum(1 for e in bm.edges if e.seam)

    # Surface area in world units, which is what texel density is measured against.
    area = sum(f.calc_area() for f in bm.faces)
    bm.free()

    size = obj.dimensions

    return {
        "name": obj.name,
        "triangles": len(mesh.loop_triangles),
        "vertices": len(mesh.vertices),
        "edges": len(mesh.edges),
        "seams": seams,
        "open_edges": open_edges,
        "non_manifold_edges": bad_edges,
        "uv_layers": [layer.name for layer in mesh.uv_layers],
        "size_units": [round(size.x, 2), round(size.y, 2), round(size.z, 2)],
        "surface_area_units2": round(area, 1),
    }


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        print("usage: ... --python view.py -- <in.blend> <albedo.png|-> <outPrefix> [size]")
        return

    blend = Path(argv[0])
    albedo = None if argv[1] == "-" else Path(argv[1])
    prefix = Path(argv[2]) if len(argv) > 2 else blend.with_suffix("")
    size = int(argv[3]) if len(argv) > 3 else 900

    bpy.ops.wm.open_mainfile(filepath=str(blend))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        print(f"no mesh in {blend}")
        return

    obj = meshes[0]
    textured(obj, albedo)

    camera_data = bpy.data.cameras.new("view")
    camera_data.angle = math.radians(40)
    camera = bpy.data.objects.new("view", camera_data)
    bpy.context.scene.collection.objects.link(camera)
    bpy.context.scene.camera = camera

    bpy.context.scene.world = bpy.data.worlds.new("flat")
    bpy.context.scene.world.color = (0.05, 0.05, 0.06)

    prefix.parent.mkdir(parents=True, exist_ok=True)

    # Each tile names the question it answers — see the module docstring.
    #
    # The textured views are lit FLAT on purpose. Workbench's studio light dimmed the
    # blade to near-black and the question these answer is "is the right art in the right
    # place", which lighting only gets in the way of. Form is the matcap's job, and it is
    # better at it than a three-point rig.
    shots = [
        ("three_quarter", 35, 22, "FLAT", "TEXTURE", None),
        ("side", 90, 4, "FLAT", "TEXTURE", None),
        ("edge_on", 0, 4, "FLAT", "TEXTURE", None),
        ("wireframe", 35, 22, "FLAT", "MATERIAL", None),
        ("matcap", 35, 22, "MATCAP", "SINGLE", "clay_studio.exr"),
        # A matcap built to read normals: its colour is the surface direction, so a
        # flipped or bent face is a different hue rather than a slightly darker grey.
        ("normals", 35, 22, "MATCAP", "SINGLE", "check_normal+y.exr"),
    ]

    wire = wireframe_over(obj)
    wire.hide_render = True

    written = []
    for name, yaw, pitch, light, colour, matcap in shots:
        workbench(light, colour, matcap)

        wire.hide_render = name != "wireframe"
        obj.hide_render = name == "wireframe"

        frame(obj, camera, yaw, pitch)
        out = prefix.parent / f"{prefix.name}_{name}.png"
        render_to(out, size)
        written.append(out)

    obj.hide_render = False

    numbers = stats(obj)
    numbers["renders"] = [str(p) for p in written]
    numbers["albedo"] = str(albedo) if albedo else None

    report = prefix.parent / f"{prefix.name}_stats.json"
    report.write_text(json.dumps(numbers, indent=2))

    print(f"\n=== {blend.name} ===")
    for key, value in numbers.items():
        if key != "renders":
            print(f"  {key:<22}{value}")
    print(f"  wrote                 {len(written)} views + {report.name}")


main()
