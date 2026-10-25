#!/usr/bin/env python3
"""Item cards for the website, built by the game's own card builder.

    cd MU2_BGFX && nice python3 <scratch>/gen_cards.py

cardx/cards.cpp links src/game/ui/describe.cpp (the item card's builder: it needs nothing from
bgfx or the client, only src/sim) with the sim, the content tables and broker.cpp's npcValue
(the bag's "Sells for" sum), compiled into the scratchpad by cardx/build.sh, and prints every
item's tip::Sheet in a few states. This script turns each Sheet into the rows tip::draw lays
out (src/game/ui/tip.cpp), and writes:

  cards.json        {item: {state: {"reader", "width", "picture", "rows": [...]}}, "_style": ...}
  card_style.json   the card's look, every number from tip.cpp / tip.h / controls.cpp / face.cpp
  card_style.md     the same in words, with CSS equivalents
  card_grain.png    the stone grain tile laid over the card body (controls.cpp makeStone, port)
"""
import json
import math
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path.cwd()
HERE = Path(__file__).resolve().parent
CARDX = HERE / "cardx"
TABLES = ROOT / "out/assets/cooked/lorencia/lorencia.mur"
OPACITY = 0.94  # tip.cpp kOpacity: every ink the card draws is faded by it


def rgba(r, g, b, a=1.0):
    return [round(r * 255), round(g * 255), round(b * 255), round(a, 3)]


def hexof(c):
    return "#%02x%02x%02x" % tuple(c[:3])


TONES = {  # tip.cpp colourOf
    "White": rgba(1, 1, 1), "Blue": rgba(0.5, 0.7, 1.0), "Red": rgba(1.0, 0.2, 0.1),
    "Yellow": rgba(1.0, 0.8, 0.1), "Green": rgba(0.1, 1.0, 0.5), "Gray": rgba(0.588, 0.600, 0.557),
    "Violet": rgba(0.7, 0.4, 1.0), "RedPurple": rgba(0.8, 0.5, 0.8), "Orange": rgba(0.9, 0.42, 0.04),
    "Uncommon": rgba(1, 1, 1), "Rare": rgba(0, 0x70 / 255, 0xdd / 255), "Epic": rgba(0xa3 / 255, 0x35 / 255, 0xee / 255),
    "Legendary": rgba(0x1e / 255, 1.0, 0.0), "Artifact": rgba(1.0, 0xd1 / 255, 0.0), "Quest": rgba(0.84, 0.52, 1.0),
    "RuneLegendary": rgba(1.0, 0x80 / 255, 0.0), "RuneAncient": rgba(0.0, 0xcc / 255, 1.0),
}
INK = {  # tip.h ink:: and tip.cpp's own
    "label": rgba(0.769, 0.757, 0.706), "quiet": rgba(0.588, 0.600, 0.557), "foot": rgba(0.490, 0.502, 0.467),
    "lettering": rgba(0.90, 0.86, 0.76), "good": rgba(0.498, 0.831, 0.545), "bad": rgba(0.886, 0.408, 0.373),
}


def brighter(c):  # tip.cpp brighter(): a third of the way to white (0.35)
    return [round(v + (255 - v) * 0.35) for v in c[:3]] + [c[3]]


def build():
    subprocess.run(["bash", str(CARDX / "build.sh")], check=True, cwd=ROOT)
    out = subprocess.run([str(CARDX / "cards"), str(TABLES)], check=True, capture_output=True, text=True, cwd=ROOT)
    return json.loads(out.stdout)


