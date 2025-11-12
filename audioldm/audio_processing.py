# audioldm/audio_processing.py
from __future__ import annotations
from pathlib import Path
import numpy as np
import librosa

SR = 16_000
N_MELS = 64
N_FFT = 512
WIN_LENGTH = int(0.025 * SR)   # 25 ms
HOP_LENGTH = int(0.010 * SR)   # 10 ms
DURATION = 2.0                 # seconds/window
TARGET_FRAMES = 128            # canonical length (64 x 128) used across pipeline
EPS = 1e-9

_ROOT = Path(__file__).resolve().parent
_DEFAULT_STATS = _ROOT / "standardization.npz"


def load_audio(path: str | Path, sr: int = SR):
    y, s = librosa.load(str(path), sr=sr, mono=True)
    return y, s


def to_logmel(
    y: np.ndarray,
    sr: int = SR,
    n_mels: int = N_MELS,
    n_fft: int = N_FFT,
    hop_length: int = HOP_LENGTH,
    win_length: int = WIN_LENGTH,
) -> np.ndarray:
    mel = librosa.feature.melspectrogram(
        y=y, sr=sr, n_mels=n_mels, n_fft=n_fft,
        hop_length=hop_length, win_length=win_length, power=2.0
    )
    logmel = librosa.power_to_db(mel, ref=np.max)
    return logmel.astype(np.float32)


def load_standardization(stats_path: str | Path = _DEFAULT_STATS):
    stats_path = Path(stats_path)
    if stats_path.exists():
        d = np.load(stats_path)
        mean = d["mean"]
        std = d["std"]
        return mean.astype(np.float32), np.maximum(std.astype(np.float32), 1e-6)
    return None, None


def save_standardization(mean: np.ndarray, std: np.ndarray, stats_path: str | Path = _DEFAULT_STATS):
    stats_path = Path(stats_path)
    stats_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(stats_path, mean=mean.astype(np.float32), std=std.astype(np.float32))


def standardize(logmel: np.ndarray, mean: np.ndarray | None, std: np.ndarray | None) -> np.ndarray:
    if mean is None or std is None:
        m = logmel.mean()
        s = logmel.std() + EPS
        return (logmel - m) / s

    if mean.ndim == 1:
        mean = mean[:, None]
    if std.ndim == 1:
        std = std[:, None]

    return (logmel - mean) / (std + EPS)


def pad_or_trim(y: np.ndarray, sr: int = SR, duration: float = DURATION):
    target_len = int(sr * duration)
    if len(y) < target_len:
        pad = target_len - len(y)
        y = np.pad(y, (0, pad), mode="constant")
    else:
        y = y[:target_len]
    return y


def ensure_frame_length(logmel: np.ndarray, target_frames: int = TARGET_FRAMES) -> np.ndarray:
    """
    Ensure log-mel spectrogram has consistent time dimension.
    Pads with zeros (silence) or truncates to `target_frames`.
    """
    if logmel.shape[1] < target_frames:
        pad_width = target_frames - logmel.shape[1]
        logmel = np.pad(logmel, ((0, 0), (0, pad_width)), mode="constant")
    else:
        logmel = logmel[:, :target_frames]
    return logmel


def slice_logmel_windows(logmel: np.ndarray, frames_per_win: int, hop_frames: int | None = None):
    n_mels, T = logmel.shape
    if hop_frames is None:
        hop_frames = frames_per_win // 2
    windows = []
    for start in range(0, max(1, T - frames_per_win + 1), hop_frames):
        win = logmel[:, start:start + frames_per_win]
        if win.shape[1] < frames_per_win:
            pad = frames_per_win - win.shape[1]
            win = np.pad(win, ((0, 0), (0, pad)), mode="edge")
        windows.append(win)
    return np.stack(windows, axis=0)


def frames_for_seconds(seconds: float, hop_length: int = HOP_LENGTH, sr: int = SR) -> int:
    samples = int(seconds * sr)
    return max(1, samples // hop_length)
