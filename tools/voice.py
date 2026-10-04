"""A quest giver's voice: his pages in src/sim/quests.cpp read aloud, one WAV a dialog page.

    tools/voice.py marlon                 every page
    tools/voice.py marlon --page offer    one page

Writes source/voice/<voice>/<voice>_{offer,underway,handin,resting}.wav, which tools/sync.sh
copies to assets/voice, where the quest window plays them (Desk, QuestRow::voice). The words
are read out of quests.cpp itself, so what he says and what the window shows are one text.

The model is Chatterbox (Resemble AI, MIT), local, chosen 2026-09-29 for its `exaggeration`:
the user wanted him dramatic, a man asking for help, and Kokoro read him flat. Each giver's
voice is cloned from a line Kokoro-82M (Apache 2.0) read, in source/voice/ref, and read to the
settings in VOICES. No reverb on Marlon: the user heard one on his first take and called it
weird. Peia, low and mystical, carries a faint echo, and Devin the same one.

Needs its own Python, which this repo does not carry:

    uv venv -p 3.11 ~/.cache/mu2-voice
    uv pip install -p ~/.cache/mu2-voice/bin/python chatterbox-tts faster-whisper "setuptools<81"
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
REFS = ROOT / "source" / "voice" / "ref"
# Every pause in a raw line held to 0.3 s, before any finish: Chatterbox leaves a second or more at
# a dash or a full stop, and a paragraph's tail, and Peia's first takes stood 4.3 s silent in the
# middle of her thanks (the user, 2026-09-29: "pretty big pauses"). Her hand-in went 28.7 s to 21.6.
SQUEEZE = ("silenceremove=start_periods=1:start_threshold=-45dB:stop_periods=-1:"
           "stop_duration=0.3:stop_threshold=-45dB:stop_silence=0.3,")
POLISH = ("bass=g=2:f=140,acompressor=threshold=0.15:ratio=2.5:attack=10:release=200,"
          "apad=pad_dur=0.4,loudnorm=I=-16:TP=-1.5:LRA=11")
# Each giver's reading: the cloned reference, how dramatic (`exaggeration`, 0.5 the model's even
# reading), how deliberate (`cfg_weight`, lower is slower), and the finish.
VOICES = {
    # Marlon: lower and heroic (the user, 2026-09-29), no longer pleading. Kokoro-82M's bm_lewis
    # (Apache 2.0), 116 Hz median against bm_george's 141, reading a rallying line, so the clone
    # carries the delivery; steadier than the first take's 0.85, a semitone down, more chest.
    "marlon": dict(ref="bm_lewis.wav", exaggeration=0.6, cfg_weight=0.5,
                   polish="asetrate=24000*0.944,aresample=24000,atempo=1.0593,"
                          "bass=g=2:f=110," + POLISH),
    # Peia: low and mystical (the user, 2026-09-29). Kokoro-82M's af_nicole (Apache 2.0), the
    # lowest of six female voices measured -- 156 Hz median against 180 to 220 -- and breathy;
    # read calm rather than pleading, a little slower, taken down a semitone (asetrate 0.94,
    # tempo put back), and a faint echo, which Devin shares.
    "peia": dict(ref="af_nicole.wav", exaggeration=0.4, cfg_weight=0.3,
                 polish="asetrate=24000*0.94,aresample=24000,atempo=1.0638,"
                        "aecho=0.8:0.5:70|140:0.18|0.1," + POLISH),
    # Devin: low, with a Nordic accent, but only a trace (the user, 2026-09-29: the full accent
    # was "to strong", the trace "perfect"). His reference is Chatterbox's multilingual model
    # (ChatterboxMultilingualTTS) reading a heroic line as Swedish, language_id "sv", off
    # bm_lewis, at exaggeration 0.9; cloned here by the English model, the accent survives only
    # as colour. devin_sv.wav, a calm read, gave a monotone Devin at any setting. Dramatic, 0.9
    # and 0.3, chosen over 1.1 and 1.3. Two semitones down (asetrate 0.89, tempo put back) and
    # more chest: older than Marlon. And Peia's faint echo, the user's "mystical echo" (2026-09-29);
    # his finished WAVs took it afterwards, echo then loudnorm, since their raw takes are not kept.
    # His pages were NOT read by main(): each paragraph read whole (stitched sentences sounded
    # cropped), seed 11, then the sentence gaps stretched and the paragraphs joined 0.9 s apart
    # -- see docs/devin-quest.md and source/voice/devin/recorded_with.py.txt. main() would undo that.
    "devin": dict(ref="devin_sv_dramatic.wav", exaggeration=0.9, cfg_weight=0.3,
                  polish="asetrate=24000*0.89,aresample=24000,atempo=1.1236,"
                         "aecho=0.8:0.5:70|140:0.18|0.1,bass=g=3:f=100," + POLISH),
    # The Golden Archer: a mythical skeleton (the user, 2026-09-30), the second of five finishes
    # tried, "two voices". Kokoro-82M's bm_george (Apache 2.0), read slow and a little dramatic,
    # three semitones down (asetrate 0.84) and a touch slower (atempo 1.10, not 1.19). Under it
    # the same voice an octave lower at 0.45: the undertone. Then two short combs, 11 and 17 ms,
    # for a hollow, boxed ring, and a stone room's tail at 190, 380 and 620 ms.
    "golden_archer": dict(ref="bm_george.wav", exaggeration=0.55, cfg_weight=0.3,
                          polish="asetrate=24000*0.84,aresample=24000,atempo=1.10,asplit[a][b];"
                                 "[b]asetrate=24000*0.595,aresample=24000,atempo=1.681,volume=0.45[o];"
                                 "[a][o]amix=inputs=2:normalize=0,highpass=f=60,"
                                 "aecho=0.8:0.7:11|17:0.30|0.20,"
                                 "aecho=0.8:0.55:190|380|620:0.22|0.12|0.06,"
                                 "acompressor=threshold=0.15:ratio=2.5:attack=10:release=200,"
                                 "apad=pad_dur=0.6,loudnorm=I=-16:TP=-1.5:LRA=11"),
    # Tersia, the Lost Tower's last guard (the user, 2026-10-01). Her giver was first an old man,
    # Senatus, whose voice the user sent back twice -- "to happy, we need more tired old man and
    # scared", "to much echo", "without reverb, and minimal echo" -- and then picked the third of
    # three auditions, "third woman voice was nice", and a woman to match. Her reference is
    # Kokoro-82M's bm_fable (Apache 2.0) reading a frightened, exhausted line at 0.78 speed,
    # source/voice/ref/tersia_fable_scared.wav; cloned at 0.6 and 0.3, seed 11 -- 0.3 read her pages
    # "very monotome, lack of emotion", and of 0.6, 0.85 and 1.1 the user took 0.6. Aged and
    # shaken, not pitched: 8% slower, a tremble in volume and pitch, a thinner bottom, one faint
    # echo at 90 ms. One voice for her seven floors, tersia_1 to tersia_7 (VOICES by the stem).
    # Her sentences ran together under SQUEEZE (the user, 2026-10-02: "little pause between
    # sentences"), so each full stop is widened back to 0.6 s after it (sentence_gap). The same day
    # the user asked for "some other woman voice ... with lower more mystical tembre" and took the
    # second of four auditions, "second one is perfect": Kokoro-82M's bf_isabella (Apache 2.0)
    # reading a hushed shrine line at 0.85 speed, source/voice/ref/tersia_isabella_mystic.wav,
    # cloned at an even 0.5; a semitone and a half down (asetrate 0.917, tempo nearly put back),
    # more chest, and a soft three-tap echo. The bm_fable reference and its tremble are retired.
    "tersia": dict(ref="tersia_isabella_mystic.wav", exaggeration=0.5, cfg_weight=0.3, seed=11,
                   sentence_gap=0.6,
                   polish="asetrate=24000*0.917,aresample=24000,atempo=1.04,highpass=f=70,"
                          "bass=g=3:f=130,aecho=0.8:0.6:80|160|300:0.22|0.14|0.08,"
                          "acompressor=threshold=0.15:ratio=2.5:attack=10:release=200,"
                          "apad=pad_dur=0.3,loudnorm=I=-17:TP=-1.5:LRA=11"),
    # The Archangel, Blood Castle's mythical demigod, and his Messenger at the Devias gate: one
    # voice for both (the user, 2026-10-04, the ninth of thirteen auditions, twice). Kokoro-82M's
    # am_onyx (Apache 2.0) reading a solemn, ancient line at 0.8 speed,
    # source/voice/ref/angel_onyx_solemn.wav, cloned at 1.0 and 0.3, seed 11 -- first 0.55, then
    # "make it more dramatic": of 0.8, 1.0 and 1.2 the user took 1.0. The "throne"
    # finish: three semitones down and tempo put back, a deep chest, and a stone room's tail at
    # 180, 380 and 650 ms. The demigod's halo and the choir's doubled voices were passed over.
    "archangel": dict(ref="angel_onyx_solemn.wav", exaggeration=1.0, cfg_weight=0.3, seed=11,
                      polish="asetrate=24000*0.84,aresample=24000,atempo=1.19,bass=g=4:f=100,"
                             "highpass=f=45,aecho=0.8:0.55:180|380|650:0.24|0.14|0.07,"
                             "acompressor=threshold=0.15:ratio=2.5:attack=10:release=200,"
                             "apad=pad_dur=1.2,loudnorm=I=-16:TP=-1.5:LRA=11"),
}
VOICES["messenger"] = VOICES["archangel"]
# Takes read again on another seed, heard wrong by whisper on the voice's own: at 1.0 the welcome
# said "appreciates me", the wait "injure", the staff's thanks "with" twice, the crossbow's
# "Koon Koon", the sword's "spear of sorcerers". "Saint's grip" blurs on every seed tried.
RETAKES = {("messenger", "none"): 51, ("messenger", "notyet"): 23,
           ("archangel", "done_staff"): 23, ("archangel", "done_crossbow"): 23,
           ("archangel", "nostaff_staff"): 51, ("archangel", "nostaff_sword"): 23}

# Pages that are not a quest row's: the Messenger's and the Archangel's, said in
# src/game/ui/quest_dialog.cpp (gateWords, angelWords), one clip a thing he can say, named as
# Desk asks for it -- the Messenger's only to one with a cloak and on a castle he can go into, so
# his "no cloak" and "not for warriors of your strength" are not voiced.
# A weapon he names is read once for each of the three (WEAPONS).
# lines() checks each against the C++, so an edit there that is not made here is refused.
DIALOG = ROOT / "src" / "game" / "ui" / "quest_dialog.cpp"
WEAPONS = {"staff": "Divine Staff of Archangel", "sword": "Divine Sword of Archangel",
           "crossbow": "Divine Crossbow of Archangel"}
SAID = {
    "messenger": {
        "none": "Your will to help the Archangel is appreciated. But be careful, young warrior "
                "for Blood Castle is a dangerous place. May God be with you.",
        "notyet": "I see that you have the Cloak of Invisibility. But you need to wait till the "
                  "gate opens to enter the Blood Castle.",
    },
    "archangel": {
        "done_{w}": "Ah, my {weapon}! Thanks to your courage, Blood Castle is free of Kundun's "
                    "soldiers once more. Take this as a token of our thanks, and with it what I "
                    "have learned in this long war.",
        "ready_{w}": "You carry my {weapon}! Give it to me, warrior, and Blood Castle is ours "
                     "again.",
        "nostaff_{w}": "My {weapon} is still in the Statue of Saint's grip. Cut down the guards "
                       "until the drawbridge falls, slay the Spirit Sorcerers who hold the door, "
                       "and break the statue. Then bring my weapon to me, before the time runs "
                       "out.",
        "ended": "The time has run out, and Kundun's soldiers hold the castle still. Rest, "
                 "warrior, and come back stronger when the gate opens again.",
        "notyet_{w}": "Kundun's soldiers have taken this castle, and a Statue of Saint holds my "
                      "{weapon} beyond its door. When the gate opens, cut through the guards, "
                      "slay the Spirit Sorcerers and break the statue. Bring my weapon back to "
                      "me, and you will not go unrewarded.",
    },
}


def lines(voice):
    """SAID[voice] as {clip: [words]}, each checked against quest_dialog.cpp's literals."""
    source = re.sub(r'"\s*\n\s*"', "", DIALOG.read_text())  # adjacent literals joined
    found = {}
    for clip, words in SAID[voice].items():
        for piece in words.split("{weapon}"):
            if piece.strip() and piece not in source:
                raise SystemExit(f"voice: {voice} '{clip}' is not what {DIALOG.name} says: "
                                 f"{piece[:60]!r}...")
        if "{w}" in clip:
            for w, weapon in WEAPONS.items():
                found[clip.format(w=w)] = [words.format(weapon=weapon)]
        else:
            found[clip] = [words]
    return found


