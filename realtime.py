"""
Realtime microphone demo for DS-CNN (MFCC 20x25).
- Captures ~2 s audio windows from default mic (16 kHz)
- Extracts MFCC (matching training config)
- Runs DS-CNN checkpoint and prints probabilities
"""
from __future__ import annotations
import argparse
import sys
import time
from pathlib import Path

import numpy as np
import sounddevice as sd
import torch
import scipy.signal as sps

from audioldm.audio_processing import (
    SR,
    MFCC_TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_mfcc,
)
from audioldm.models.ds_cnn import build_ds_cnn


def load_model(ckpt_path: Path, device: torch.device):
    model = build_ds_cnn(num_classes=2).to(device)
    state = torch.load(ckpt_path, map_location=device)
    if isinstance(state, dict) and "model" in state:
        state = state["model"]
    model.load_state_dict(state, strict=True)
    model.eval()
    return model


def bandpass(audio_np: np.ndarray, sr: int, low: int = 300, high: int = 3000):
    b, a = sps.butter(4, [low / (sr * 0.5), high / (sr * 0.5)], btype="bandpass")
    return sps.lfilter(b, a, audio_np)


def infer_window(model, device, mean, std, audio_np: np.ndarray, use_prefilter: bool = False):
    audio_np = pad_or_trim(audio_np, sr=SR)
    if use_prefilter:
        audio_np = bandpass(audio_np, SR)
    rms = np.sqrt(np.mean(audio_np**2) + 1e-9)
    rms_db = 20 * np.log10(rms + 1e-9)
    mfcc = to_mfcc(audio_np, sr=SR)
    mfcc = standardize(mfcc, mean, std)
    mfcc = ensure_frame_length(mfcc, MFCC_TARGET_FRAMES)
    x = torch.from_numpy(mfcc).unsqueeze(0).unsqueeze(0).to(device)
    with torch.no_grad():
        logits = model(x)
        probs = torch.softmax(logits, dim=-1)[0].cpu().numpy()
    return float(probs[0]), float(probs[1]), float(rms_db)  # [cry, not_cry], rms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", type=Path, default=Path("artifacts/best_ds_cnn.pth"), help="Checkpoint path (.pth)")
    ap.add_argument("--device_idx", type=int, default=None, help="sounddevice input device index (optional)")
    ap.add_argument("--threshold", type=float, default=0.6, help="Threshold to announce cry")
    ap.add_argument("--margin", type=float, default=0.15, help="Require (cry_prob - calm_prob) >= margin")
    ap.add_argument("--window_sec", type=float, default=2.0, help="Capture window length in seconds (keep at 2.0 for 25 frames)")
    ap.add_argument("--vote_win", type=int, default=2, help="Consecutive CRY windows required to trigger CRY")
    ap.add_argument("--rms_gate_db", type=float, default=-55.0, help="Ignore decision if RMS below this dBFS")
    ap.add_argument("--prefilter", action="store_true", help="Apply 300-3000 Hz bandpass before MFCC")
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using torch device: {device}")

    if not args.ckpt.exists():
        print(f"Checkpoint not found: {args.ckpt}")
        sys.exit(1)
    model = load_model(args.ckpt, device)

    mean, std = load_standardization()
    if mean is None or std is None:
        print("Missing standardization_mfcc.npz. Train once to generate it.")
        sys.exit(1)

    # Configure audio input
    sd.default.samplerate = SR
    if args.device_idx is not None:
        sd.default.device = args.device_idx
    channels = 1
    try:
        sd.check_input_settings(device=sd.default.device, channels=channels, samplerate=SR)
    except Exception as e:
        print(f"Audio device error: {e}")
        sys.exit(1)

    samples_per_win = int(args.window_sec * SR)
    print("Starting realtime mic inference (Ctrl+C to stop)")
    print(f"- device: {sd.default.device}, sr={SR}, window={args.window_sec}s, threshold={args.threshold}, margin={args.margin}, vote_win={args.vote_win}")

    vote_counter = 0
    try:
        while True:
            audio = sd.rec(samples_per_win, samplerate=SR, channels=channels, dtype="float32")
            sd.wait()
            audio = audio[:, 0]  # mono

            prob_cry, prob_calm, rms_db = infer_window(model, device, mean, std, audio, use_prefilter=args.prefilter)
            gated = rms_db < args.rms_gate_db
            margin_ok = (prob_cry - prob_calm) >= args.margin

            if gated:
                # Force calm when gate is active to avoid confusing readouts
                prob_cry, prob_calm = 0.0, 1.0

            if gated or prob_cry < args.threshold or not margin_ok:
                vote_counter = 0
            else:
                vote_counter += 1

            is_cry = (vote_counter >= args.vote_win) and (not gated)
            ts = time.strftime("%H:%M:%S")
            print(f"[{ts}] cry={prob_cry:.3f} calm={prob_calm:.3f} Δ={prob_cry-prob_calm:.3f} rms={rms_db:.1f}dB gated={gated} votes={vote_counter}/{args.vote_win} -> {'CRY' if is_cry else 'CALM'}")
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
