"""Synthesizes the game's placeholder sound effects as 16-bit mono WAVs.

Run from the repo root:  python Tools/generate_sounds.py
Writes SourceAudio/*.wav, which Tools/import_sounds.py turns into /Game/Audio assets.
Pure standard library, deterministic (fixed random seed).
"""

import math
import os
import random
import struct
import wave

RATE = 44100
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "SourceAudio")


# --- building blocks ---------------------------------------------------------

def silence(seconds):
    return [0.0] * int(seconds * RATE)


def env(t, attack, tau):
    """Linear attack then exponential decay with time constant tau."""
    if t < attack:
        return t / attack
    return math.exp(-(t - attack) / tau)


def sweep(f0, f1, t, length, curve="exp"):
    a = min(t / length, 1.0)
    if curve == "exp":
        return f0 * (f1 / f0) ** a
    return f0 + (f1 - f0) * a


class Osc:
    def __init__(self):
        self.phase = 0.0

    def sine(self, freq):
        self.phase += 2.0 * math.pi * freq / RATE
        return math.sin(self.phase)

    def saw(self, freq):
        self.phase = (self.phase + freq / RATE) % 1.0
        return 2.0 * self.phase - 1.0


class LowPass:
    """One-pole low-pass with a per-sample cutoff."""

    def __init__(self):
        self.y = 0.0

    def __call__(self, x, cutoff):
        a = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
        self.y += a * (x - self.y)
        return self.y


def noise():
    return random.uniform(-1.0, 1.0)


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(n)]


def delay(track, seconds):
    return silence(seconds) + track


def render(length, fn):
    return [fn(i / RATE) for i in range(int(length * RATE))]


def finish(samples, peak=0.9):
    """Normalize, and fade the edges so nothing clicks."""
    top = max(1e-9, max(abs(s) for s in samples))
    out = [s * peak / top for s in samples]
    fade_in, fade_out = int(0.002 * RATE), int(0.01 * RATE)
    for i in range(min(fade_in, len(out))):
        out[i] *= i / fade_in
    for i in range(min(fade_out, len(out))):
        out[-1 - i] *= i / fade_out
    return out


# --- sounds ------------------------------------------------------------------

def hit():
    # Quake 3 style hitsound: a crisp metallic tick.
    a, b = Osc(), Osc()
    return render(0.09, lambda t: (a.sine(2200) + 0.5 * b.sine(3300)) * env(t, 0.001, 0.022))


def kill():
    def tone(freq, length):
        o, h = Osc(), Osc()
        return render(length, lambda t: (o.sine(freq) + 0.3 * h.sine(freq * 3)) * env(t, 0.003, length * 0.5))
    return mix(tone(880, 0.09), delay(tone(1320, 0.18), 0.075))


def mg_fire():
    lp, thump = LowPass(), Osc()
    return render(0.16, lambda t:
                  lp(noise(), sweep(7000, 700, t, 0.1)) * env(t, 0.001, 0.028) * 1.4
                  + thump.sine(sweep(160, 55, t, 0.08)) * env(t, 0.001, 0.03))


def rocket_fire():
    lp, thump = LowPass(), Osc()
    return render(0.55, lambda t:
                  lp(noise(), sweep(1800, 350, t, 0.5)) * env(t, 0.012, 0.17) * 1.6
                  + thump.sine(sweep(95, 45, t, 0.15)) * env(t, 0.002, 0.06) * 0.9)


def rocket_explode():
    lp, sub = LowPass(), Osc()
    crackle = LowPass()

    def sample(t):
        body = lp(noise(), sweep(3200, 180, t, 1.0)) * env(t, 0.003, 0.33) * 2.2
        low = sub.sine(sweep(75, 32, t, 0.6)) * env(t, 0.004, 0.25) * 1.1
        spark = crackle(noise() if random.random() < 0.02 else 0.0, 5000) * env(t, 0.0, 0.25) * 6.0
        return body + low + spark
    return render(1.4, sample)


def rail_fire():
    carrier, mod, hum = Osc(), Osc(), Osc()
    lp = LowPass()

    def sample(t):
        f = sweep(1500, 170, t, 0.65)
        index = 3.0 * env(t, 0.0, 0.18)
        fm = math.sin(carrier.phase + index * mod.sine(f * 1.5))
        carrier.phase += 2.0 * math.pi * f / RATE
        sizzle = (noise() - lp(noise(), 2500)) * env(t, 0.0, 0.08) * 0.5
        buzz = (1.0 if hum.saw(55) > 0 else -1.0) * 0.12 * env(t, 0.01, 0.3)
        return (fm + sizzle + buzz) * env(t, 0.002, 0.26)
    return render(0.9, sample)


def jump():
    lp, o = LowPass(), Osc()
    return render(0.12, lambda t:
                  lp(noise(), 900) * env(t, 0.004, 0.04) * 1.5 + o.sine(sweep(190, 120, t, 0.1)) * env(t, 0.003, 0.03) * 0.4)


def land():
    lp, o = LowPass(), Osc()
    return render(0.22, lambda t:
                  o.sine(sweep(115, 48, t, 0.12)) * env(t, 0.002, 0.05) + lp(noise(), 500) * env(t, 0.001, 0.03) * 1.8)


def footstep():
    lp, o = LowPass(), Osc()
    return render(0.09, lambda t:
                  lp(noise(), 1800) * env(t, 0.001, 0.018) * 1.6 + o.sine(140) * env(t, 0.001, 0.02) * 0.5)


