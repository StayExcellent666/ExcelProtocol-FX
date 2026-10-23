"""Generate the original layered PCM sound pack used by Raid Battle."""

from __future__ import annotations

from array import array
import math
from pathlib import Path
import random
import wave

RATE = 44100
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "data" / "sounds"


def buffer(seconds: float) -> tuple[list[float], list[float]]:
    count = round(seconds * RATE)
    return [0.0] * count, [0.0] * count


def envelope(t: float, duration: float, attack: float = .012, release: float = .12) -> float:
    return min(1.0, t / max(attack, 1e-6), (duration - t) / max(release, 1e-6)) ** 1.35


def tone(channels, start: float, duration: float, f0: float, f1: float | None = None,
         gain: float = .3, shape: str = "sine", pan: float = 0.0, vibrato: float = 0.0) -> None:
    left, right = channels
    begin, end = round(start * RATE), min(len(left), round((start + duration) * RATE))
    phase = 0.0
    for index in range(begin, end):
        t = (index - begin) / RATE
        progress = t / duration
        frequency = f0 * ((f1 or f0) / f0) ** progress
        if vibrato:
            frequency *= 1.0 + .012 * math.sin(2 * math.pi * vibrato * t)
        phase += 2 * math.pi * frequency / RATE
        if shape == "saw":
            sample = 2 * ((phase / (2 * math.pi)) % 1.0) - 1
        elif shape == "square":
            sample = 1.0 if math.sin(phase) >= 0 else -1.0
        elif shape == "pulse":
            sample = 1.0 if (phase / (2 * math.pi)) % 1.0 < .2 else -.35
        else:
            sample = math.sin(phase)
        sample *= gain * envelope(t, duration)
        left[index] += sample * math.sqrt((1 - pan) / 2)
        right[index] += sample * math.sqrt((1 + pan) / 2)


def noise(channels, start: float, duration: float, gain: float, seed: int,
          pan: float = 0.0, color: float = .25) -> None:
    left, right = channels
    rng = random.Random(seed)
    begin, end = round(start * RATE), min(len(left), round((start + duration) * RATE))
    state = 0.0
    for index in range(begin, end):
        t = (index - begin) / RATE
        raw = rng.uniform(-1, 1)
        state += color * (raw - state)
        sample = state * gain * envelope(t, duration, .003, duration * .65)
        left[index] += sample * math.sqrt((1 - pan) / 2)
        right[index] += sample * math.sqrt((1 + pan) / 2)


def echo(channels, delay: float = .09, decay: float = .25) -> None:
    left, right = channels
    offset = round(delay * RATE)
    source_l, source_r = left[:], right[:]
    for index in range(offset, len(left)):
        left[index] += source_r[index - offset] * decay
        right[index] += source_l[index - offset] * decay


def save(name: str, channels, target_peak: float = .66, compress: bool = False) -> None:
    left, right = channels
    if compress:
        # Music has many sustained layers. Gentle saturation raises its body and
        # keeps isolated drum transients from forcing the whole song quiet.
        left[:] = [math.tanh(value * .9) for value in left]
        right[:] = [math.tanh(value * .9) for value in right]
    peak = max(.001, *(abs(value) for value in left), *(abs(value) for value in right))
    # Leave generous headroom in every asset. Runtime mixing adds a second
    # limiter for overlapping cues, but quiet source material prevents even a
    # single cue from sounding sharp at the default master volume.
    scale = target_peak / peak
    samples = array("h")
    for lvalue, rvalue in zip(left, right):
        samples.append(round(max(-1, min(1, lvalue * scale)) * 32767))
        samples.append(round(max(-1, min(1, rvalue * scale)) * 32767))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUTPUT / f"{name}.wav"), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(samples.tobytes())


