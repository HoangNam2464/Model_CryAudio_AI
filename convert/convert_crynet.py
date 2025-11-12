"""
Legacy helper to convert an ONNX model to a TensorFlow SavedModel using onnx-tf.
Outputs to convert/export_tf/ so that convert_crynet_int8.py can consume it.
"""

import sys
from pathlib import Path

import onnx
from onnx_tf.backend import prepare

BASE_DIR = Path(__file__).resolve().parent
EXPORT_DIR = BASE_DIR / "export_tf"


def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_crynet.py ../artifacts/crynet_small.onnx")
        sys.exit(1)

    onnx_path = Path(sys.argv[1])
    if not onnx_path.exists():
        raise FileNotFoundError(f"Cannot find ONNX model: {onnx_path}")

    print(f"[1/2] Loading ONNX model from {onnx_path}")
    onnx_model = onnx.load(str(onnx_path))

    print("[2/2] Converting to TensorFlow SavedModel…")
    tf_rep = prepare(onnx_model)
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    tf_rep.export_graph(str(EXPORT_DIR))
    print(f"✅ Saved TensorFlow model to {EXPORT_DIR}")


if __name__ == "__main__":
    main()