def rows_of(sheet):
    """The Sheet as tip::draw lays it out, top to bottom (item cards: Sheet::item)."""
    rows = []
    rows.append({"kind": "title", "text": sheet["name"], "tone": sheet["nameTone"]})
    if sheet.get("base"):
        rows.append({"kind": "subtitle", "text": sheet["base"], "tone": sheet["nameTone"]})
    if sheet.get("bound"):
        rows.append({"kind": "bound", "text": sheet["bound"], "tone": sheet["boundTone"]})
    hero = sheet.get("hero")
    if hero:
        rows.append({"kind": "headline", "text": hero["value"], "tone": hero["tone"], "word": hero["word"]})
        for r in hero["rail"]:
            v = (r.get("values") or [{}])[0]
            one = {"kind": "breakdown", "text": r.get("label", "")}
            if v:
                one["figure"] = v["text"]
                if v["tone"] != "White":
                    one["figureTone"] = v["tone"]
            rows.append(one)
    for sec in sheet["sections"]:
        rule = {"kind": "rule"}
        if sec.get("framed"):
            rule["framed"] = True
            if sec.get("kicker"):
                rule["set"] = sec["kicker"]  # not printed on an item card (tip.cpp `kicked`)
        rows.append(rule)
        for r in sec["rows"]:
            if "free" in r or "tail" in r:
                one = {"kind": "option" if r.get("mark") else "prose"}
                for k_from, k_to in (("keyword", "keyword"), ("keywordTone", "keywordTone"), ("free", "text"),
                                     ("tail", "tail"), ("mark", "mark"), ("markTone", "markTone"), ("gap", "gap")):
                    if k_from in r:
                        one[k_to] = r[k_from]
                if r.get("freeTone", "White") != "White":
                    one["tone"] = r["freeTone"]
            else:
                one = {"kind": "stat", "label": r.get("label", "")}
                if r.get("labelTone"):
                    one["labelTone"] = r["labelTone"]
                vals = r.get("values") or []
                if len(vals) == 1 and not vals[0].get("chip"):
                    one["tail"] = vals[0]["text"]
                    if vals[0]["tone"] != "White":
                        one["tailTone"] = vals[0]["tone"]
                else:
                    one["values"] = vals
                if r.get("mark"):
                    one["mark"], one["markTone"] = r["mark"], r["markTone"]
                if r.get("gap"):
                    one["gap"] = True
            if sec.get("framed"):
                one["framed"] = True
            rows.append(one)
    # The foot: two columns, one line each.
    right = []
    if sheet.get("wear"):
        right.append({"kind": "wear", "text": sheet["wear"], "worn": sheet["worn"],
                      **({"ringTone": sheet["wearTone"]} if sheet["wearTone"] != "White" else {})})
    if sheet.get("note"):
        right.append({"kind": "foot_right", "text": sheet["note"], "tone": sheet["noteTone"]})
    for k in sheet.get("keep", []):
        right.append({"kind": "foot_right", "text": k["text"], **({"quiet": True} if k.get("quiet") else
                                                                 {"tone": k["tone"]})})
    if sheet.get("price"):
        right.append({"kind": "foot_right", "text": sheet["price"], "tone": sheet["priceTone"]})
    left = [{"kind": "foot_left", "text": w["text"], **({"quiet": True} if w.get("quiet") else {"tone": w["tone"]})}
            for w in sheet.get("who", [])]
    if left or right:
        rows.append({"kind": "foot", "left": left, "right": right})
    if sheet.get("sell"):
        rows.append({"kind": "sell", "text": sheet["sell"]})
    for r in rows:  # White is the default tone
        if r.get("tone") == "White":
            del r["tone"]
    return rows


