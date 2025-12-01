import argparse
import shutil
import sys
from pathlib import Path
import tempfile

import librosa
import numpy as np
import tensorflow as tf
import onnx
from onnx_tf.backend import prepare

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
ARTIFACTS_DIR = PROJECT_ROOT / "artifacts"
DEFAULT_ONNX = ARTIFACTS_DIR / "ds_cnn.onnx"
DEFAULT_CALIB_DIR = BASE_DIR / "calib"
DEFAULT_DATA_DIR = PROJECT_ROOT / "data_new"
OUT_PATH = ARTIFACTS_DIR / "ds_cnn_int8.tflite"

SR = 16_000

sys.path.append(str(PROJECT_ROOT))

from audioldm.audio_processing import (  # noqa: E402
    DEFAULT_MFCC_STATS,
    MFCC_N_FFT,
    MFCC_HOP_LENGTH,
    MFCC_N_MELS,
    MFCC_N_MFCC,
    MFCC_TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_mfcc,
)


def list_audio_files(root: Path):
    return [p for p in root.rglob("*") if p.suffix.lower() in {".wav", ".flac", ".mp3", ".ogg", ".m4a"}]


def representative_dataset(calib_dir: Path, data_fallback: Path | None):
    mean, std = load_standardization(DEFAULT_MFCC_STATS)
    if mean is None or std is None:
        raise RuntimeError("Missing standardization_mfcc.npz. Run training to generate statistics first.")

    wavs = []
    if calib_dir and calib_dir.exists():
        wavs = list_audio_files(calib_dir)
    if not wavs and data_fallback and data_fallback.exists():
        # fallback: use data_new cry/not_cry
        for sub in ["cry", "not_cry"]:
            wavs += list_audio_files(data_fallback / sub)
    if not wavs:
        raise RuntimeError(f"No calibration audio found. Provide wavs in {calib_dir} or set a valid --data_dir fallback.")

    for path in np.random.permutation(wavs)[:200]:
        y, _ = librosa.load(path, sr=SR, mono=True)
        y = pad_or_trim(y, sr=SR)
        mfcc = to_mfcc(
            y,
            sr=SR,
            n_mfcc=MFCC_N_MFCC,
            n_mels=MFCC_N_MELS,
            n_fft=MFCC_N_FFT,
            hop_length=MFCC_HOP_LENGTH,
            win_length=MFCC_N_FFT,
        )
        mfcc = standardize(mfcc, mean, std)
        mfcc = ensure_frame_length(mfcc, MFCC_TARGET_FRAMES)
        yield [mfcc[np.newaxis, np.newaxis, :, :].astype(np.float32)]


def onnx_to_saved_model(onnx_path: Path, export_dir: Path):
    model = onnx.load(str(onnx_path))
    tf_rep = prepare(model)
    if export_dir.exists():
        shutil.rmtree(export_dir)
    tf_rep.export_graph(str(export_dir))
    return export_dir


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", type=str, default=str(DEFAULT_ONNX), help="Path to ds_cnn.onnx")
    ap.add_argument("--out", type=str, default=str(OUT_PATH), help="Output INT8 TFLite path")
    ap.add_argument("--calib_dir", type=str, default=str(DEFAULT_CALIB_DIR), help="Directory containing calibration WAVs (optional)")
    ap.add_argument("--data_dir", type=str, default=str(DEFAULT_DATA_DIR), help="Fallback data directory (expects cry/not_cry subdirs)")
    args = ap.parse_args()

    onnx_path = Path(args.onnx)
    if not onnx_path.exists():
        raise FileNotFoundError(f"ONNX model not found: {onnx_path}")
    calib_dir = Path(args.calib_dir) if args.calib_dir else None
    data_fallback = Path(args.data_dir) if args.data_dir else None

    with tempfile.TemporaryDirectory() as tmpdir:
        saved_model_dir = Path(tmpdir) / "saved_model"
        print("[1/3] Converting ONNX -> TensorFlow SavedModel...")
        onnx_to_saved_model(onnx_path, saved_model_dir)

        print("[2/3] Building INT8 TFLite converter...")
        converter = tf.lite.TFLiteConverter.from_saved_model(str(saved_model_dir))
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = lambda: representative_dataset(calib_dir, data_fallback)
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8

        print("[3/3] Converting to INT8 TFLite...")
        tflite_model = converter.convert()

    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    out_path = Path(args.out)
    out_path.write_bytes(tflite_model)
    print(f"Saved INT8 model: {out_path} ({out_path.stat().st_size/1024:.1f} KB)")


if __name__ == "__main__":
    main()
