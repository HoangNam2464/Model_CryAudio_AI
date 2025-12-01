# audioldm/audio_processing.py
from __future__ import annotations
from pathlib import Path
import numpy as np
import librosa

SR = 16_000
DURATION = 2.0  # seconds/window

# MFCC config cho pipeline duy nhất (20 x 25 frames, hop ~80 ms)
MFCC_N_MELS = 32
MFCC_N_MFCC = 20
MFCC_N_FFT = 512
MFCC_HOP_LENGTH = int(0.080 * SR)  # 80 ms
MFCC_WIN_LENGTH = MFCC_N_FFT       # 32 ms
MFCC_TARGET_FRAMES = 25
EPS = 1e-9

_ROOT = Path(__file__).resolve().parent
DEFAULT_MFCC_STATS = _ROOT / "standardization_mfcc.npz"


def load_audio(path: str | Path, sr: int = SR):
    y, s = librosa.load(str(path), sr=sr, mono=True)
    return y, s


def to_mfcc(
    y: np.ndarray,
    sr: int = SR,
    n_mfcc: int = MFCC_N_MFCC,
    n_mels: int = MFCC_N_MELS,
    n_fft: int = MFCC_N_FFT,
    hop_length: int = MFCC_HOP_LENGTH,
    win_length: int = MFCC_WIN_LENGTH,
) -> np.ndarray:
    mfcc = librosa.feature.mfcc(
        y=y,
        sr=sr,
        n_mfcc=n_mfcc,
        n_mels=n_mels,
        n_fft=n_fft,
        hop_length=hop_length,
        win_length=win_length,
    )
    return mfcc.astype(np.float32)


def load_standardization(stats_path: str | Path = DEFAULT_MFCC_STATS):
    stats_path = Path(stats_path)
    if stats_path.exists():
        d = np.load(stats_path)
        mean = d["mean"]
        std = d["std"]
        return mean.astype(np.float32), np.maximum(std.astype(np.float32), 1e-6)
    return None, None


def save_standardization(mean: np.ndarray, std: np.ndarray, stats_path: str | Path = DEFAULT_MFCC_STATS):
    stats_path = Path(stats_path)
    stats_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(stats_path, mean=mean.astype(np.float32), std=std.astype(np.float32))


def standardize(arr: np.ndarray, mean: np.ndarray | None, std: np.ndarray | None) -> np.ndarray:
    if mean is None or std is None:
        m = arr.mean()
        s = arr.std() + EPS
        return (arr - m) / s

    if mean.ndim == 1:
        mean = mean[:, None]
    if std.ndim == 1:
        std = std[:, None]

    return (arr - mean) / (std + EPS)


def pad_or_trim(y: np.ndarray, sr: int = SR, duration: float = DURATION):
    target_len = int(sr * duration)
    if len(y) < target_len:
        pad = target_len - len(y)
        y = np.pad(y, (0, pad), mode="constant")
    else:
        y = y[:target_len]
    return y


def ensure_frame_length(arr: np.ndarray, target_frames: int = MFCC_TARGET_FRAMES) -> np.ndarray:
    """Pad/truncate feature theo trục thời gian về target_frames."""
    if arr.shape[1] < target_frames:
        pad_width = target_frames - arr.shape[1]
        arr = np.pad(arr, ((0, 0), (0, pad_width)), mode="constant")
    else:
        arr = arr[:, :target_frames]
    return arr
