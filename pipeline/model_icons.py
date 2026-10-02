"""The buff strip's cells that are ours, rendered from the thing's own model: buff_angel,
buff_imp, buff_ale and buff_uniria.

    python3 pipeline/model_icons.py            renders through Blender (kept), then composes

The Ale's was MuDream's cell 10 until 2026-09-29 -- eBuff_BlessPotion, Season 6's siege flask,
borrowed (buff_icons.py) -- and read soft beside every cut cell and was not the Ale. Now it is
the Ale's own bottle, the one the bag shows, on an amber ground no cut cell uses.

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
WORKSHOP = PROJECT / "workshop" / "items"
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
W, H = 80, 112
SCALE = 4  # composed at four times and taken down, for clean edges
RENDER_SIZE = 1600  # the render, square; cropped to the figure, which is small in it

PETS = {
    # name: the model, the ground's lit centre and its edge, the glow round the figure, and the
    # camera's turn about the pet. A ground each of its own hue beside the cut cells' crimson,
    # blue, olive, orange and green: the Angel's is teal off its own light (0.5, 0.8, 0.6), the
    # Imp's an ember red off its (0.5, 0, 0), deeper and yellower than Defense's pink crimson.
    "angel": ("pets/Helper01", (25, 140, 140), (2, 22, 30), (215, 255, 240), 35.0),
    "imp": ("pets/Helper02", (240, 100, 30), (48, 4, 2), (255, 215, 130), 35.0),
    "ale": ("misc/Ale01", (225, 150, 35), (52, 22, 2), (255, 240, 180), 20.0),
    # The Horn of Uniria's cell is the horse it calls, Rider01, in its rest pose, which rears.
    # MU hangs no light on it (GOBoid.cpp RenderMount), so the ground is ours: a dusk violet no
    # other cell uses, under a pale lilac glow that the white mane stands out of.
    "uniria": ("pets/Rider01", (120, 85, 185), (18, 8, 42), (235, 225, 255), 35.0),
}

# Degrees, anticlockwise, for what stands too straight in its cell.
LEAN = {"ale": 24.0}

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


def compose(figure: Path, centre, edge, glow, out: Path, lean: float = 0.0) -> None:
    """The cut cells' own treatment, read off them rather than invented: a full-bleed ground
    lit from the middle and never gone to black, one emblem filling the cell with a glow of a
    pale tint of the ground round it, and the bevel every one of them has -- a lit band along
    the top, a dark line down the left and a dark foot (buff_defense: (244, 144, 178) on the
    top row over (140, 42, 67), (41, 8, 0) at the foot)."""
    from PIL import Image, ImageChops, ImageFilter

    w, h = W * SCALE, H * SCALE
    ground = Image.new("RGB", (w, h))
    px = ground.load()
    for y in range(h):
        for x in range(w):
            t = min(1.0, math.hypot((x - w / 2) / (w * 0.75), (y - h * 0.42) / (h * 0.7)))
            t = t ** 1.1
            px[x, y] = tuple(int(c * (1 - t) + e * t) for c, e in zip(centre, edge))
    pet = Image.open(figure).convert("RGBA")
    # Leaned, where the thing is a bottle standing straight -- the cut cells' emblems are thrown
    # across the cell rather than stood in it, the flask this replaced among them.
    if lean:
        pet = pet.rotate(lean, resample=Image.BICUBIC, expand=True)
    # Cropped to the figure and fitted to the cell, whatever else the model's bounds held --
    # filling it, as the cut cells' emblems do.
    pet = pet.crop(pet.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())
    fit = min(w * 0.96 / pet.width, h * 0.86 / pet.height)
    pet = pet.resize((max(1, int(pet.width * fit)), max(1, int(pet.height * fit))), Image.LANCZOS)
    placed = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    placed.paste(pet, ((w - pet.width) // 2, int(h * 0.5) - pet.height // 2))
    pet = placed
    alpha = pet.getchannel("A")
    # The glow: the figure's shape grown and softened, in the pale tint, so a dark imp and a
    # pale angel both stand off their ground at thirty pixels.
    halo = alpha.filter(ImageFilter.MaxFilter(SCALE * 4 + 1)).filter(ImageFilter.GaussianBlur(SCALE * 5))
    halo = halo.point(lambda a: int(min(255, a * 1.6)))
    ground.paste(Image.new("RGB", (w, h), glow), (0, 0), halo)
    # A thin dark line hugging it inside the glow, so the edge is drawn and not smudged.
    line = alpha.filter(ImageFilter.MaxFilter(SCALE + 1)).filter(ImageFilter.GaussianBlur(SCALE * 0.6))
    line = ImageChops.subtract(line, alpha).point(lambda a: int(a * 0.6))
    ground.paste(Image.new("RGB", (w, h), tuple(int(c * 0.25) for c in edge)), (0, 0), line)
    ground.paste(pet, (0, 0), pet)

    small = ground.resize((W, H), Image.LANCZOS)
    bevel = small.load()
    lit = tuple(min(255, int(c * 0.35 + 255 * 0.65)) for c in centre)
    dark = tuple(int(c * 0.35) for c in edge)
    for x in range(W):
        bevel[x, 0] = lit
        bevel[x, 1] = tuple((a + b) // 2 for a, b in zip(lit, bevel[x, 1]))
        bevel[x, H - 1] = dark
        bevel[x, H - 2] = tuple((a + b) // 2 for a, b in zip(dark, bevel[x, H - 2]))
    for y in range(1, H - 1):
        bevel[0, y] = dark
        bevel[W - 1, y] = tuple((a + b) // 2 for a, b in zip(dark, bevel[W - 1, y]))
    small.save(out)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (item, centre, edge, glow, turn) in PETS.items():
        glb = WORKSHOP / item / f"{Path(item).name}.glb"
        if not glb.exists():
            print(f"missing {glb}: build it with tools/asset.sh {Path(item).name}",
                  file=sys.stderr)
            return 1
        # The render is kept beside the model in workshop/ and reused while the model is older,
        # so a change to the composition does not wait on Blender.
        figure = WORKSHOP / item / f"{Path(item).name}_icon_figure.png"
        if not figure.exists() or figure.stat().st_mtime < glb.stat().st_mtime:
            render(glb, figure, turn)
        compose(figure, centre, edge, glow, OUT / f"buff_{name}.png", LEAN.get(name, 0.0))
        print(f"wrote {OUT / f'buff_{name}.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