def pain():
    # Synthetic grunt: a filtered, wobbling sawtooth.
    o, lp = Osc(), LowPass()
    return render(0.3, lambda t:
                  lp(o.saw(sweep(135, 85, t, 0.3) * (1 + 0.04 * math.sin(2 * math.pi * 22 * t))), 1100)
                  * env(t, 0.01, 0.11))


def pickup():
    def bell(freq):
        o, h = Osc(), Osc()
        return render(0.4, lambda t: (o.sine(freq) + 0.25 * h.sine(freq * 2.76)) * env(t, 0.002, 0.14))
    return mix(bell(1046.5), delay(bell(1568.0), 0.06))


def weapon_pickup():
    def clack():
        lp = LowPass()
        return render(0.05, lambda t: lp(noise(), 4000) * env(t, 0.0005, 0.012))
    ring = Osc()
    return mix(clack(), delay(clack(), 0.09), render(0.3, lambda t: ring.sine(2700) * env(t, 0.001, 0.04) * 0.3))


def jumppad():
    o = Osc()
    return render(0.5, lambda t:
                  o.sine(sweep(180, 620, t, 0.35) * (1 + 0.06 * math.sin(2 * math.pi * 18 * t))) * env(t, 0.004, 0.2))


def death():
    lp, o = LowPass(), Osc()
    return render(0.7, lambda t:
                  lp(noise(), sweep(1400, 150, t, 0.5)) * env(t, 0.002, 0.2) * 1.8 + o.sine(sweep(90, 38, t, 0.4)) * env(t, 0.003, 0.15))


def spawn():
    oscs = [Osc() for _ in range(3)]
    return render(0.7, lambda t: sum(
        osc.sine(sweep(400 * (k + 1), 1600 * (k + 1), t, 0.5)) / (k + 1) for k, osc in enumerate(oscs))
        * (0.6 + 0.4 * math.sin(2 * math.pi * 14 * t)) * env(t, 0.05, 0.18))


def no_ammo():
    o = Osc()
    return render(0.06, lambda t: noise() * env(t, 0.0003, 0.004) + o.sine(1500) * env(t, 0.0005, 0.008) * 0.6)


def shotgun_fire():
    lp, thump = LowPass(), Osc()
    return render(0.4, lambda t:
                  lp(noise(), sweep(5000, 450, t, 0.25)) * env(t, 0.001, 0.06) * 1.8
                  + thump.sine(sweep(120, 45, t, 0.15)) * env(t, 0.001, 0.07))


def grenade_fire():
    lp, o, click = LowPass(), Osc(), LowPass()
    return render(0.25, lambda t:
                  o.sine(sweep(230, 85, t, 0.12)) * env(t, 0.002, 0.04)
                  + lp(noise(), 1200) * env(t, 0.001, 0.025) * 1.4
                  + click(noise(), 6000) * env(t, 0.0005, 0.004) * 0.6)


def grenade_bounce():
    a, b, lp = Osc(), Osc(), LowPass()
    return render(0.15, lambda t:
                  (a.sine(900) + 0.6 * b.sine(1370)) * env(t, 0.001, 0.03)
                  + lp(noise(), 3000) * env(t, 0.0005, 0.008))


def lightning_fire():
    # Short crackling hum; fired 20 times a second it blends into a continuous buzz.
    o, lp, crackle = Osc(), LowPass(), LowPass()
    return render(0.12, lambda t:
                  (lp(o.saw(90), 3000) * 0.8
                   + crackle(noise() if random.random() < 0.08 else 0.0, 7000) * 5.0)
                  * env(t, 0.005, 0.08))


def plasma_fire():
    carrier, mod = Osc(), Osc()
    return render(0.18, lambda t:
                  carrier.sine(sweep(1800, 600, t, 0.12) * (1 + 0.3 * mod.sine(210))) * env(t, 0.001, 0.05))


def plasma_explode():
    lp, o = LowPass(), Osc()
    return render(0.25, lambda t:
                  lp(noise(), sweep(2500, 400, t, 0.2)) * env(t, 0.001, 0.05) * 1.5
                  + o.sine(sweep(300, 120, t, 0.15)) * env(t, 0.001, 0.04))


def gauntlet_fire():
    o, lp = Osc(), LowPass()
    return render(0.4, lambda t:
                  lp(o.saw(70 * (1 + 0.5 * math.sin(2 * math.pi * 35 * t))), 2500) * env(t, 0.005, 0.15))


# Names match EArenaSound in Source/Arena/Public/ArenaTypes.h.
SOUNDS = {
    "Hit": hit,
    "Kill": kill,
    "MachineGunFire": mg_fire,
    "RocketFire": rocket_fire,
    "RocketExplode": rocket_explode,
    "RailFire": rail_fire,
    "Jump": jump,
    "Land": land,
    "Footstep": footstep,
    "Pain": pain,
    "Pickup": pickup,
    "WeaponPickup": weapon_pickup,
    "JumpPad": jumppad,
    "Death": death,
    "Spawn": spawn,
    "NoAmmo": no_ammo,
    "ShotgunFire": shotgun_fire,
    "GrenadeFire": grenade_fire,
    "GrenadeBounce": grenade_bounce,
    "LightningFire": lightning_fire,
    "PlasmaFire": plasma_fire,
    "PlasmaExplode": plasma_explode,
    "GauntletFire": gauntlet_fire,
}


def write_wav(path, samples):
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, fn in SOUNDS.items():
        random.seed(name)
        samples = finish(fn())
        write_wav(os.path.join(OUT_DIR, f"{name}.wav"), samples)
        print(f"{name}.wav  {len(samples) / RATE:.2f}s")


if __name__ == "__main__":
    main()
