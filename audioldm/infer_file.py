from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
import torch

from audioldm.audio_processing import (
    SR,
    TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_logmel,
)
from audioldm.models.crynet import build_crynet

CLASS_NAMES = ["cry", "not_cry"]


def load_wav(path: str | Path, sr: int = SR) -> np.ndarray:
    """Load an audio file, convert to mono float32 at the desired sr."""
    y, file_sr = sf.read(path)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if file_sr != sr:
        import librosa

        y = librosa.resample(y, orig_sr=file_sr, target_sr=sr)
    return y.astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", type=str, default="artifacts/best_crynet_small.pth", help="Path to trained checkpoint")
    ap.add_argument("--wav", type=str, required=True, help=" WAV file to evaluate")
    ap.add_argument("--threshold", type=float, default=0.6, help="Probability threshold for cry detection")
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"🖥️  Using device: {device}")

    model = build_crynet(model_size="small", n_mels=64, n_classes=len(CLASS_NAMES)).to(device)
    state = torch.load(args.ckpt, map_location=device)
    if isinstance(state, dict) and "model" in state:
        state = state["model"]
    model.load_state_dict(state)
    model.eval()

    mean, std = load_standardization()
    y = load_wav(args.wav, sr=SR)
    y = pad_or_trim(y, sr=SR)

    logmel = to_logmel(y, sr=SR)
    logmel = standardize(logmel, mean, std)
    logmel = ensure_frame_length(logmel, TARGET_FRAMES)
    x = torch.from_numpy(logmel).unsqueeze(0).unsqueeze(0).to(device)

    with torch.no_grad():
        logits = model(x)
        proba = torch.softmax(logits, dim=-1)[0].cpu().numpy()

    cry_prob = float(proba[CLASS_NAMES.index("cry")])
    pred_idx = int(proba.argmax())
    pred_label = CLASS_NAMES[pred_idx]

    print(f"\n📄 File: {args.wav}")
    for name, value in zip(CLASS_NAMES, proba):
        print(f"  {name:<8}: {value:.3f}")
    print(f"🧠 Prediction: {pred_label.upper()}")

    if cry_prob >= args.threshold:
        print(f"⚠️  Cry detected! (prob={cry_prob:.2f} ≥ {args.threshold})")
    else:
        print(f"✅ No cry detected (prob={cry_prob:.2f} < {args.threshold})")


if __name__ == "__main__":
    main()