def weapon_shot(kind: str, number: int) -> None:
    """Give every weapon a crisp transient and recognizable low-register body."""
    if kind == "blaster":
        sound = buffer(.34)
        base = (285, 330)[number - 1]
        tone(sound, 0, .22, base * 1.8, base * .55, .48, "saw", -.18)
        tone(sound, 0, .16, 92, 54, .42, "sine", .12)
        tone(sound, .004, .055, 1850, 690, .19, "square", -.08)
        noise(sound, 0, .105, .3, 100 + number, color=.78)
        echo(sound, .07, .16)
    elif kind == "plasma":
        sound = buffer(.38)
        base = (175, 205)[number - 1]
        tone(sound, 0, .3, base, base * 1.62, .43, "sine", -.2, 18)
        tone(sound, .018, .28, base * .72, base * 1.18, .32, "saw", .22, 9)
        tone(sound, 0, .08, 1280, 360, .18, "pulse")
        noise(sound, 0, .16, .23, 120 + number, color=.54)
        echo(sound, .085, .21)
    elif kind == "railgun":
        sound = buffer(.5)
        base = (82, 96)[number - 1]
        tone(sound, 0, .31, base, base * 3.2, .4, "sine", -.28, 7)
        tone(sound, .12, .25, base * 3.4, base * .64, .54, "saw", .25)
        tone(sound, .115, .07, 2200, 410, .24, "square")
        noise(sound, .11, .13, .38, 140 + number, color=.82)
        echo(sound, .095, .26)
    else:  # Ban Hammer
        sound = buffer(.58)
        base = (52, 61)[number - 1]
        tone(sound, 0, .44, base * 1.6, base * .54, .7, "sine", -.08)
        tone(sound, 0, .27, base * 5.2, base * 1.5, .42, "square", .14)
        tone(sound, 0, .055, 1450, 270, .22, "saw")
        noise(sound, 0, .2, .46, 160 + number, color=.12)
        noise(sound, .045, .26, .22, 170 + number, color=.62)
        echo(sound, .12, .2)
    save(f"raid-shot-{kind}-{number}", sound, target_peak=.78)


def enemy_hit(number: int) -> None:
    sound = buffer(.3)
    base = (105, 132, 86)[number - 1]
    tone(sound, 0, .19, base * 2.4, base, .5, "sine", (-.3, .2, .4)[number - 1])
    noise(sound, 0, .14, .48, 200 + number, color=.12)
    tone(sound, .03, .12, 330 + number * 45, 145, .13, "square")
    save(f"raid-enemy-hit-{number}", sound)


def enemy_death(number: int) -> None:
    sound = buffer(.72)
    base = (235, 275, 190)[number - 1]
    tone(sound, 0, .52, base * 2.2, 65 + number * 8, .5, "saw", -.2, 11)
    tone(sound, .07, .5, base, 48, .42, "sine", .25)
    for burst in range(5):
        noise(sound, .08 + burst * .075, .13, .24, 300 + number * 10 + burst, pan=(-.6 + burst * .3), color=.7)
    echo(sound, .105, .28)
    save(f"raid-enemy-death-{number}", sound)


def enemy_spawn(number: int) -> None:
    sound = buffer(.75)
    base = (68, 84)[number - 1]
    tone(sound, 0, .65, base, base * 3.4, .33, "saw", (-.25, .25)[number - 1], 7)
    tone(sound, .2, .42, base * 1.6, base * 3.8, .28, "sine", .2)
    noise(sound, .05, .52, .2, 400 + number, color=.08)
    echo(sound, .12, .27)
    save(f"raid-enemy-spawn-{number}", sound)


def synthwave_battle() -> None:
    """Build a melodic, seamless eight-bar synthwave loop at 120 BPM."""
    music = buffer(16.0)
    beat = .5
    # E minor, C major, G major, D major. Two passes make an eight-bar loop.
    chords = (
        (82.41, (164.81, 196.00, 246.94)),
        (65.41, (130.81, 164.81, 196.00)),
        (98.00, (196.00, 246.94, 293.66)),
        (73.42, (146.83, 185.00, 220.00)),
    )
    for bar in range(8):
        start = bar * 4 * beat
        root, chord = chords[bar % len(chords)]

        # Wide analog pads establish a clear four-chord progression.
        for note_index, note in enumerate(chord):
            tone(music, start, 1.94, note, note * 1.002, .05, "saw",
                 -.55 + note_index * .55, 3.5 + note_index)
            tone(music, start, 1.96, note / 2, note / 2, .06, "sine",
                 .45 - note_index * .45)

        # A strong octave bass pulse and one-bar neon arpeggio define the groove.
        for step in range(4):
            at = start + step * beat
            bass = root * (2 if step == 3 else 1)
            tone(music, at, .42, bass, bass * .985, .25, "saw", -.08)
            tone(music, at, .45, root / 2, root / 2, .24, "sine", .08)
        arp = (chord[0], chord[1], chord[2], chord[1], chord[0] * 2, chord[2], chord[1], chord[2])
        for step in range(8):
            note = arp[step]
            tone(music, start + step * beat / 2, .19, note, note * 1.01,
                 .075, "pulse", -.62 + (step % 4) * .41)

        # Four-on-the-floor kick, gated snare and alternating hats.
        for step in range(4):
            at = start + step * beat
            tone(music, at, .22, 96, 43, .66, "sine")
            noise(music, at, .055, .2, 700 + bar * 8 + step, color=.72)
            if step in (1, 3):
                noise(music, at, .19, .42, 800 + bar * 8 + step, color=.2)
                tone(music, at, .13, 185, 118, .18, "square")
            noise(music, at + beat / 2, .07, .15, 900 + bar * 8 + step,
                  pan=-.35 if step % 2 else .35, color=.86)

        # The lead is present from the first bar, so the loop reads as music
        # immediately instead of as an ambient bed beneath combat sounds.
        hooks = (
            (329.63, 392.00, 493.88, 392.00, 329.63, 293.66, 329.63, 392.00),
            (329.63, 392.00, 523.25, 493.88, 392.00, 329.63, 293.66, 329.63),
            (293.66, 392.00, 493.88, 587.33, 493.88, 392.00, 293.66, 246.94),
            (369.99, 440.00, 587.33, 440.00, 369.99, 329.63, 293.66, 220.00),
        )
        hook = hooks[bar % len(hooks)]
        for index, note in enumerate(hook):
            at = start + .08 + index * beat / 2
            tone(music, at, .21, note, note * 1.003, .24, "saw", .2, 4.5)
            tone(music, at, .23, note / 2, note / 2, .13, "sine", -.2)

    echo(music, .125, .12)
    save("raid-battle-music", music, target_peak=.84, compress=True)


