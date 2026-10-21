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


def save(name: str, channels) -> None:
    left, right = channels
    peak = max(.001, *(abs(value) for value in left), *(abs(value) for value in right))
    scale = .86 / peak
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


def shot(number: int) -> None:
    sound = buffer(.24)
    bases = (510, 660, 420, 790)
    base = bases[number - 1]
    tone(sound, 0, .16, base * 2.1, base, .42, "saw", -.15)
    tone(sound, .012, .19, base, base * .62, .34, "sine", .2)
    noise(sound, 0, .11, .23, 100 + number, color=.6)
    echo(sound, .055, .18)
    save(f"raid-shot-{number}", sound)


def enemy_hit(number: int) -> None:
    sound = buffer(.3)
    base = (105, 132, 86)[number - 1]
    tone(sound, 0, .19, base * 2.4, base, .5, "sine", (-.3, .2, .4)[number - 1])
    noise(sound, 0, .14, .48, 200 + number, color=.12)
    tone(sound, .03, .12, 900 + number * 170, 260, .18, "square")
    save(f"raid-enemy-hit-{number}", sound)


def enemy_death(number: int) -> None:
    sound = buffer(.72)
    base = (390, 470, 310)[number - 1]
    tone(sound, 0, .52, base * 2.2, 65 + number * 8, .5, "saw", -.2, 11)
    tone(sound, .07, .5, base, 48, .42, "sine", .25)
    for burst in range(5):
        noise(sound, .08 + burst * .075, .13, .24, 300 + number * 10 + burst, pan=(-.6 + burst * .3), color=.7)
    echo(sound, .105, .28)
    save(f"raid-enemy-death-{number}", sound)


def enemy_spawn(number: int) -> None:
    sound = buffer(.75)
    base = (110, 145)[number - 1]
    tone(sound, 0, .65, base, base * 5.2, .33, "saw", (-.25, .25)[number - 1], 7)
    tone(sound, .2, .42, base * 2, base * 6, .31, "sine", .2)
    noise(sound, .05, .52, .2, 400 + number, color=.08)
    echo(sound, .12, .27)
    save(f"raid-enemy-spawn-{number}", sound)


def main() -> None:
    start = buffer(1.45)
    noise(start, 0, .6, .2, 10, color=.04)
    for index, note in enumerate((110, 164.81, 220, 329.63, 440)):
        tone(start, .12 + index * .18, .5, note, note * 1.015, .27, "saw", -.35 + index * .18)
        tone(start, .12 + index * .18, .58, note * 2, note * 2.02, .18, "sine", .35 - index * .15)
    echo(start, .14, .3); save("raid-start", start)

    for number in range(1, 5): shot(number)
    for number in range(1, 4): enemy_hit(number)
    for number in range(1, 4): enemy_death(number)
    for number in range(1, 3): enemy_spawn(number)

    alert = buffer(.95)
    for at in (0, .42):
        tone(alert, at, .35, 185, 145, .43, "saw")
        tone(alert, at, .35, 370, 290, .24, "square")
    noise(alert, 0, .9, .09, 20, color=.08); echo(alert, .08, .18); save("raid-alert", alert)

    unlock = buffer(.9)
    for index, note in enumerate((293.66, 440, 587.33, 880)):
        tone(unlock, index * .115, .42, note, note * 1.02, .32, "sine", -.4 + index * .27)
        tone(unlock, index * .115, .22, note * 2, note * 2.1, .12, "pulse")
    echo(unlock, .13, .3); save("raid-unlock", unlock)

    fire = buffer(1.0)
    noise(fire, 0, .95, .5, 30, color=.035)
    for index in range(10):
        noise(fire, index * .085, .12, .2, 31 + index, pan=random.Random(index).uniform(-.8, .8), color=.75)
    tone(fire, 0, .85, 72, 118, .34, "saw"); save("raid-fire", fire)

    ice = buffer(1.15)
    for index, note in enumerate((1046.5, 1318.5, 1568, 2093)):
        tone(ice, index * .08, .72, note, note * .94, .25, "sine", -.55 + index * .35, 5)
    noise(ice, .02, .42, .12, 40, color=.75); echo(ice, .16, .38); save("raid-ice", ice)

    victory = buffer(2.35)
    notes = (261.63, 329.63, 392, 523.25, 659.25, 783.99)
    for index, note in enumerate(notes):
        tone(victory, .08 + index * .19, .82, note, note * 1.006, .25, "saw", -.45 + index * .18)
        tone(victory, .08 + index * .19, .9, note * 2, note * 2.008, .2, "sine", .45 - index * .16)
    for note in (261.63, 329.63, 392, 523.25): tone(victory, 1.27, .95, note, note, .2, "sine")
    noise(victory, 1.2, .65, .1, 50, color=.2); echo(victory, .17, .32); save("raid-victory", victory)

    failure = buffer(1.7)
    for index, note in enumerate((246.94, 207.65, 174.61, 123.47)):
        tone(failure, index * .25, .72, note, note * .72, .34, "saw", .25 - index * .18)
        tone(failure, index * .25, .7, note / 2, note / 2.2, .3, "sine")
    noise(failure, .7, .85, .18, 60, color=.05); echo(failure, .13, .25); save("raid-failure", failure)


if __name__ == "__main__":
    main()
