#!/usr/bin/env python3
"""Copies MU2's build/ into assets/ here, by what index.json reaches.

Three rules, MU2's own (its docs/environments.md, where they replaced a hand-written copy
list that shipped a town with no attribute grid and no sound):

  1. every string in index.json that names a file under build/;
  2. every bare file name a copied .json names beside itself — a world's grids, a
     ground_surfaces.json's sheets and its normal and roughness maps;
  3. every image a copied .glb reaches for by URI, and the .actions.glb clip library a
     model finds by convention.

Rule 2 is applied to the files rule 1 brought, and rule 3 to the models, until nothing new
is found, because a json can name a json.

Nothing here cooks: this is a copy. tools/cook.py (sprint 3) turns the result into what the
engine loads.
"""

import argparse
import json
import os
import shutil
import struct
import sys

IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".tga", ".ktx", ".dds")


def walk_strings(node):
    """Every string anywhere in a decoded json, keys included."""
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for k, v in node.items():
            yield k
            yield from walk_strings(v)
    elif isinstance(node, list):
        for v in node:
            yield from walk_strings(v)


def glb_uris(path):
    """The URIs a .glb's json chunk names. Read without a glTF library: the header is 12
    bytes, then chunks of (length, type, data), and the first chunk is the json."""
    out = []
    try:
        with open(path, "rb") as f:
            magic, _version, _length = struct.unpack("<III", f.read(12))
            if magic != 0x46546C67:  # 'glTF'
                return out
            chunk_len, chunk_type = struct.unpack("<II", f.read(8))
            if chunk_type != 0x4E4F534A:  # 'JSON'
                return out
            doc = json.loads(f.read(chunk_len).decode("utf-8"))
    except (OSError, ValueError, struct.error) as exc:
        print(f"  ! {path}: {exc}", file=sys.stderr)
        return out
    for image in doc.get("images", []):
        uri = image.get("uri")
        if uri and not uri.startswith("data:"):
            out.append(uri)
    for buf in doc.get("buffers", []):
        uri = buf.get("uri")
        if uri and not uri.startswith("data:"):
            out.append(uri)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--dst", required=True)
    ap.add_argument("--world", action="append", default=[],
                    help="restrict the copy to these worlds (their folder under world/)")
    ap.add_argument("--only-world", action="store_true",
                    help="with --world: skip everything that is not under that world")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    src, dst = os.path.abspath(args.src), os.path.abspath(args.dst)
    index_path = os.path.join(src, "index.json")
    if not os.path.isfile(index_path):
        sys.exit(f"no index.json in {src}")

    # Rule 1, seeded with the index itself.
    pending = ["index.json"]
    wanted = set()

    def consider(rel):
        rel = rel.replace("\\", "/").lstrip("./")
        if rel in wanted:
            return
        full = os.path.join(src, rel)
        if not os.path.isfile(full):
            return
        if args.world and args.only_world and rel.startswith("world/"):
            if not any(rel.startswith(f"world/{w}/") for w in args.world):
                return
        wanted.add(rel)
        pending.append(rel)

    while pending:
        rel = pending.pop()
        full = os.path.join(src, rel)
        here = os.path.dirname(rel)
        if rel == "index.json":
            wanted.add(rel)
        if rel.endswith(".json"):
            try:
                with open(full, "r", encoding="utf-8") as f:
                    doc = json.load(f)
            except (OSError, ValueError) as exc:
                print(f"  ! {rel}: {exc}", file=sys.stderr)
                continue
            for s in walk_strings(doc):
                if "/" in s:
                    consider(s)                       # rule 1: a path under build/
                elif "." in s:
                    consider(os.path.join(here, s))   # rule 2: a bare name beside it
        elif rel.endswith(".glb"):
            for uri in glb_uris(full):                # rule 3
                consider(os.path.normpath(os.path.join(here, uri)))
            # and the clip library a model finds by convention
            stem = rel[: -len(".glb")]
            for conv in (stem + ".actions.glb", stem + ".actions.res"):
                consider(conv)

    copied = skipped = 0
    total = 0
    for rel in sorted(wanted):
        s = os.path.join(src, rel)
        d = os.path.join(dst, rel)
        size = os.path.getsize(s)
        total += size
        if os.path.isfile(d) and os.path.getsize(d) == size and \
                os.path.getmtime(d) >= os.path.getmtime(s):
            skipped += 1
            continue
        copied += 1
        if args.dry_run:
            continue
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)

    images = sum(1 for r in wanted if r.endswith(IMAGE_SUFFIXES))
    models = sum(1 for r in wanted if r.endswith(".glb"))
    print(f"{len(wanted)} files ({total / 1e6:.0f} MB): {models} models, {images} images")
    print(f"{copied} copied, {skipped} already here{' (dry run)' if args.dry_run else ''}")


if __name__ == "__main__":
    main()
