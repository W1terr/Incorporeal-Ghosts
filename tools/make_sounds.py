"""Makes the pass-through sounds (res/Sound/FX/IncorporealGhosts/passthrough_0N.wav): an attack sliding through a
ghost. Layered from vanilla sounds - the shimmer of the Become Ethereal shout, a soft blade whoosh and a pitched up
Wispmother breath - with a short reverb tail, kept quieter than a real hit. Mono, 16 bit, 44.1 kHz.

usage: python make_sounds.py   (needs numpy + scipy)
"""
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

import gamefiles

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "res" / "Sound" / "FX" / "IncorporealGhosts"
RATE = 44100

ETHEREAL = r"sound\fx\voc\shout\fx\becomeethereal\in\mag_shout_becomeethereal_in_01.wav"
SWING = r"sound\fx\wpn\swing\blade\medium\fx_swing_blade_medium_0{}.wav"
BREATH = r"sound\fx\npc\wisp\npc_wisp_injured_0{}.wav"

# (file, source start s, source length s, mix start s, gain, pitch factor, lowpass Hz or 0, highpass Hz or 0)
VARIANTS = [
    [(ETHEREAL, 0.05, 0.75, 0.00, 0.60, 1.20, 0, 300), (SWING.format(1), 0.0, 0.40, 0.00, 0.55, 0.80, 1800, 0),
     (BREATH.format(1), 0.0, 0.60, 0.04, 0.30, 1.25, 4000, 200)],
    [(ETHEREAL, 1.25, 0.75, 0.00, 0.55, 1.35, 0, 300), (SWING.format(2), 0.0, 0.40, 0.00, 0.55, 0.75, 1600, 0),
     (BREATH.format(2), 0.0, 0.60, 0.05, 0.30, 1.35, 4000, 200)],
    [(ETHEREAL, 0.10, 0.75, 0.00, 0.60, 1.05, 0, 300), (SWING.format(3), 0.0, 0.40, 0.00, 0.55, 0.85, 2000, 0),
     (BREATH.format(3), 0.0, 0.60, 0.03, 0.30, 1.20, 4000, 200)],
]


def load(name):
    rate, data = wavfile.read(gamefiles.get(name))
    data = data.astype(np.float64)
    if data.ndim > 1:
        data = data.mean(axis=1)
    data /= 32768.0
    if rate != RATE:
        data = signal.resample_poly(data, RATE, rate)
    return data


def layer(name, start, length, gain, pitch, lowpass, highpass):
    data = load(name)[int(start * RATE):int((start + length) * RATE)].copy()
    fade_in = min(len(data), int(0.01 * RATE))
    data[:fade_in] *= np.linspace(0.0, 1.0, fade_in)
    fade = len(data) * 2 // 5
    data[-fade:] *= np.linspace(1.0, 0.0, fade) ** 2
    if pitch != 1.0:
        # faster playback = higher pitch: resample to fewer samples
        up, down = 1000, int(round(1000 * pitch))
        data = signal.resample_poly(data, up, down)
    if lowpass:
        b, a = signal.butter(4, lowpass / (RATE / 2), "low")
        data = signal.lfilter(b, a, data)
    if highpass:
        b, a = signal.butter(2, highpass / (RATE / 2), "high")
        data = signal.lfilter(b, a, data)
    peak = np.max(np.abs(data)) or 1.0
    return data / peak * gain


def reverb(dry, seconds=0.6, wet=0.3, seed=1):
    rng = np.random.default_rng(seed)
    n = int(seconds * RATE)
    impulse = rng.standard_normal(n) * np.exp(-np.linspace(0.0, 6.0, n))
    b, a = signal.butter(2, 3000 / (RATE / 2), "low")
    impulse = signal.lfilter(b, a, impulse)
    tail = signal.fftconvolve(dry, impulse)
    tail /= np.max(np.abs(tail)) or 1.0
    out = np.zeros(len(tail))
    out[:len(dry)] += dry
    return out + tail * wet * np.max(np.abs(dry))


def make(variant, seed):
    length = int(RATE * 1.5)
    mix = np.zeros(length)
    for name, start, src_len, at, gain, pitch, lowpass, highpass in variant:
        data = layer(name, start, src_len, gain, pitch, lowpass, highpass)
        s = int(at * RATE)
        n = min(len(data), length - s)
        mix[s:s + n] += data[:n]
    mix = reverb(mix, seed=seed)
    level = np.abs(mix)
    last = np.nonzero(level > 0.002)[0]
    mix = mix[:last[-1] + 1] if len(last) else mix
    fade = min(len(mix), int(0.2 * RATE))
    mix[-fade:] *= np.linspace(1.0, 0.0, fade)
    # soft compression, then -6 dBFS peak (an attack that does nothing should not sound like a hit)
    mix = np.tanh(mix * 1.4) / np.tanh(1.4)
    mix *= 10 ** (-6 / 20) / np.max(np.abs(mix))
    return (mix * 32767).astype(np.int16)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i, variant in enumerate(VARIANTS, 1):
        data = make(variant, i)
        path = OUT / f"passthrough_{i:02d}.wav"
        wavfile.write(path, RATE, data)
        rms = np.sqrt(np.mean((data / 32768.0) ** 2))
        print(f"wrote {path.name}: {len(data) / RATE:.2f} s, RMS {20 * np.log10(rms):.1f} dBFS")


if __name__ == "__main__":
    main()
