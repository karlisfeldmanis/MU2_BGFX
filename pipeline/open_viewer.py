"""Opens an MU2 asset in Blender, set up ready to be looked at.

Run through MU2/view.sh rather than directly — the wrapper knows where the asset and its
maps are and passes them in.

Blender opens a .blend the way it was saved, which for a file written by a headless script
means solid grey, no texture, camera nowhere near the model. Every one of those is a step
somebody has to repeat by hand each time they open it. This does them on load instead:

- the albedo hooked up through the *baked* layout, not MU's stacked one, so what is on
  screen is what the pipeline produced rather than what the file happens to remember
- material preview shading, so the texture is actually shown
- the object framed, with the view centred on it
- both UV layers left in place and the baked one active, so tabbing into the UV editor
  shows the layout that matters

Nothing here is saved back. It is a way of looking at the file, not a change to it.
"""

import sys
from pathlib import Path

import bpy

BAKE_UV = "bake"


def albedo_material(obj, albedo: Path | None):
    """Shows the item's own texture, through the layout it was baked into."""
    material = bpy.data.materials.new("mu2_preview")
    material.use_nodes = True
    tree = material.node_tree
    tree.nodes.clear()

    output = tree.nodes.new("ShaderNodeOutputMaterial")
    shader = tree.nodes.new("ShaderNodeBsdfPrincipled")
    output.location = (300, 0)

    if albedo and albedo.exists():
        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = bpy.data.images.load(str(albedo))

        # Closest, because the transferred art is MU's own texels and the point of looking
        # at it is to see them. Smoothing here would hide exactly the thing being judged.
        texture.interpolation = "Closest"
        texture.location = (-600, 0)

        coords = tree.nodes.new("ShaderNodeUVMap")
        coords.uv_map = BAKE_UV
        coords.location = (-800, 0)

        tree.links.new(coords.outputs["UV"], texture.inputs["Vector"])
        tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])

    tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])

    obj.data.materials.clear()
    obj.data.materials.append(material)


def show(obj) -> None:
    """Puts the viewport into a state worth opening onto."""
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type != "VIEW_3D":
                continue

            for space in area.spaces:
                if space.type != "VIEW_3D":
                    continue

                space.shading.type = "MATERIAL"
                space.shading.use_scene_lights = False
                space.shading.use_scene_world = False
                space.overlay.show_wireframes = False
                space.clip_start = 0.1
                space.clip_end = 10000.0

            # Framed through an operator, since the view matrix is the region's own and
            # setting it by hand means reimplementing what this already does correctly.
            with bpy.context.temp_override(
                    window=window, area=area,
                    region=next(r for r in area.regions if r.type == "WINDOW")):
                bpy.ops.object.select_all(action="DESELECT")
                obj.select_set(True)
                bpy.context.view_layer.objects.active = obj
                bpy.ops.view3d.view_selected()


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    albedo = Path(argv[0]) if argv and argv[0] != "-" else None

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        print("no mesh in this file")
        return

    obj = meshes[0]
    albedo_material(obj, albedo)

    # The baked layout is the one the UV editor should open onto.
    if BAKE_UV in obj.data.uv_layers:
        obj.data.uv_layers.active = obj.data.uv_layers[BAKE_UV]

    show(obj)

    mesh = obj.data
    mesh.calc_loop_triangles()
    print(
        f"\n=== {obj.name} ===\n"
        f"  {len(mesh.loop_triangles)} triangles, {len(mesh.vertices)} vertices\n"
        f"  uv layers: {[layer.name for layer in mesh.uv_layers]} (active: {BAKE_UV})\n"
        f"  albedo:    {albedo if albedo else 'none'}\n"
        f"  size:      {mesh and tuple(round(v, 1) for v in obj.dimensions)} units\n")


main()
