import argparse
import sys
from pathlib import Path

import librosa
import numpy as np
import onnxruntime as ort
import torch

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
ARTIFACTS_DIR = PROJECT_ROOT / "artifacts"

sys.path.append(str(PROJECT_ROOT))

from audioldm.audio_processing import (  # noqa: E402
    TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_logmel,
)
from audioldm.models.crynet import build_crynet_small  # noqa: E402


def extract_features(path: Path) -> np.ndarray:
    y, _ = librosa.load(path, sr=16_000, mono=True)
    y = pad_or_trim(y)
    logmel = to_logmel(y, 16_000)
    mean, std = load_standardization()
    logmel = standardize(logmel, mean, std)
    logmel = ensure_frame_length(logmel, TARGET_FRAMES)
    return logmel[np.newaxis, np.newaxis, :, :].astype(np.float32)


def main():
    parser = argparse.ArgumentParser(description="Compare PyTorch checkpoint vs ONNX output on a single file")
    parser.add_argument("--wav", required=True, help="Path to WAV file for comparison")
    parser.add_argument("--ckpt", default=str(ARTIFACTS_DIR / "best_crynet_small.pth"))
    parser.add_argument("--onnx", default=str(ARTIFACTS_DIR / "crynet_small.onnx"))
    args = parser.parse_args()

    feat = extract_features(Path(args.wav))

    device = torch.device("cpu")
    model = build_crynet_small()
    state = torch.load(args.ckpt, map_location=device)
    if isinstance(state, dict) and "model" in state:
        state = state["model"]
    model.load_state_dict(state, strict=True)
    model.eval()
    with torch.no_grad():
        torch_out = model(torch.from_numpy(feat)).numpy().squeeze()

    ort_session = ort.InferenceSession(args.onnx, providers=["CPUExecutionProvider"])
    input_name = ort_session.get_inputs()[0].name
    onnx_out = ort_session.run(None, {input_name: feat})[0].squeeze()

    print("PyTorch logits:", torch_out)
    print("ONNX logits   :", onnx_out)
    print("Diff          :", np.abs(torch_out - onnx_out))


if __name__ == "__main__":
    main()
