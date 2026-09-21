"""Doubles a sheet's resolution, keeping MU's drawing and taking only fine detail from a model.

    python3 pipeline/upscale.py \
        source/textures/sword02_npc.png workshop/items/Sword01/sword02_npc_x2.png 0.35

The last resort, and it is used because the alternatives ran out. Sword01's art is
Data/NPC/sword02.OZJ at 173x39, and there is nothing larger of it anywhere in MU's own
data — that was searched once, across 8,472 distinct textures, and the search is not kept
because the answer does not change. The atlas is 2048. No filter invents information and
no folder holds more of it, so anything further has to be generated — and generated
detail is exactly what this project threw out at the start.

*What makes this different from what was thrown out.* The 256x64 sword02 in textures-hd was
a four-times neural upscale allowed to redraw the picture, and it did: it put a faceted
ridge down the blade and brass studs on the grip that MU never painted. The failure was not
that a model was used. It was that the model was allowed to decide what the sword *is*.

So the model's output is split by frequency and only half of it is kept. Downsample the
model's result to the original's size and back up with the same kernel, and what you have
is the part of its output the original could already represent. That part is thrown away
and replaced with the original's own enlargement. What is added back is only what sits
above the original's Nyquist limit — detail too fine for MU's file to have held an opinion
about. The coarse structure is the artist's, texel for texel, and cannot drift.

`detail` says how much of that fine part survives, 0 to 1. It was chosen by looking, in the
engine, at the hilt:

- at 1.0 the guard's rounded ribs become hard rectangular tiles with black outlines — a
  chain, not a guard. That is the old failure returning through a smaller door.
- at 0.35 the same ribs are the same ribs, resolved rather than redrawn, and the grip's
  gold bands clean up without gaining any that MU did not draw.

0.35 is therefore the default and the number this pipeline runs at. It is a small,
bounded, deliberate use of a model, and it is worth being suspicious of every time.

*Two times, not four.* The network multiplies by four; that output is downsampled to the
factor actually wanted, which is better than asking a smaller model for it — the same
argument as supersampling. Two is what 173x39 supports: enough to resolve what is there,
not enough for the model to start composing.
"""

import hashlib
import re
import sys
import urllib.request
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image
from torch import nn

#: The one checkpoint this supports, and where MU2 keeps it: weights/, which is
#: named for what is in it. It was `models` until the project grew player models in
#: it, at which point a folder called models holding neither was a trap.
#: The networks this will run, by the name a profile asks for.
#:
#: Two, and the second one is here because the first is measurably wrong for the ground.
#: Real-ESRGAN's x4plus is trained on photographs, and what it does to a 128-pixel
#: hand-painted tile is smooth it: through the whole pipeline it produced 1.12 times the
#: detail density of a plain Lanczos resize on tileground03. Not twelve percent better than
#: nothing much — twelve percent better than *no model at all*.
#:
#: This note used to say a menu of models is a menu of ways to get this wrong, and that
#: UltraSharp was the one that turned the sword's guard into a chain. Both halves survive
#: measurement. Siax does the same thing to that guard — its ribs come out as hard
#: rectangles with dark outlines, which is a chain — and on tileground03 it recovers the
#: twisted-cord motif that x4plus dissolves into pebbles, at 2.21 times a plain resize.
#:
#: So it is not a menu, it is two answers to two different questions. A ground tile is a
#: metre of stone under a camera and wants its drawing back. A sword hilt is a centimetre of
#: gold at arm's length and wants to be left alone. The profile says which it is; nothing
#: chooses per sheet, and nothing chooses by feel.
MODELS = {
    "photographic": {
        "file": "RealESRGAN_x4plus.pth",
        "sha256": "4fa0d38905f75ac06eb49a7951b426670021be3018265fd191d2125df9d682f1",
        "url": "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/"
               "RealESRGAN_x4plus.pth",
    },
    "drawn": {
        "file": "4x_NMKD-Siax_200k.pth",
        "sha256": "560424d9f68625713fc47e9e7289a98aabe1d744e1cd6a9ae5a35e9957fd127e",
        "url": "https://huggingface.co/uwg/upscaler/resolve/main/ESRGAN/"
               "4x_NMKD-Siax_200k.pth",
    },
}

