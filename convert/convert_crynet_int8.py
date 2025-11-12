import sys
from pathlib import Path

import librosa
import numpy as np
import tensorflow as tf

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
MODEL_DIR = BASE_DIR / "export_tf"
CALIB_DIR = MODEL_DIR / "assets" / "calib"
ARTIFACTS_DIR = PROJECT_ROOT / "artifacts"
OUT_PATH = ARTIFACTS_DIR / "crynet_int8.tflite"

SR = 16_000
N_MELS = 64

sys.path.append(str(PROJECT_ROOT))

from audioldm.audio_processing import (  # noqa: E402
    TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_logmel,
)


def representative_dataset():
    mean, std = load_standardization()
    if mean is None or std is None:
        raise RuntimeError("Missing standardization.npz. Run training to generate statistics first.")

    if not CALIB_DIR.exists():
        raise FileNotFoundError(f"Calibration directory not found: {CALIB_DIR}")

    wavs = sorted([p for p in CALIB_DIR.rglob("*.wav")])
    if not wavs:
        raise RuntimeError(f"No .wav files found inside calibration directory: {CALIB_DIR}")

    for path in np.random.permutation(wavs)[:200]:
        y, _ = librosa.load(path, sr=SR, mono=True)
        y = pad_or_trim(y, sr=SR)
        logmel = to_logmel(y, sr=SR, n_mels=N_MELS)
        logmel = standardize(logmel, mean, std)
        logmel = ensure_frame_length(logmel, TARGET_FRAMES)
        yield [logmel[np.newaxis, np.newaxis, :, :].astype(np.float32)]


def main():
    if not MODEL_DIR.exists():
        raise FileNotFoundError(f"SavedModel directory not found: {MODEL_DIR}")

    print("[1/2] Loading TensorFlow SavedModel…")
    converter = tf.lite.TFLiteConverter.from_saved_model(str(MODEL_DIR))
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    print("[2/2] Converting to INT8 TFLite…")
    tflite_model = converter.convert()

    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_bytes(tflite_model)
    print(f"✅ Saved INT8 model: {OUT_PATH} ({OUT_PATH.stat().st_size/1024:.1f} KB)")


if __name__ == "__main__":
    main()
