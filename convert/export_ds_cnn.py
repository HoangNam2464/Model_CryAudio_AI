import argparse
from pathlib import Path
import torch

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
ARTIFACTS_DIR = PROJECT_ROOT / "artifacts"

import sys

sys.path.append(str(PROJECT_ROOT))

from audioldm.models.ds_cnn import build_ds_cnn  # noqa: E402


def export_to_onnx(ckpt_path: Path, output_path: Path) -> None:
    device = torch.device("cpu")
    model = build_ds_cnn(num_classes=2).to(device)

    if not ckpt_path.exists():
        raise FileNotFoundError(f"Checkpoint not found: {ckpt_path}")

    state = torch.load(ckpt_path, map_location=device)
    if isinstance(state, dict) and "model" in state:
        state = state["model"]
    model.load_state_dict(state, strict=True)
    model.eval()

    dummy_input = torch.randn(1, 1, 20, 25, device=device)
    torch.onnx.export(
        model,
        dummy_input,
        str(output_path),
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch"}, "output": {0: "batch"}},
        opset_version=12,
    )
    print(f"Exported ONNX model to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Export DS-CNN checkpoint to ONNX")
    parser.add_argument(
        "--ckpt",
        type=str,
        default=str(ARTIFACTS_DIR / "best_ds_cnn.pth"),
        help="Path to .pth checkpoint",
    )
    parser.add_argument(
        "--out",
        type=str,
        default=str(ARTIFACTS_DIR / "ds_cnn.onnx"),
        help="Output ONNX path",
    )
    args = parser.parse_args()

    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    export_to_onnx(Path(args.ckpt), Path(args.out))


if __name__ == "__main__":
    main()