DEFAULT_MODEL = "photographic"

#: What the network multiplies by. Anything else is reached by resampling its output.
NATIVE = 4

#: Texels of context padded around the sheet before it goes in.
PAD = 16

#: How that context is made up, and it is not one answer for everything.
#:
#: An item sheet is an atlas — a blade in one corner, a leather grip in another — and
#: wrapping feeds the grip's browns into the blade's edge as context, which invents rust
#: along a cutting edge the artist painted clean. Those replicate: "edge".
#:
#: A world sheet is the opposite. tile_ston04 and tile_wood02 are laid repeatedly across a
#: wall, so the texel past the right edge really is the texel at the left edge, and telling
#: the network otherwise makes it resolve a border that the game then puts next to itself.
#: Those wrap.
#:
#: The profiles have carried a `pad` key since they were written; this is where it arrives.
DEFAULT_PAD_MODE = "edge"

#: How much of the model's fine detail survives by default. See the module note.
DEFAULT_DETAIL = 0.65

#: Below this many pixels a sheet gets less of the model's opinion, in proportion.
#:
#: A reconstruction model invents in proportion to how little it was given, and MU's sheets
#: run from 256 square down to 32. At 128 and above there is enough drawn structure that what
#: comes back is mostly the art sharpened; at 32 there are a thousand texels in the whole
#: sheet and the model fills the gap with its own idea of what belongs there.
#:
#: Caught on joint.jpg, the kerb Lorencia's flower beds are edged with: MU draws a soft dark
#: band of rounded stones along its foot, and at full detail the upscale came back with each
#: stone crisply outlined - structure nobody painted, on the one sheet with the least evidence
#: for it. Scaled by size, a 32-square sheet now gets 0.45 of the detail a 128 gets.
CONFIDENT_AT = 128


