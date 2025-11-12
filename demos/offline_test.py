import argparse
import os
import sys
from pathlib import Path

import librosa
import numpy as np
import onnxruntime as ort

sys.path.append(str(Path(__file__).resolve().parents[1]))

from audioldm.audio_processing import (  # noqa: E402
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


def extract_features(path: str) -> np.ndarray:
    y, _ = librosa.load(path, sr=SR, mono=True)
    y = pad_or_trim(y, sr=SR)
    logmel = to_logmel(y, SR)
    mean, std = load_standardization()
    logmel = standardize(logmel, mean, std)
    logmel = ensure_frame_length(logmel, TARGET_FRAMES)
    return np.expand_dims(logmel, axis=(0, 1)).astype(np.float32)


def run_directory(folder: str) -> None:
    if not os.path.exists(ONNX_MODEL):
        raise FileNotFoundError(f"Missing ONNX model at: {ONNX_MODEL}")

    ort_session = ort.InferenceSession(ONNX_MODEL, providers=["CPUExecutionProvider"])
    input_name = ort_session.get_inputs()[0].name

    wavs = sorted([p for p in Path(folder).rglob("*.wav")])
    if not wavs:
        print(f"No .wav files found in {folder}")
        return

    for wav_path in wavs:
        feat = extract_features(str(wav_path))
        logits = ort_session.run(None, {input_name: feat})[0][0]
        probs = np.exp(logits) / np.exp(logits).sum()
        pred = CLASS_NAMES[int(np.argmax(probs))]
        print(f"{wav_path.name:30s} -> {pred.upper()} ({probs[0]:.3f} cry)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Batch evaluate ONNX model on a folder of WAV files")
    parser.add_argument("folder", type=str, help="Folder containing .wav files")
    args = parser.parse_args()
    run_directory(args.folder)
