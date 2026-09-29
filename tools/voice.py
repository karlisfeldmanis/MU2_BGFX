"""A quest giver's voice: his pages in src/sim/quests.cpp read aloud, one WAV a dialog page.

    tools/voice.py marlon                 every page
    tools/voice.py marlon --page offer    one page

Writes source/voice/<voice>/<voice>_{offer,underway,handin,resting}.wav, which tools/sync.sh
copies to assets/voice, where the quest window plays them (Desk, QuestRow::voice). The words
are read out of quests.cpp itself, so what he says and what the window shows are one text.

The model is Chatterbox (Resemble AI, MIT), local, chosen 2026-09-29 for its `exaggeration`:
the user wanted him dramatic, a man asking for help, and Kokoro read him flat. Its voice is
cloned from source/voice/ref/bm_george.wav, a line Kokoro-82M (Apache 2.0) read as bm_george.
No reverb: the user heard one on the first take and called it weird.

Needs its own Python, which this repo does not carry:

    uv venv -p 3.11 ~/.cache/mu2-voice
    uv pip install -p ~/.cache/mu2-voice/bin/python chatterbox-tts "setuptools<81"
    ~/.cache/mu2-voice/bin/python tools/voice.py marlon

About a minute of CPU a line on this Mac; MPS was slower. The seed is fixed, so a page read
twice is the same take.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
QUESTS = ROOT / "src" / "sim" / "quests.cpp"
REF = ROOT / "source" / "voice" / "ref" / "bm_george.wav"
EXAGGERATION = 0.85   # 0.5 is the model's even reading; he is pleading
CFG_WEIGHT = 0.35     # lower is slower and more deliberate
POLISH = ("bass=g=2:f=140,acompressor=threshold=0.15:ratio=2.5:attack=10:release=200,"
          "apad=pad_dur=0.6,loudnorm=I=-16:TP=-1.5:LRA=11")


def pages(voice):
    """The quest row whose `row.voice` is `voice`, as {page: [paragraph, ...]}."""
    text = QUESTS.read_text()
    found = {}
    for body in re.split(r"\nQuestRow \w+\(\) \{", text)[1:]:
        if f'row.voice = "{voice}"' not in body:
            continue
        for m in re.finditer(r'row\.(offer|handIn|underway|resting)(?:\[(\d)\])?\s*=\s*'
                             r'((?:\s*"(?:[^"\\]|\\.)*")+);', body):
            words = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(3)))
            words = words.replace('\\"', "").strip()
            page = m.group(1).lower()
            found.setdefault(page, []).append((int(m.group(2) or 0), words))
    return {page: [w for _, w in sorted(lines)] for page, lines in found.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("voice")
    parser.add_argument("--page", choices=("offer", "underway", "handin", "resting"))
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args()

    wanted = pages(args.voice)
    if not wanted:
        print(f"voice: no quest in {QUESTS.name} has row.voice = \"{args.voice}\"", file=sys.stderr)
        return 1
    import torch
    import torchaudio
    from chatterbox.tts import ChatterboxTTS

    model = ChatterboxTTS.from_pretrained(device=args.device)
    out = ROOT / "source" / "voice" / args.voice
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        for page, paragraphs in wanted.items():
            if args.page and page != args.page:
                continue
            parts = []
            for i, words in enumerate(paragraphs):
                torch.manual_seed(7)
                wav = model.generate(words, audio_prompt_path=str(REF),
                                     exaggeration=EXAGGERATION, cfg_weight=CFG_WEIGHT,
                                     temperature=0.8)
                raw = pathlib.Path(scratch) / f"{page}{i}.raw.wav"
                torchaudio.save(str(raw), wav, model.sr)
                part = pathlib.Path(scratch) / f"{page}{i}.wav"
                subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(raw), "-af", POLISH,
                                "-ar", "24000", "-ac", "1", "-sample_fmt", "s16", str(part)],
                               check=True)
                parts.append(part)
                print(f"voice: {page} {i + 1}/{len(paragraphs)}, "
                      f"{wav.shape[-1] / model.sr:.1f} s", flush=True)
            listing = pathlib.Path(scratch) / f"{page}.txt"
            listing.write_text("".join(f"file '{p}'\n" for p in parts))
            target = out / f"{args.voice}_{page}.wav"
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
                            "-i", str(listing), "-c", "copy", str(target)], check=True)
            print(f"voice: {target.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