class ResidualDenseBlock(nn.Module):
    def __init__(self, num_feat: int = 64, num_grow_ch: int = 32) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(num_feat, num_grow_ch, 3, 1, 1)
        self.conv2 = nn.Conv2d(num_feat + num_grow_ch, num_grow_ch, 3, 1, 1)
        self.conv3 = nn.Conv2d(num_feat + (2 * num_grow_ch), num_grow_ch, 3, 1, 1)
        self.conv4 = nn.Conv2d(num_feat + (3 * num_grow_ch), num_grow_ch, 3, 1, 1)
        self.conv5 = nn.Conv2d(num_feat + (4 * num_grow_ch), num_feat, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, inplace=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x1 = self.lrelu(self.conv1(x))
        x2 = self.lrelu(self.conv2(torch.cat((x, x1), 1)))
        x3 = self.lrelu(self.conv3(torch.cat((x, x1, x2), 1)))
        x4 = self.lrelu(self.conv4(torch.cat((x, x1, x2, x3), 1)))
        x5 = self.conv5(torch.cat((x, x1, x2, x3, x4), 1))
        return (x5 * 0.2) + x


class RRDB(nn.Module):
    def __init__(self, num_feat: int, num_grow_ch: int = 32) -> None:
        super().__init__()
        self.rdb1 = ResidualDenseBlock(num_feat, num_grow_ch)
        self.rdb2 = ResidualDenseBlock(num_feat, num_grow_ch)
        self.rdb3 = ResidualDenseBlock(num_feat, num_grow_ch)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return (self.rdb3(self.rdb2(self.rdb1(x))) * 0.2) + x


class RRDBNet(nn.Module):
    """Real-ESRGAN x4plus, written out rather than imported.

    `basicsr` brings a dependency tree that fights with current torch and numpy, and the
    architecture is small enough to state directly. The layer names match the published
    checkpoint's keys, which is what lets the weights load unchanged.
    """

    def __init__(self, num_feat: int = 64, num_block: int = 23, num_grow_ch: int = 32) -> None:
        super().__init__()
        self.conv_first = nn.Conv2d(3, num_feat, 3, 1, 1)
        self.body = nn.Sequential(*[RRDB(num_feat, num_grow_ch) for _ in range(num_block)])
        self.conv_body = nn.Conv2d(num_feat, num_feat, 3, 1, 1)
        self.conv_up1 = nn.Conv2d(num_feat, num_feat, 3, 1, 1)
        self.conv_up2 = nn.Conv2d(num_feat, num_feat, 3, 1, 1)
        self.conv_hr = nn.Conv2d(num_feat, num_feat, 3, 1, 1)
        self.conv_last = nn.Conv2d(num_feat, 3, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, inplace=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        feat = self.conv_first(x)
        feat = feat + self.conv_body(self.body(feat))
        feat = self.lrelu(self.conv_up1(F.interpolate(feat, scale_factor=2, mode="nearest")))
        feat = self.lrelu(self.conv_up2(F.interpolate(feat, scale_factor=2, mode="nearest")))
        return self.conv_last(self.lrelu(self.conv_hr(feat)))


def weights(models: Path, which: str = DEFAULT_MODEL) -> Path:
    """The checkpoint, fetched into weights/ on first use and checked before it is run.

    Inside MU2 like the engine is, and for the same reason: everything this needs lives
    under MU2 or it is not really self-contained. The checksum is not ceremony — this is a
    64 MB binary downloaded over the network and then executed on the machine.
    """
    if which not in MODELS:
        raise SystemExit(f"error: no model called '{which}'; "
                         f"there is {' and '.join(sorted(MODELS))}.")

    said = MODELS[which]
    path = models / said["file"]

    if path.exists():
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest == said["sha256"]:
            return path

        raise SystemExit(
            f"error: {path} does not match its published checksum.\n"
            f"  expected {said['sha256']}\n  actually {digest}\n"
            f"  Delete it and run again to fetch a fresh copy.")

    models.mkdir(parents=True, exist_ok=True)
    print(f"  fetching {said['file']} into weights/ (once, 64 MB)...", file=sys.stderr)

    urllib.request.urlretrieve(said["url"], path)

    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != said["sha256"]:
        path.unlink()
        raise SystemExit("error: the download did not match its checksum — refusing to use it.")

    return path


def modernise(state: dict) -> dict:
    """The original ESRGAN's key naming into the one this file's RRDBNet uses.

    Real-ESRGAN's own releases call a block body.N.rdbJ.convK; everything built on the
    original ESRGAN calls the same weights model.1.sub.N.RDBJ.convK.0. Same architecture,
    same tensors, different spelling — so this is a rename and not a conversion, and a model
    that fails to load after it would have failed to load before it too.
    """
    if any(key.startswith("body.") for key in state):
        return state

    renamed = {}

    for key, value in state.items():
        name = key.replace("model.0.", "conv_first.")
        name = re.sub(r"model\.1\.sub\.(\d+)\.RDB(\d)\.conv(\d)\.0\.",
                      r"body.\1.rdb\2.conv\3.", name)
        name = re.sub(r"model\.1\.sub\.\d+\.(weight|bias)$", r"conv_body.\1", name)

        for was, becomes in (("model.3.", "conv_up1."), ("model.6.", "conv_up2."),
                             ("model.8.", "conv_hr."), ("model.10.", "conv_last.")):
            name = name.replace(was, becomes)

        renamed[name] = value

    return renamed


def load(models: Path, which: str = DEFAULT_MODEL) -> tuple[RRDBNet, torch.device]:
    device = torch.device("mps" if torch.backends.mps.is_available() else "cpu")

    state = torch.load(weights(models, which), map_location="cpu", weights_only=True)
    state = modernise(state.get("params_ema", state.get("params", state)))

    # Read off the checkpoint rather than assumed. x4plus is 23 blocks and the anime release
    # is 6; a model whose depth is guessed loads with strict=True and fails, which is the
    # good outcome, but it fails with a wall of missing keys rather than the one line that
    # says what actually differs.
    blocks = max(int(key.split(".")[1]) for key in state
                 if key.startswith("body.") and key.split(".")[1].isdigit()) + 1

    model = RRDBNet(num_feat=state["conv_first.weight"].shape[0], num_block=blocks,
                    num_grow_ch=state["body.0.rdb1.conv1.weight"].shape[0])
    model.load_state_dict(state, strict=True)
    model.eval().to(device)

    return model, device


def bleed(image: "Image.Image", alpha: "Image.Image") -> "Image.Image":
    """Grows the opaque colour outward into the transparent texels.

    Nothing here is visible in the result — every texel this writes is cut away by the mask
    it was derived from. What it changes is what the network is shown. A cut-out sheet stores
    black under its own transparency, so without this the model is handed a shape with a hard
    dark rim, reconstructs the rim as though it were drawn, and the sharpened dark then bleeds
    back inside the cut wherever a sampler lands near the boundary.
    """
    import numpy as np

    plane = np.asarray(image, dtype=np.float32)
    mask = np.asarray(alpha, dtype=np.float32) > 127.0

    if not mask.any():
        return image

    filled = plane.copy()

    # Six passes of a four-neighbour average over whatever is not yet filled. Enough to
    # cover the pad the network is given plus the margin a mip chain reaches into, and
    # cheap on a sheet this size.
    for _ in range(6):
        known = mask.copy()
        spread = np.zeros_like(filled)
        weight = np.zeros(mask.shape, dtype=np.float32)

        for shift, axis in ((1, 0), (-1, 0), (1, 1), (-1, 1)):
            rolled = np.roll(filled, shift, axis=axis)
            valid = np.roll(known, shift, axis=axis)
            spread += rolled * valid[..., None]
            weight += valid

        grew = (~mask) & (weight > 0)
        filled[grew] = spread[grew] / weight[grew][..., None]
        mask = mask | grew

    return Image.fromarray(filled.round().clip(0, 255).astype("uint8"), "RGB")


@torch.no_grad()
def run(model: RRDBNet, device: torch.device, plane: np.ndarray,
        pad_mode: str = DEFAULT_PAD_MODE) -> np.ndarray:
    """One HxWx3 uint8 plane through the network at 4x, padded and cropped."""
    padded = np.pad(plane, ((PAD, PAD), (PAD, PAD), (0, 0)), mode=pad_mode)

    tensor = torch.from_numpy(padded.astype(np.float32) / 255.0)
    tensor = tensor.permute(2, 0, 1).unsqueeze(0).to(device)

    result = model(tensor).clamp(0, 1).squeeze(0).permute(1, 2, 0).cpu().numpy()

    trim = PAD * NATIVE
    return (result[trim:-trim, trim:-trim] * 255.0).round().astype(np.uint8)


@torch.no_grad()
def single(model: RRDBNet, device: torch.device, plane: np.ndarray,
           pad_mode: str = DEFAULT_PAD_MODE) -> np.ndarray:
    """One pass, keeping whatever the model said including the part it was unsure about."""
    return run(model, device, plane, pad_mode)


@torch.no_grad()
def ensemble(model: RRDBNet, device: torch.device, plane: np.ndarray,
             pad_mode: str = DEFAULT_PAD_MODE) -> np.ndarray:
    """The same plane through the network eight ways, averaged back into one.

    A network is not symmetric. Run a sheet through it, then run the same sheet rotated a
    quarter turn and rotate the answer back, and the two disagree — most on exactly the
    diagonal edges and small features where it is least sure. Those disagreements are the
    part of its output that is invention rather than reconstruction, because the picture
    did not change and the answer did.

    So it is run over all eight ways a square can be laid down — four turns, each with and
    without a mirror — and the results are turned back and averaged. What survives is what
    the model said regardless of which way round the sheet was, which is the part that came
    from the image. What cancels is the rest.

    Eight times the work on a sheet that is 32 by 64. The cost is not the point at this
    size; the variance is.
    """
    total = None

    for turn in range(4):
        for mirror in (False, True):
            fed = np.rot90(plane, turn)
            if mirror:
                fed = np.fliplr(fed)

            got = run(model, device, np.ascontiguousarray(fed), pad_mode)

            if mirror:
                got = np.fliplr(got)
            got = np.rot90(got, -turn)

            total = got.astype(np.float32) if total is None else total + got

    return np.clip(total / 8.0, 0, 255).round().astype(np.uint8)


def keep_structure(
        original: Image.Image, rebuilt: Image.Image, detail: float,
        target: tuple[int, int]) -> Image.Image:
    """MU's drawing, with only the model's above-Nyquist detail added to its brightness.

    The whole safeguard. See the module note for why it is the difference between this and
    the upscale that put studs on the grip.

    The detail goes into luma alone, and MU's colour comes through untouched. A generative
    upscaler drifts hue — it is the part of its output with the least to go on, because
    chroma in a JPEG this small is already subsampled and half guessed, and what it does
    with the little it has is invent a colour that was never painted. Fine relief is a
    brightness variation anyway: a scratch on a plate is a lighter line, not a differently
    coloured one. So the model is allowed to say where the light is and never what colour
    the paint is.
    """
    if detail <= 0:
        return original.resize(target, Image.LANCZOS)

    # Everything is brought to the size actually wanted before anything is combined, and
    # MU's own picture gets exactly one resampling to get there.
    #
    # It used to be built at the network's 4x and then resized down to the factor asked
    # for, which put two Lanczos passes between the source and the result — 256 to 1024 to
    # 768 for a 3x. Each one costs a little sharpness and the second one costs more,
    # because three quarters is not a clean ratio the way a half is. Measured on
    # skin_wizard_01, that alone moved the round trip from 1.2 to 2.1 against a floor of
    # 1.4: the doubled sheet had been more faithful than the tripled one, which is the
    # wrong way round.
    base = original.resize(target, Image.LANCZOS)

    # What the model produced that the original could already have said. Band-limited by
    # a round trip through the original's own resolution, then brought to the target.
    # Band-limited by a round trip through the original's own resolution. An area average
    # was tried here — it is the decimation the original's grid implies, and 4x to 1x is an
    # exact four-by-four box — and it measured worse, 2.21 against Lanczos's 2.05. The
    # stopband is not what is leaking.
    coarse = rebuilt.resize(original.size, Image.LANCZOS).resize(target, Image.LANCZOS)
    rebuilt = rebuilt.resize(target, Image.LANCZOS)

    was = np.asarray(base.convert("YCbCr"), dtype=np.float32)
    model = np.asarray(rebuilt.convert("YCbCr"), dtype=np.float32)
    smooth = np.asarray(coarse.convert("YCbCr"), dtype=np.float32)

    out = was.copy()
    out[..., 0] = was[..., 0] + (detail * (model[..., 0] - smooth[..., 0]))

    return Image.fromarray(
        np.clip(out, 0, 255).astype(np.uint8), "YCbCr").convert("RGB")


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 upscale.py <in.png> <out.png> [detail] [factor] [pad] "
              "[model] [passes]")
        return

    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    detail = float(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_DETAIL
    factor = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    pad_mode = sys.argv[5] if len(sys.argv) > 5 else DEFAULT_PAD_MODE
    which = sys.argv[6] if len(sys.argv) > 6 else DEFAULT_MODEL

    # One pass or eight, and it is a real choice rather than a speed setting.
    #
    # The self-ensemble averages the sheet over all eight ways a square can be laid down, so
    # what survives is what the model said whichever way round it was — the reconstruction —
    # and what cancels is the invention. That is the right instinct and it is not free: with
    # the drawn model on a ground tile it takes the result from 2.21 times a plain resize
    # down to 1.56. It is cancelling reconstruction along with invention, because on art
    # this small the two are not cleanly separable by rotation.
    #
    # So the ground takes one pass and lives with the invention; an item takes eight and
    # gives up a third of the detail to be sure of what it is looking at.
    passes = int(sys.argv[7]) if len(sys.argv) > 7 else 8

    if pad_mode not in ("edge", "wrap"):
        print(f"  unknown pad '{pad_mode}', using {DEFAULT_PAD_MODE}")
        pad_mode = DEFAULT_PAD_MODE

    opened = Image.open(source)

    # The alpha channel is carried, and this used to be thrown away here.
    #
    # `.convert("RGB")` was harmless while everything went through a bake: transfer_albedo
    # re-derives the alpha from MU's own sheet in a second pass, so whatever this pass did
    # with it was overwritten. A tiled asset has no bake and ships this file, so the alpha
    # that leaves here is the alpha the engine tests — and dropping it turned Stone01's
    # thirteen grass tufts into thirteen olive rectangles.
    #
    # Kept out of the network. It is a shape rather than a picture: a mask has no texture to
    # reconstruct and a model asked to invent detail in one invents a ragged edge. Resampled
    # instead, which is what an enlargement of a shape is.
    alpha = opened.getchannel("A") if "A" in opened.getbands() else None

    if alpha is not None and alpha.getextrema()[0] >= 250:
        alpha = None

    image = opened.convert("RGB")

    # And the colour under the transparent part is filled in before the network sees it.
    #
    # MU leaves whatever it likes there — usually black — and to the model that is a hard
    # dark border running round the tuft, which it faithfully reconstructs and sharpens. The
    # result bleeds back inside the cut when the sheet is sampled at the mask's edge. Grown
    # outward from the opaque texels instead, so the picture simply continues past its own
    # boundary the way the pad at the sheet's border does.
    if alpha is not None:
        image = bleed(image, alpha)

    # How much of the model's opinion this sheet has earned. See CONFIDENT_AT: the less art
    # there is, the more of what comes back is invention rather than reconstruction.
    smallest = min(opened.width, opened.height)
    trust = min(1.0, max(0.45, smallest / CONFIDENT_AT))
    detail = detail * trust

    print(f"\n=== {source.name} -> {destination.name} ===")
    print(f"  source     {image.width} x {image.height}")

    if trust < 1.0:
        print(f"  detail     scaled to {trust:.2f} of it — {smallest} px is under {CONFIDENT_AT}")

    model, device = load(Path(__file__).resolve().parent.parent / "weights", which)
    print(f"  model      {which} ({MODELS[which]['file']}) on {device}, "
          f"{passes} pass{'' if passes == 1 else 'es'}")
    print(f"  context    {PAD} texels, {pad_mode}")

    big = Image.fromarray(
        (ensemble(model, device, np.asarray(image, dtype=np.uint8), pad_mode) if passes > 1
         else single(model, device, np.asarray(image, dtype=np.uint8), pad_mode)), "RGB")

    # Down from the network's four to the factor actually wanted, inside the blend rather
    # than after it. Averaging its last octave away is where the least confident of its
    # invention goes — and three throws away less of the reconstruction than two did, which
    # is most of why three is worth asking for at all.
    out = keep_structure(
        image, big, detail, (image.width * factor, image.height * factor))

    if alpha is not None:
        out = out.convert("RGBA")
        out.putalpha(alpha.resize(out.size, Image.LANCZOS))
        print(f"  alpha      carried, {alpha.size[0]} -> {out.size[0]} by resample")

    destination.parent.mkdir(parents=True, exist_ok=True)
    out.save(destination, "PNG", optimize=True)

    print(
        f"  detail     {detail}  (coarse structure is MU's, texel for texel)\n"
        f"  factor     {factor}x   ->   {out.width} x {out.height}\n"
        f"  wrote      {destination} ({destination.stat().st_size // 1024} KB)")


# Guarded, so this file can be imported.
#
# It ran main() on import, which meant anything that wanted `run` or `keep_structure` — a
# bench comparing two models, say — got a usage message and whatever argv it happened to be
# holding parsed as a detail figure. Three separate test harnesses today came back with a
# blank image and a plausible-looking zero before that was noticed.
if __name__ == "__main__":
    main()