def main() -> None:
    start = buffer(1.45)
    noise(start, 0, .6, .2, 10, color=.04)
    for index, note in enumerate((73.42, 110, 146.83, 220, 293.66)):
        tone(start, .12 + index * .18, .5, note, note * 1.015, .27, "saw", -.35 + index * .18)
        tone(start, .12 + index * .18, .58, note * 1.5, note * 1.515, .15, "sine", .35 - index * .15)
    echo(start, .14, .3); save("raid-start", start)

    synthwave_battle()

    for kind in ("blaster", "plasma", "railgun", "hammer"):
        for number in range(1, 3): weapon_shot(kind, number)
    for number in range(1, 4): enemy_hit(number)
    for number in range(1, 4): enemy_death(number)
    for number in range(1, 3): enemy_spawn(number)

    alert = buffer(.95)
    for at in (0, .42):
        tone(alert, at, .35, 130, 96, .42, "saw")
        tone(alert, at, .35, 260, 192, .2, "square")
    noise(alert, 0, .9, .09, 20, color=.08); echo(alert, .08, .18); save("raid-alert", alert)

    unlock = buffer(.9)
    for index, note in enumerate((146.83, 220, 293.66, 440)):
        tone(unlock, index * .115, .42, note, note * 1.02, .32, "sine", -.4 + index * .27)
        tone(unlock, index * .115, .22, note * 1.5, note * 1.58, .1, "pulse")
    echo(unlock, .13, .3); save("raid-unlock", unlock)

    fire = buffer(1.0)
    noise(fire, 0, .95, .5, 30, color=.035)
    for index in range(10):
        noise(fire, index * .085, .12, .2, 31 + index, pan=random.Random(index).uniform(-.8, .8), color=.75)
    tone(fire, 0, .85, 72, 118, .34, "saw"); save("raid-fire", fire)

    ice = buffer(1.15)
    for index, note in enumerate((329.63, 440, 523.25, 659.25)):
        tone(ice, index * .08, .72, note, note * .94, .25, "sine", -.55 + index * .35, 5)
    noise(ice, .02, .42, .12, 40, color=.75); echo(ice, .16, .38); save("raid-ice", ice)

    victory = buffer(2.35)
    notes = (130.81, 164.81, 196, 261.63, 329.63, 392)
    for index, note in enumerate(notes):
        tone(victory, .08 + index * .19, .82, note, note * 1.006, .25, "saw", -.45 + index * .18)
        tone(victory, .08 + index * .19, .9, note * 1.5, note * 1.506, .17, "sine", .45 - index * .16)
    for note in (130.81, 164.81, 196, 261.63): tone(victory, 1.27, .95, note, note, .2, "sine")
    noise(victory, 1.2, .65, .1, 50, color=.2); echo(victory, .17, .32); save("raid-victory", victory)

    failure = buffer(1.7)
    for index, note in enumerate((246.94, 207.65, 174.61, 123.47)):
        tone(failure, index * .25, .72, note, note * .72, .34, "saw", .25 - index * .18)
        tone(failure, index * .25, .7, note / 2, note / 2.2, .3, "sine")
    noise(failure, .7, .85, .18, 60, color=.05); echo(failure, .13, .25); save("raid-failure", failure)


if __name__ == "__main__":
    main()
