import argparse
from pathlib import Path

import torch

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
ARTIFACTS_DIR = PROJECT_ROOT / "artifacts"

import sys

sys.path.append(str(PROJECT_ROOT))

from audioldm.models.crynet import (
    build_crynet_large,
    build_crynet_small,
)


def export_to_onnx(model_size: str, ckpt_path: Path, output_path: Path) -> None:
    device = torch.device("cpu")
    n_classes = 2

    if model_size == "small":
        model = build_crynet_small(n_mels=64, n_classes=n_classes).to(device)
    else:
        model = build_crynet_large(n_mels=64, n_classes=n_classes).to(device)

    if not ckpt_path.exists():
        raise FileNotFoundError(f"Checkpoint not found: {ckpt_path}")

    state = torch.load(ckpt_path, map_location=device)
    if isinstance(state, dict) and "model" in state:
        state = state["model"]
    model.load_state_dict(state, strict=True)
    model.eval()

    dummy_input = torch.randn(1, 1, 64, 128, device=device)
    torch.onnx.export(
        model,
        dummy_input,
        str(output_path),
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch"}, "output": {0: "batch"}},
        opset_version=12,
    )
    print(f"✅ Exported ONNX model to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Export CryNet checkpoint to ONNX")
    parser.add_argument(
        "--model",
        choices=["small", "large"],
        default="small",
        help="Architecture variant",
    )
    parser.add_argument(
        "--ckpt",
        type=str,
        default=str(ARTIFACTS_DIR / "best_crynet_small.pth"),
        help="Path to .pth checkpoint",
    )
    args = parser.parse_args()

    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    out_name = f"crynet_{args.model}.onnx"
    export_to_onnx(args.model, Path(args.ckpt), ARTIFACTS_DIR / out_name)


if __name__ == "__main__":
    main()
