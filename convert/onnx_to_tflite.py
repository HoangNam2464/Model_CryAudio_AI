"""
Utility script that mirrors the ONNX -> TFLite (FP32 / FP16 / INT8) pipeline
using onnx2tf. Paths have been normalised to ASCII names so they work on any OS.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
ARTIFACTS_DIR = PROJECT_ROOT / "artifacts"
EXPORT_DIR = BASE_DIR / "export_tf"


def ensure_package(pkg: str) -> None:
    try:
        __import__(pkg)
    except ImportError as exc:
        raise SystemExit(f"Missing dependency '{pkg}'. Install with: pip install {pkg}") from exc


def run_cmd(args):
    subprocess.run(args, check=True)


def convert(model_name: str, quant_type: str | None, output_file: Path) -> None:
    out_dir = EXPORT_DIR if quant_type is None else EXPORT_DIR.with_name(f"export_tf_{quant_type}")
    args = [
        sys.executable,
        "-m",
        "onnx2tf",
        "-i",
        str(ARTIFACTS_DIR / model_name),
        "-o",
        str(out_dir),
    ]
    if quant_type:
        args += ["--quant_type", quant_type]
    run_cmd(args)

    suffix = {
        None: "model_float32.tflite",
        "float16": "model_float16.tflite",
        "int8": "model_integer_quant.tflite",
    }[quant_type]

    src = out_dir / suffix
    if src.exists():
        shutil.copy(src, output_file)
        print(f"✅ Saved {quant_type or 'float32'} TFLite: {output_file}")
    else:
        print(f"⚠️  Expected output not found at {src}")


def main():
    parser = argparse.ArgumentParser(description="Convert CryNet ONNX model to multiple TFLite variants.")
    parser.add_argument(
        "--onnx",
        default="crynet_small.onnx",
        help="ONNX filename inside artifacts/ (default: crynet_small.onnx)",
    )
    parser.add_argument("--no-fp16", action="store_true", help="Skip Float16 conversion")
    parser.add_argument("--no-int8", action="store_true", help="Skip INT8 conversion")
    args = parser.parse_args()

    ensure_package("onnx2tf")

    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)

    convert(args.onnx, None, ARTIFACTS_DIR / "crynet_fp32.tflite")
    if not args.no_fp16:
        convert(args.onnx, "float16", ARTIFACTS_DIR / "crynet_fp16.tflite")
    if not args.no_int8:
        convert(args.onnx, "int8", ARTIFACTS_DIR / "crynet_int8.tflite")


if __name__ == "__main__":
    main()
