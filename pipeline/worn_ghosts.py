"""Ten of the worn slots' ghosts, win_ghost_<slot>.png, rendered from MU's own items.

slot_ghosts.py cuts the ghosts out of MU's empty-slot plates, and the plates are the trouble:
about 46 px of drawing, so the shape comes out ragged and its inside mottled, and at the bag's
faint ink the mottling is what reads (the user, 2026-10-16: "cleaner and not so pixelated?").
Smoothing the cut could not help -- strong enough to take the hairs off, it took the quiver's
arrows and the pet's legs with them -- and S17's own ghosts (InventoryFrame_I1.ozd) are the same
plates again, smaller.

So these are drawn from the built item in workshop/, the way quiver_ghost.py and mount_ghost.py
draw theirs: textured, orthographic, from the side the slot's shape reads from, and written the
same -- the model's own shading as grey in RGB, the coverage in alpha -- at twice the plate's
size, so the bag takes them down rather than up. The pendant, the mount and the quiver keep
theirs (the user: "keep old mount and arrows", "keep old pendant").

The Plate helm and not a horned one: a helm's face is painted into the same sheet as its metal,
so the Dragon Helm comes with a face that no cut by colour takes off cleanly; the Plate's visor
covers it.

    python3 pipeline/worn_ghosts.py            all ten (renders kept beside each model)
    python3 pipeline/worn_ghosts.py helm       one, by its slot name
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
# The folders are tools/paths.py's; appended, so nothing there shadows a module here.
sys.path.append(str(Path(__file__).resolve().parents[1] / "tools"))
from model_icons import BLENDER, WORKSHOP  # noqa: E402
from paths import P  # noqa: E402

OUT = P.CONTENT / "interface"

#: slot: the item, where the camera stands looking at it (x, y, z, the model's axes), the turn
#: in the picture in degrees anticlockwise, and the longer side written -- twice the plate's.
SLOTS = {
    "helm": ("armor/HelmMale10", (0.2, -1.0, 0.1), 0.0, 192),
    "armour": ("armor/ArmorMale01", (0.0, -1.0, 0.0), 0.0, 196),
    "pants": ("armor/PantMale01", (0.0, -1.0, 0.0), 0.0, 196),
    "gloves": ("armor/GloveMale01", (0.0, -1.0, 0.0), 0.0, 168),
    "boots": ("armor/BootMale01", (0.0, -1.0, 0.0), 0.0, 210),
    "wings": ("wings/Wing02", (0.0, -1.0, 0.6), 0.0, 146),
    # The sword leans as the plate's did; the kite shield lies along x and is stood on its point.
    "weapon_left": ("weapons/Sword07", (1.0, 0.0, 0.0), -30.0, 268),
    "weapon_right": ("shields/Shield03", (1.0, 0.0, 0.0), -90.0, 244),
    "pet": ("pets/Helper01", (0.0, -1.0, 0.0), 0.0, 154),
    "ring": ("jewellery/Ring01", (0.0, -1.0, 0.0), 0.0, 82),
}
#: The shading's range in grey: the darkest fold no darker than this, as slot_ghosts.py's
#: FLOOR_TONE, so the shape stays one thing lit from somewhere.
LOW, HIGH = 115, 255
RENDER_SIZE = 768

RENDER = r'''
import bpy, sys, mathutils
glb, out = sys.argv[-5], sys.argv[-4]
view = mathutils.Vector(tuple(float(v) for v in sys.argv[-3:])).normalized()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
# Cut-out cards (wings, the Angel) need their alpha honoured, not blended to grey.
for o in meshes:
    for m in o.data.materials:
        if m and hasattr(m, "surface_render_method"):
            m.surface_render_method = "DITHERED"
lo = mathutils.Vector((1e9, 1e9, 1e9)); hi = -lo
for o in meshes:
    for c in o.bound_box:
        p = o.matrix_world @ mathutils.Vector(c)
        lo = mathutils.Vector(map(min, lo, p)); hi = mathutils.Vector(map(max, hi, p))
centre = (lo + hi) / 2
size = (hi - lo).length
scene = bpy.context.scene
engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items]
scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
scene.render.resolution_x = scene.render.resolution_y = %(size)d
scene.render.film_transparent = True
scene.view_settings.view_transform = "Standard"
scene.world = bpy.data.worlds.new("w"); scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = "ORTHO"; cam.data.ortho_scale = size
cam.location = centre + view * size * 3
cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Z").to_euler()
key = bpy.data.objects.new("key", bpy.data.lights.new("key", "SUN")); key.data.energy = 3.0
key.rotation_euler = (-(view + mathutils.Vector((0.4, 0, 1)))).to_track_quat("-Z", "Z").to_euler()
scene.collection.objects.link(key)
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
'''


def render(glb: Path, out: Path, view) -> None:
    script = out.with_suffix(".py")
    script.write_text(RENDER % {"size": RENDER_SIZE})
    subprocess.run([BLENDER, "--background", "--python", str(script), "--", str(glb), str(out),
                    *(str(v) for v in view)], check=True, stdout=subprocess.DEVNULL)
    script.unlink()


def ghost(slot: str) -> Path:
    item, view, turn, side = SLOTS[slot]
    glb = WORKSHOP / item / f"{Path(item).name}.glb"
    if not glb.exists():
        raise SystemExit(f"missing {glb}: build it with ./mu make {Path(item).name}")
    figure = glb.with_name(f"{Path(item).name}_ghost_{slot}.png")
    if not figure.exists() or figure.stat().st_mtime < glb.stat().st_mtime:
        render(glb, figure, view)
    im = Image.open(figure).convert("RGBA")
    if turn:
        im = im.rotate(turn, resample=Image.BICUBIC, expand=True)
    im = im.crop(im.getchannel("A").point(lambda a: 255 if a > 6 else 0).getbbox())
    fit = (side - 2) / max(im.width, im.height)
    small = im.resize((max(1, round(im.width * fit)), max(1, round(im.height * fit))),
                      Image.LANCZOS)
    rgba = np.array(small).astype(np.float32)
    lum = rgba[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)
    covered = rgba[..., 3] > 128
    lo, hi = np.percentile(lum[covered], [3, 97]) if covered.any() else (0.0, 255.0)
    tone = np.clip((lum - lo) / max(hi - lo, 1.0), 0.0, 1.0) * (HIGH - LOW) + LOW
    alpha = Image.fromarray(rgba[..., 3].astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.5))
    out = np.zeros((small.height + 2, small.width + 2, 4), np.uint8)
    out[1:-1, 1:-1, 0] = out[1:-1, 1:-1, 1] = out[1:-1, 1:-1, 2] = tone.astype(np.uint8)
    out[1:-1, 1:-1, 3] = np.array(alpha)
    target = OUT / f"win_ghost_{slot}.png"
    Image.fromarray(out, "RGBA").save(target)
    print(f"  ghost      {slot:<13} {out.shape[1]}x{out.shape[0]} <- {item}")
    return target


def main() -> int:
    wanted = sys.argv[1:] or list(SLOTS)
    unknown = [s for s in wanted if s not in SLOTS]
    if unknown:
        print(f"no such slot: {', '.join(unknown)} (have {', '.join(SLOTS)})", file=sys.stderr)
        return 1
    for slot in wanted:
        ghost(slot)
    return 0


if __name__ == "__main__":
    sys.exit(main())
