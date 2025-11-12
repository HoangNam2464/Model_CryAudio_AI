import argparse
import os
import sys
from pathlib import Path

import librosa
import numpy as np
import onnxruntime as ort

sys.path.append(str(Path(__file__).resolve().parents[1]))

from audioldm.audio_processing import (  # noqa: E402
    DURATION,
    SR,
    TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_logmel,
)

CLASS_NAMES = ["cry", "not_cry"]
ONNX_MODEL = "artifacts/crynet_small.onnx"


def load_wav(path: str, sr: int = SR, duration: float = DURATION) -> np.ndarray:
    """Load and normalise a waveform to mono, 16 kHz, fixed duration."""
    y, _ = librosa.load(path, sr=sr, mono=True)
    y = pad_or_trim(y, sr=sr, duration=duration)
    return y


def extract_features(y: np.ndarray, sr: int = SR) -> np.ndarray:
    """Compute log-mel feature and apply global standardisation."""
    logmel = to_logmel(y, sr)
    mean, std = load_standardization()
    logmel = standardize(logmel, mean, std)
    logmel = ensure_frame_length(logmel, TARGET_FRAMES)
    return np.expand_dims(logmel, axis=(0, 1)).astype(np.float32)


def predict_one(wav_path: str) -> None:
    if not os.path.exists(ONNX_MODEL):
        raise FileNotFoundError(f"Missing ONNX model at: {ONNX_MODEL}")

    ort_session = ort.InferenceSession(ONNX_MODEL, providers=["CPUExecutionProvider"])
    input_name = ort_session.get_inputs()[0].name

    y = load_wav(wav_path)
    feat = extract_features(y)
    logits = ort_session.run(None, {input_name: feat})[0][0]
    probs = np.exp(logits) / np.exp(logits).sum()

    pred_idx = int(np.argmax(probs))
    pred_class = CLASS_NAMES[pred_idx]

    print(f"\n📄 File: {wav_path}")
    for i, cname in enumerate(CLASS_NAMES):
        print(f"  {cname:<8}: {probs[i]:.3f}")
    print(f"🧠 Prediction: {pred_class.upper()} (id={pred_idx})")

    if probs[0] >= 0.1:
        print("⚠️  Detected: baby is crying (prob ≥ 0.1)")
    else:
        print("✅ No cry detected.\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Predict baby cry from a single .wav file")
    parser.add_argument("wav_path", type=str, help="Path to .wav (2s, 16 kHz)")
    args = parser.parse_args()
    predict_one(args.wav_path)
