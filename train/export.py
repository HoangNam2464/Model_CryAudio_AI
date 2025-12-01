import tensorflow as tf
from pathlib import Path
import librosa
import numpy as np

from config import SR, CALIB_FILES_MAX
from preprocess import normalize_gain, pad_or_trim, extract_mfcc


def representative_dataset(calib_files):
    for p in calib_files[:CALIB_FILES_MAX]:
        y, _ = librosa.load(p, sr=SR, mono=True)
        y = normalize_gain(y)
        y = pad_or_trim(y)
        mfcc = extract_mfcc(y)
        yield [mfcc[np.newaxis, ..., np.newaxis].astype(np.float32)]


def convert_int8(saved_model_dir, calib_files, out_path="cry_tiny_int8.tflite"):
    converter = tf.lite.TFLiteConverter.from_saved_model(saved_model_dir)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: representative_dataset(calib_files)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    tfl = converter.convert()
    Path(out_path).write_bytes(tfl)
    print(f"Saved INT8 model to {out_path} ({len(tfl)/1024:.1f} KB)")


def tflite_to_cc(tflite_path, cc_path, var="g_cry_tflite"):
    data = Path(tflite_path).read_bytes()
    with open(cc_path, "w") as f:
        f.write("#include <cstddef>\n")
        f.write(f"alignas(16) const unsigned char {var}[] = {{\n")
        for i, b in enumerate(data):
            f.write(f"0x{b:02X},")
            if (i + 1) % 12 == 0:
                f.write("\n")
        f.write("};\n")
        f.write(f"const int {var}_len = {len(data)};\n")
    print(f"Saved C array to {cc_path} (bytes={len(data)})")
