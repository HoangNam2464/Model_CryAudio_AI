import librosa
import numpy as np
from pathlib import Path

from config import SR, FRAME_LEN, HOP, N_MFCC, N_FRAMES, TARGET_SAMPLES


def normalize_gain(y, target_db: float = -20.0):
    rms = np.sqrt(np.mean(y ** 2) + 1e-9)
    rms_db = 20 * np.log10(rms + 1e-9)
    gain = 10 ** ((target_db - rms_db) / 20)
    return y * gain


def pad_or_trim(y: np.ndarray, target_len: int = TARGET_SAMPLES):
    if len(y) < target_len:
        y = np.pad(y, (0, target_len - len(y)))
    else:
        y = y[:target_len]
    return y


def extract_mfcc(y: np.ndarray):
    mfcc = librosa.feature.mfcc(
        y=y,
        sr=SR,
        n_mfcc=N_MFCC,
        n_fft=FRAME_LEN,
        hop_length=HOP,
        htk=True,
    )
    if mfcc.shape[1] < N_FRAMES:
        mfcc = np.pad(mfcc, ((0, 0), (0, N_FRAMES - mfcc.shape[1])), mode="constant")
    else:
        mfcc = mfcc[:, :N_FRAMES]
    mean = mfcc.mean()
    std = mfcc.std() + 1e-6
    mfcc = (mfcc - mean) / std
    return mfcc.astype(np.float32)  # (N_MFCC, N_FRAMES)


def preprocess_file(path: Path):
    y, _ = librosa.load(path, sr=SR, mono=True)
    y = normalize_gain(y, target_db=-20)
    y = pad_or_trim(y)
    mfcc = extract_mfcc(y)
    return mfcc  # (N_MFCC, N_FRAMES)