# Words the model says wrong, spelled as they are said: the window keeps the written form. MU is
# one syllable, "moo" (the user, 2026-09-29: "Moo is correct").
# A dash is read as a comma: the model held a long breath at one. The Messenger's "Continent of
# Mu" is the same word, and his quotes round 'Blood Bone' are the window's, not said.
SPOKEN = {r"\bMU\b": "Moo", r"\bMu\b": "Moo", r"\s*--\s*": ", ", r"(?<=\s)'|'(?=[\s.,])": ""}


def spoken(words):
    for pattern, said in SPOKEN.items():
        words = re.sub(pattern, said, words)
    return words


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


_heard = None


def heard(path):
    """The take's words as faster-whisper hears them, [(word, start, end)]."""
    global _heard
    if _heard is None:
        from faster_whisper import WhisperModel
        _heard = WhisperModel("small.en", device="cpu", compute_type="int8", cpu_threads=3)
    segments, _ = _heard.transcribe(str(path), word_timestamps=True, beam_size=5)
    return [(w.word, w.start, w.end) for s in segments for w in s.words]


def plain(word):
    return re.sub(r"[^a-z0-9]", "", word.lower())


def pace(path, target, words, gap):
    """A raw take's pauses set in one pass, written to `target`: each sentence's end held `gap`
    seconds, cut where the take was heard to say the sentence's last word, every other pause held
    to SQUEEZE's 0.3 s, the ends trimmed. Placed first by letter share, the gap cut Tersia's second
    offer inside "poison" and after "plates"; and ffmpeg's silenceremove and the zeros put in after
    it were hard splices, a click at each (the user, 2026-10-03: "buggy audio"). Every join here
    fades out and in over FADE."""
    import difflib
    import numpy as np
    import soundfile as sf
    x, sr = sf.read(str(path))
    hop, fade = int(0.01 * sr), int(0.015 * sr)
    level = np.array([np.sqrt(np.mean(x[j:j + hop] ** 2)) for j in range(0, len(x), hop)])
    still = 20 * np.log10(level + 1e-9) < -45
    spans, j = [], 0  # the take's quiet runs of 60 ms or more, in samples
    while j < len(still):
        if still[j]:
            k = j
            while k < len(still) and still[k]:
                k += 1
            if k - j >= 6 or j == 0 or k == len(still):
                spans.append([j * hop, min(len(x), k * hop)])
            j = k
        else:
            j += 1

    cuts = []  # where each sentence ends, in samples
    said = [plain(w) for w in words.split()]
    ends = {i for i, w in enumerate(words.split()) if re.search(r"[.!?][\"']?$", w)}
    ends.discard(len(said) - 1)
    if ends:
        take = heard(path)
        match = difflib.SequenceMatcher(None, said, [plain(w) for w, _, _ in take],
                                        autojunk=False)
        at = {a + k: b + k for a, b, n in match.get_matching_blocks() for k in range(n)}
        for i in sorted(ends):
            if i not in at or at[i] + 1 >= len(take):
                print(f"voice: sentence end '{words.split()[i]}' not heard, left as read",
                      file=sys.stderr)
                continue
            stop, start = take[at[i]][2], take[at[i] + 1][1]
            # whisper's word edges are loose: the quietest 20 ms near them, not in a tail
            lo, hi = int((stop - 0.1) * sr), int((start + 0.1) * sr)
            cuts.append(min(range(max(0, lo), min(len(x) - 2 * hop, hi), hop),
                            key=lambda q: np.square(x[q:q + 2 * hop]).mean(),
                            default=int(stop * sr)) + hop)

    # (keep from, keep to, silence after): the take as kept pieces with silence between
    pieces, last = [], 0
    for lo, hi in spans:
        inside = [c for c in cuts if lo <= c <= hi]
        if lo == 0:  # the lead-in: trimmed to 50 ms
            last = max(0, hi - int(0.05 * sr))
            continue
        if hi >= len(x):  # the tail: 150 ms of it kept, the polish pads the rest
            pieces.append((last, min(len(x), lo + int(0.15 * sr)), 0))
            last = None
            break
        hold = gap if inside else 0.3
        have = (hi - lo) / sr
        if have > hold:  # too long: keep its two edges, join them in the quiet
            keep = int(hold / 2 * sr)
            pieces.append((last, lo + keep, 0))
            last = hi - keep
        elif inside:  # a sentence's end too short: silence put in its quietest place
            pieces.append((last, inside[0], int((hold - have) * sr)))
            last = inside[0]
        for c in inside:
            cuts.remove(c)
    for c in sorted(cuts):  # a sentence end read straight through, with no quiet run at all
        if last is not None and c > last:
            pieces.append((last, c, int(gap * sr)))
            last = c
    if last is not None:
        pieces.append((last, len(x), 0))

    ramp = np.linspace(0, 1, fade)
    out = []
    for lo, hi, rest in pieces:  # the take's own two ends faded too: its tail was a cut
        y = x[lo:hi].copy()
        if len(y) > 2 * fade:
            y[:fade] *= ramp
            y[-fade:] *= ramp[::-1]
        out += [y, np.zeros(rest)]
    sf.write(str(target), np.concatenate(out), sr)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("voice")
    parser.add_argument("--page", help="offer, underway, handin or resting; or a SAID clip")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--seed", type=int, help="another take of a page that came out wrong")
    args = parser.parse_args()

    wanted = lines(args.voice) if args.voice in SAID else pages(args.voice)
    if not wanted:
        print(f"voice: no quest in {QUESTS.name} has row.voice = \"{args.voice}\"", file=sys.stderr)
        return 1
    import torch
    import torchaudio
    from chatterbox.tts import ChatterboxTTS

    # A chain's links share their giver's voice: golden_archer_2 reads as golden_archer.
    how = VOICES.get(args.voice) or VOICES[re.sub(r"_\d+$", "", args.voice)]
    model = ChatterboxTTS.from_pretrained(device=args.device)
    out = ROOT / "source" / "voice" / args.voice
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        for page, paragraphs in wanted.items():
            if args.page and page != args.page:
                continue
            parts = []
            for i, words in enumerate(paragraphs):
                seed = RETAKES.get((args.voice, page), how.get("seed", 7))
                torch.manual_seed(args.seed if args.seed is not None else seed)
                wav = model.generate(spoken(words), audio_prompt_path=str(REFS / how["ref"]),
                                     exaggeration=how["exaggeration"],
                                     cfg_weight=how["cfg_weight"],
                                     temperature=0.8)
                raw = pathlib.Path(scratch) / f"{page}{i}.raw.wav"
                torchaudio.save(str(raw), wav, model.sr)
                part = pathlib.Path(scratch) / f"{page}{i}.wav"
                polish = how["polish"]
                if "sentence_gap" in how:
                    paced = pathlib.Path(scratch) / f"{page}{i}.paced.wav"
                    pace(raw, paced, spoken(words), how["sentence_gap"])
                    raw = paced
                else:
                    polish = SQUEEZE + polish
                subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(raw), "-af", polish,
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