def grain_tile(path):
    """controls.cpp makeStone, tile 0 (stone): white noise softened by its neighbours, flecks
    on a 4-texel lattice, mottle on 32, summed, normalised by 3 sd; black where below nought
    (alpha up to 0.24), warm white (255,240,222) where above (alpha up to 0.048)."""
    T = 128
    M = 0xFFFFFFFF

    def h(x, y, seed):
        v = ((x * 0x8da6b343) ^ (y * 0xd8163841) ^ (seed * 0xcb1ab31f)) & M
        v ^= v >> 13
        v = (v * 0x5bd1e995) & M
        v ^= v >> 15
        return v

    def white(x, y, seed):
        return (h(x & (T - 1), y & (T - 1), seed) & 0xFFFF) / 32767.5 - 1.0

    def value(x, y, cell, seed):
        n = T // cell
        fx, fy = x / cell, y / cell
        x0, y0 = math.floor(fx), math.floor(fy)
        tx, ty = fx - x0, fy - y0

        def at(i, j):
            return (h(((i % n) + n) % n, ((j % n) + n) % n, seed) & 0xFFFF) / 32767.5 - 1.0
        sx, sy = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx
        b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx
        return a + (b - a) * sy

    grain, fleck, mottle, dark, light, seed = 0.55, 0.35, 0.25, 0.24, 0.048, 11
    s = []
    for y in range(T):
        for x in range(T):
            g = 0.6 * white(x, y, seed) + 0.1 * (white(x - 1, y, seed) + white(x + 1, y, seed) +
                                                 white(x, y - 1, seed) + white(x, y + 1, seed))
            s.append(grain * g + fleck * value(x, y, 4, seed + 1) + mottle * value(x, y, 32, seed + 2))
    sd = math.sqrt(sum(v * v for v in s) / len(s))
    im = Image.new("RGBA", (T, T))
    px = im.load()
    for i, v in enumerate(s):
        n = max(-1.0, min(1.0, v / (sd * 3)))
        lift = n > 0
        a = max(0, min(255, int((n * light if lift else -n * dark) * 255)))
        px[i % T, i // T] = (255, 240, 222, a) if lift else (0, 0, 0, a)
    im.save(path, optimize=True)


def style():
    u = 1.0
    row = 14.0
    return {
        "reference": "pixels at 1080 screen lines (tip::unit() = screen height / 1080); scale everything by height/1080",
        "opacity": {"value": OPACITY, "note": "every ink the card draws (text, marks, ring, rules, foot) has its alpha x0.94; "
                                              "the body gradient is not faded"},
        "fonts": {
            "title": {"family": "Cinzel", "weight": 500, "file": "Cinzel-Medium.ttf", "google": "Cinzel:wght@500"},
            "text": {"family": "Alegreya Sans", "weight": 500, "file": "AlegreyaSans-Medium.ttf",
                     "google": "Alegreya+Sans:wght@500"},
            "heavy": "the same Alegreya Sans Medium printed twice, the second 0.5 px to the right (max(0.5, 0.45 x unit)): "
                     "a faux bold. On the web: font-weight 700 of Alegreya Sans, or -webkit-text-stroke 0.45px",
            "drop": "every line is printed over its own shadow: 1 px right and 1 px down (max(1, unit)), black at 0.75 alpha; "
                    "text-shadow: 1px 1px 0 rgba(0,0,0,.75)",
        },
        "card": {
            "width": 346, "width_note": "Sheet::wide overrides (0 = 346)", "pad": 12, "radius": 4,
            "body": {"top": [13, 10, 8, 0.92], "foot": [5, 3, 3, 0.90], "note": "a vertical gradient over the whole card, "
                     "rounded 4; tip::ink::kBodyTop / kBodyFoot"},
            "grain": {"png": "card_grain.png", "note": "controls::grain: the 128 px stone tile laid 1:1 over the body inset 1 px "
                      "(a texel a pixel, point-sampled, repeated); its alpha darkens or lifts what is under it"},
            "edge": [
                {"what": "outer seam", "width": 1, "color": [0, 0, 0, 1.0], "radius": 6, "note": "a band from 1 to 2 px outside the body"},
                {"what": "iron ring", "width": 1, "top": [107, 86, 69, round(1.0 * OPACITY, 3)],
                 "foot": [42, 32, 25, round(1.0 * OPACITY, 3)], "radius": 5,
                 "note": "a band from 0 to 1 px outside the body, graded top to foot"},
            ],
            "css": "border-radius:4px; background:linear-gradient(rgba(13,10,8,.92),rgba(5,3,3,.90)); "
                   "box-shadow: 0 0 0 1px (iron gradient: use a 1px border-image linear-gradient(#6b5645f0,#2a2019f0)), "
                   "0 0 0 2px #000, 0 1px 3px rgba(0,0,0,.7), 0 8px 14px rgba(0,0,0,.55), 0 30px 60px rgba(0,0,0,.55)",
            "shadow": {"falls": [{"drop": 1, "sigma": 1.5, "alpha": 0.70}, {"drop": 8, "sigma": 7, "alpha": 0.55},
                                 {"drop": 30, "sigma": 30, "alpha": 0.55}],
                       "note": "three Gaussian falloffs summed and clamped at 0.85, cut away under the card (the card is glass); "
                               "CSS blur = 2 x sigma"},
            "head_tint": {"note": "a band over the head (title+subtitle+bound+2 pads tall), the name's tone at 0.13 x 0.94 alpha at the top "
                                  "fading to clear at its foot; top corners rounded 4"},
            "head_line": {"note": "a 2 px line along the top edge (from y = -1), from x = 4 to width - 4, the name's tone at "
                                  "0.85 x 0.94 alpha in the middle fading to clear at both ends (two linear halves)"},
            "placement": "centred over the hovered item's cells, its bottom 10 px above their top; if no room it drops 10 px "
                         "below, and with room for neither it stands beside (the side with more room); kept 4 px inside the screen",
            "hover": "shown while the pointer rests on an item (never during a drag); the bag turns the hovered model; "
                     "values get a delta against the worn piece (good #7fd48b, bad #e2685f, same #969a8e) at 13 px, right-aligned",
        },
        "picture": {"size": 96, "note": "item card: the item's own 3D picture, square 96 x artScale (0.2-1), at the head's "
                                        "right: right edge 6 px in from the card's right, top 6 px down; the name wraps at "
                                        "width - pad*2 - 0.6 x picture"},
        "kinds": {
            "title": {"font": "title", "size": 17, "line": 21, "case": "uppercase", "letterSpacing": "0.06em",
                      "color": "tone (the item's quality)", "wrap": "at 346 - 24 - 0.6 x picture"},
            "subtitle": {"font": "text", "size": 14, "line": 22, "color": "tone (same as the title)"},
            "bound": {"font": "text", "size": 14, "line": 21, "color": "tone (RedPurple #cc80cc)"},
            "head": {"note": "head height = title lines x 21 + subtitle 22 + bound 21 + 24 (pad above and below); "
                             "the title starts 12 px down, at x 12"},
            "headline": {"font": "text", "heavy": True, "size": 24, "line": 31, "color": "tone",
                         "word": {"size": 15, "color": hexof(INK["label"]), "gap": 8.4}},
            "breakdown": {"font": "text", "size": 13.5, "line": 21, "x": 25, "figure": "heavy, figureTone or the label ink "
                          + hexof(INK["label"]), "text_color": hexof(INK["quiet"]), "gap": 4.7,
                          "bar": {"x": 15, "width": 1, "color": [255, 255, 255, round(0.18 * OPACITY, 3)],
                                  "note": "from 3 px below the first breakdown line's top to 3 px above the last one's foot "
                                          "(0.15 and 0.3 of a 21 px line)"},
                          "after": "12 px under the last breakdown (railPad x 1.5)"},
            "body_top": "the sections start at max(head + headline block, 6 + picture + 4)",
            "rule": {"height": 1, "color": [255, 255, 255, round(0.06 * OPACITY, 3)], "width": "the card's whole width",
                     "note": "each section opens with it, then 8 px of air; each section ends with 8 px of air"},
            "option": {"font": "text", "size": 14, "line": 21, "x": 37,
                       "compose": "keyword + ' · ' + text + ' ' + tail, wrapped together at 297 px (346 - 37 - 12)",
                       "keyword": "keywordTone or white", "text": "tone (default white)",
                       "tail": "heavy, the tone a third of the way to white (35%)",
                       "mark": "in a 16 px gutter at x 12: centre x 20, centre y the line's middle; glyph 8 px (Ring/RingSet 18)"},
            "prose": {"same_as": "option", "note": "a free line with no mark"},
            "stat": {"font": "text", "size": 14, "line": 21, "x": 37, "label": "label ink " + hexof(INK["label"])
                     + " or labelTone", "tail": "right-aligned at width - 12, tailTone (default white)",
                     "values": "several stack a line each, right-aligned; a chip is an outlined box, 11 px, tracking 0.08em"},
            "gap": {"height": 9, "note": "a row with gap gets 9 px of air above it (round(14 x 1.5 x 0.45))"},
            "framed": {"note": "a framed section (a set's block): fill white 0.03, 1 px outline white 0.10, from x 31 to "
                               "width - 6 (textX - 6, textWide + 12), 4 px inside the section's air; its rows indent 6 px more, "
                               "a marked row's mark stands at the frame's left (x 37 + 6 = 43) and its text after 21 px"},
            "foot": {"font": "text", "size": 13, "line": 20, "pad": "8 px above and below", "background": [0, 0, 0, round(0.40 * OPACITY, 3)],
                     "rule": "1 px white 0.06 on its top", "corners": "bottom corners rounded 4",
                     "left": "who lines from x 12: the class line first (Green when he may, Red when not), then each "
                             "requirement (quiet: met, foot ink " + hexof(INK["foot"]) + "; Red: unmet)",
                     "right": "right-aligned at width - 12: the wear (with its ring), the note, keep lines, the price"},
            "wear": {"text_color": hexof(INK["foot"]), "ring": {"radius": 8, "thickness": 3,
                     "centre": "6 px + radius left of the words, y = baseline - 0.33 x 13", "back": [255, 255, 255, round(0.12 * OPACITY, 3)],
                     "arc": "what is left (worn 0-1), from 12 o'clock clockwise, in ringTone or the lettering ink "
                            + hexof(INK["lettering"])}},
            "sell": {"height": 36, "background": [0, 0, 0, round(0.55 * OPACITY, 3)], "rule": [107, 86, 69, round(0.9 * OPACITY, 3)],
                     "label": {"text": "SELLS FOR", "size": 11.96, "letterSpacing": "0.12em", "color": hexof(INK["foot"]), "x": 12},
                     "figure": {"size": 14.56, "heavy": True, "color": hexof(TONES["Yellow"]), "align": "right at width - 12"},
                     "coin": {"height": 18, "gap": 5.4, "note": "MU's Zen coin (bag_zen) to the figure's left"}},
        },
        "marks": {
            "Diamond": {"what": "an upright square (a rolled option)", "size": 8, "svg": "<rect x='-2.48' y='-2.48' width='4.96' height='4.96'/>"},
            "Star": {"what": "four spikes (an excellent option)", "size": 8,
                     "svg": "<polygon points='0,-4 1.28,-1.28 4,0 1.28,1.28 0,4 -1.28,1.28 -4,0 -1.28,-1.28'/>"},
            "Socket": {"what": "the square, hollow", "size": 8},
            "Ring": {"what": "an empty socket: three lozenges (width 0.8 x height)", "size": 18,
                     "svg": "<polygon points='0,-9 7.2,0 0,9 -7.2,0' fill='rgba(0,0,0,.7)'/>"
                            "<polygon points='0,-7.92 6.34,0 0,7.92 -6.34,0' fill='#9e733d'/>"
                            "<polygon points='0,-5.4 4.32,0 0,5.4 -4.32,0' fill='#0d0908'/>"},
            "RingSet": {"what": "a set socket: the Ring with the Rune of Creation's two inks in it, and in game the rune's own "
                                "3D picture over it", "size": 18,
                        "svg": "(Ring) + <polygon points='0,-5.58 4.46,0 0,5.58 -4.46,0' fill='#8c80ff'/>"
                               "<polygon points='0,-3.6 2.88,0 0,3.6 -2.88,0' fill='#ff63b3'/>"},
            "Blade": {"what": "a blade on the diagonal with its guard (section marks on other cards)"},
            "Shield": {"what": "square shoulders down to a point"},
            "Triangle": {}, "Note": {},
            "colour": "markTone, x0.94 alpha; the Ring/RingSet keep their own colours",
        },
        "tones": {k: {"hex": hexof(v), "tail": hexof(brighter(v))} for k, v in TONES.items()},
        "inks": {k: hexof(v) for k, v in INK.items()},
        "quality_ladder": "the name's tone (describe.cpp qualityOf): Rune of Creation RuneLegendary/RuneAncient, "
                          "excellent Green (a legendary green), socketed Rare blue, +7 and jewels Artifact gold, "
                          "luck/skill/option Uncommon (white), else White; quest items Quest violet",
    }


def main():
    raw = build()
    st = style()
    cards = {}
    for key, states in raw.items():
        out = {}
        for name, s in states.items():
            sh = s["sheet"]
            one = {"reader": s["reader"], "rows": rows_of(sh)}
            if sh.get("wide"):
                one["width"] = sh["wide"]
            if sh.get("item"):
                one["picture"] = round(96 * max(0.2, min(1.0, sh.get("artScale", 1.0))), 1)
            out[name] = one
        cards[key] = out
    meta = {"note": "rows top to bottom as tip::draw lays them; tones and kinds resolve through card_style.json "
                    "(tones[tone].hex, kinds[kind]); width 346 unless given; picture = px square at the head's right",
            "reader": "the character the card was built for: his class and level, reborn once (so rune levels are met), meeting exactly what the thing asks "
                      "(requirements print quiet), except the 'unmet' state (level 1, starting stats: red)",
            "states": ["plain", "+0 luck +4", "+9 luck +12", "+11 excellent (bits 0 and 3) N sockets",
                       "wings: +9 luck +12, +13 luck +16 (2nd wings: +life extra)",
                       "jewellery: +4 luck option (powered: three powers)", "+0 unmet (level 1, starting stats)"],
            "tones": {k: v["hex"] for k, v in st["tones"].items()}}
    (HERE / "cards.json").write_text(json.dumps({"_meta": meta, **cards}, separators=(",", ":"), ensure_ascii=False))
    (HERE / "card_style.json").write_text(json.dumps(st, indent=1, ensure_ascii=False))
    grain_tile(HERE / "card_grain.png")
    print(len(cards), "items,", sum(len(v) for v in cards.values()), "cards")


if __name__ == "__main__":
    main()
