import numpy as np
import librosa
import random
from config import SR


def time_shift(y, max_shift=0.2):
    shift = int(random.uniform(-max_shift, max_shift) * len(y))
    return np.roll(y, shift)


def change_gain(y, db_range=(-6, 6)):
    db = random.uniform(*db_range)
    return y * (10 ** (db / 20))


def mix_noise(y, noise, snr_db=5):
    if len(noise) < len(y):
        noise = np.pad(noise, (0, len(y) - len(noise)))
    noise = noise[: len(y)]
    sig_power = np.mean(y ** 2) + 1e-9
    noise_power = np.mean(noise ** 2) + 1e-9
    desired_noise_power = sig_power / (10 ** (snr_db / 10))
    scale = np.sqrt(desired_noise_power / noise_power)
    return y + noise * scale


def pre_emphasis(y, coef=0.97):
    return librosa.effects.preemphasis(y, coef=coef)


def pitch_shift_small(y, sr=SR, steps_range=(-1, 1)):
    steps = random.uniform(*steps_range)
    return librosa.effects.pitch_shift(y, sr=sr, n_steps=steps)


def augment_wave(y, noise_pool):
    y = time_shift(y, 0.2)
    y = change_gain(y, (-6, 6))
    if noise_pool:
        n = random.choice(noise_pool)
        snr = random.choice([0, 5, 10, 15])
        y = mix_noise(y, n, snr)
    y = pre_emphasis(y, 0.97)
    if random.random() < 0.3:
        y = pitch_shift_small(y, SR, (-0.5, 0.5))
    return y
