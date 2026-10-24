"""Skill and talent icons, the game's own pictures (ported from the prototype's gen_systems_b.py).

Skills: content/interface/skills/skill_<id>.png (the game's cut of MuDream's newui_skill, 80x112),
taken to 46x64. Talents: content/interface/talents/talent_icons.png, cell = icon - 300, 25 a row,
20x28 (src/game/ui/talent_window.cpp's arithmetic), enlarged to 46x64; which icon each talent
wears is the dump's talents.json. Written as icons/<hash>.png, the hash over the source picture,
the cell and this script, so an unchanged icon keeps its name and is not cut again.
"""
import hashlib
from pathlib import Path

from PIL import Image

from common import CACHE, place, source_hash

SIZE = (46, 64)


def _icon_numbers(tree):
    """Every int under an "icon" key anywhere in the dump's talents tree."""
    found = set()
    if isinstance(tree, dict):
        for k, v in tree.items():
            if k == "icon" and isinstance(v, int):
                found.add(v)
            else:
                found |= _icon_numbers(v)
    elif isinstance(tree, list):
        for v in tree:
            found |= _icon_numbers(v)
    return found


def build(src, talents, out_dir, say):
    """Returns {"skills": {id: hash}, "talents": {icon number: hash}}."""
    me = source_hash("icons.py")
    cache = CACHE / "icons"
    cache.mkdir(parents=True, exist_ok=True)
    got = {"skills": {}, "talents": {}}
    made = 0

    def keep(key_bytes, cut):
        nonlocal made
        h = hashlib.sha256(key_bytes + me.encode()).hexdigest()[:16]
        p = cache / f"{h}.png"
        if not p.exists():
            cut().save(p, optimize=True)
            made += 1
        place(p, Path(out_dir) / "icons" / p.name)
        return h

    for f in sorted((src / "content/interface/skills").glob("skill_*.png")):
        sid = f.stem.split("_", 1)[1]
        if not sid.isdigit():
            continue
        got["skills"][int(sid)] = keep(f.read_bytes(),
                                       lambda f=f: Image.open(f).convert("RGBA").resize(SIZE, Image.LANCZOS))
    sheet_file = src / "content/interface/talents/talent_icons.png"
    if talents is not None and sheet_file.exists():
        data = sheet_file.read_bytes()
        sheet = Image.open(sheet_file).convert("RGBA")
        for icon in sorted(_icon_numbers(talents)):
            cell = icon - 300
            if cell < 0:
                continue
            x, y = (cell % 25) * 20, (cell // 25) * 28
            got["talents"][icon] = keep(data + f"|{icon}".encode(),
                                        lambda x=x, y=y: sheet.crop((x, y, x + 20, y + 28)).resize(SIZE, Image.LANCZOS))
    say(f"icons: {len(got['skills'])} skills, {len(got['talents'])} talents ({made} cut)")
    return got
