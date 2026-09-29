"""The two pets' cells in the buff strip, rendered from their own models: buff_angel, buff_imp.

    python3 pipeline/pet_icons.py              renders both through Blender, then composes

Ours and not MuDream's. MuDream's status sheet has no cell for a pet -- MuMain shows a worn pet
only in its own life bar (NewUIItemEnduranceInfo) -- so there is nothing for buff_icons.py to
cut. What stands in the cell is the pet itself: the built Helper01/Helper02 from workshop/,
three-quarter on from above, over a glow in its own colour, the way the cut cells put their
emblem on a lit ground. The Angel's glow is the pale green-gold of the BITMAP_LIGHT MU hangs
on it (0.5, 0.8, 0.6, GOBoid.cpp RenderMount); the Imp's is the red one it carries on the
shoulder (0.5, 0, 0, ZzzCharacter.cpp:15433-15466).

Same size as the cut cells, 80 by 112, and no alpha, as theirs have none.
"""
import math
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
OUT = PROJECT / "source" / "interface" / "buffs"
WORKSHOP = PROJECT / "workshop" / "items" / "pets"
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
W, H = 80, 112
SCALE = 4  # composed at four times and taken down, for clean edges
RENDER_SIZE = 1600  # the render, square; cropped to the figure, which is small in it

PETS = {
    # name, glow centre colour, glow edge colour, the camera's turn about the pet
    "angel": ("Helper01", (150, 200, 160), (18, 34, 28), 35.0),
    "imp": ("Helper02", (230, 70, 40), (45, 8, 6), 35.0),
}

RENDER = r'''
import bpy, math, sys, mathutils
glb, out, turn = sys.argv[-3], sys.argv[-2], float(sys.argv[-1])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
lo = mathutils.Vector((1e9, 1e9, 1e9)); hi = -lo
for o in meshes:
    for c in o.bound_box:
        p = o.matrix_world @ mathutils.Vector(c)
        lo = mathutils.Vector(map(min, lo, p)); hi = mathutils.Vector(map(max, hi, p))
centre = (lo + hi) / 2
size = max(hi - lo)
scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
scene.render.resolution_x = scene.render.resolution_y = %(size)d
scene.render.film_transparent = True
scene.world = bpy.data.worlds.new("w"); scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs[1].default_value = 0.6
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = "ORTHO"; cam.data.ortho_scale = size * 1.25
a = math.radians(turn)
d = size * 3
cam.location = centre + mathutils.Vector((math.sin(a) * d, -math.cos(a) * d, d * 0.35))
cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
for name, energy, rot in (("key", 4.0, (50, 0, 30)), ("fill", 1.5, (60, 0, -120))):
    l = bpy.data.objects.new(name, bpy.data.lights.new(name, "SUN"))
    l.data.energy = energy; l.rotation_euler = [math.radians(v) for v in rot]
    scene.collection.objects.link(l)
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
'''


def render(glb: Path, out: Path, turn: float) -> None:
    script = out.with_suffix(".py")
    script.write_text(RENDER % {"size": RENDER_SIZE})
    subprocess.run([BLENDER, "--background", "--python", str(script), "--", str(glb), str(out),
                    str(turn)], check=True, stdout=subprocess.DEVNULL)
    script.unlink()


def compose(figure: Path, centre, edge, out: Path) -> None:
    from PIL import Image, ImageFilter

    w, h = W * SCALE, H * SCALE
    ground = Image.new("RGB", (w, h))
    px = ground.load()
    for y in range(h):
        for x in range(w):
            t = min(1.0, math.hypot((x - w / 2) / (w * 0.62), (y - h * 0.45) / (h * 0.55)))
            t = t * t
            px[x, y] = tuple(int(c * (1 - t) + e * t) for c, e in zip(centre, edge))
    pet = Image.open(figure).convert("RGBA")
    # Cropped to the figure and fitted to the cell, whatever else the model's bounds held.
    pet = pet.crop(pet.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())
    fit = min(w * 0.88 / pet.width, h * 0.78 / pet.height)
    pet = pet.resize((max(1, int(pet.width * fit)), max(1, int(pet.height * fit))), Image.LANCZOS)
    placed = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    placed.paste(pet, ((w - pet.width) // 2, int(h * 0.47) - pet.height // 2))
    pet = placed
    # A soft dark rim under the figure, so a pale angel reads off a pale glow.
    shadow = Image.new("RGBA", pet.size, (0, 0, 0, 0))
    shadow.putalpha(pet.getchannel("A").point(lambda a: int(a * 0.55)))
    shadow = shadow.filter(ImageFilter.GaussianBlur(SCALE * 2))
    ground.paste(shadow, (0, 0), shadow)
    ground.paste(pet, (0, 0), pet)
    ground.resize((W, H), Image.LANCZOS).save(out)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (item, centre, edge, turn) in PETS.items():
        glb = WORKSHOP / item / f"{item}.glb"
        if not glb.exists():
            print(f"missing {glb}: build it with tools/asset.sh {item}", file=sys.stderr)
            return 1
        figure = OUT / f"_{name}_figure.png"
        render(glb, figure, turn)
        compose(figure, centre, edge, OUT / f"buff_{name}.png")
        figure.unlink()
        print(f"wrote {OUT / f'buff_{name}.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
