from __future__ import annotations
import argparse
import sys
import time
from pathlib import Path

import numpy as np
import sounddevice as sd
import soundfile as sf

SR = 16_000
WIN_SEC = 2.0


def record_clip(sec: float, device_idx: int | None) -> np.ndarray:
    sd.default.samplerate = SR
    if device_idx is not None:
        sd.default.device = device_idx
    channels = 1
    try:
        sd.check_input_settings(device=sd.default.device, channels=channels, samplerate=SR)
    except Exception as e:
        print(f"Audio device error: {e}")
        return None
    samples = int(sec * SR)
    audio = sd.rec(samples, samplerate=SR, channels=channels, dtype="float32")
    sd.wait()
    return audio[:, 0]


def save_clip(arr: np.ndarray, out_dir: Path, prefix: str):
    out_dir.mkdir(parents=True, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    fname = f"{prefix}_{ts}.wav"
    path = out_dir / fname
    sf.write(path, arr, SR)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device_idx", type=int, default=None, help="Input device index for sounddevice")
    ap.add_argument("--win_sec", type=float, default=WIN_SEC, help="Clip length in seconds (default 2.0)")
    ap.add_argument("--root", type=Path, default=Path("data_new"), help="Root dataset folder")
    ap.add_argument("--auto_not_cry", action="store_true", help="Record continuously, auto-label as not_cry (Ctrl+C to stop)")
    args = ap.parse_args()

    if args.auto_not_cry:
        print("Mic labeler (AUTO not_cry). Press Ctrl+C to stop.")
    else:
        print("Mic labeler (auto-not_cry). Controls: q=quit, c=save as cry (optional).")
    print(f"Device={args.device_idx}, SR={SR}, window={args.win_sec}s")

    cry_dir = args.root / "cry"
    not_dir = args.root / "not_cry"

    while True:
        print("\nRecording...")
        audio = record_clip(args.win_sec, args.device_idx)
        if audio is None:
            print("Recording failed, retrying...")
            continue

        rms = np.sqrt(np.mean(audio**2) + 1e-9)
        rms_db = 20 * np.log10(rms + 1e-9)
        print(f"Recorded {len(audio)/SR:.2f}s, RMS={rms_db:.1f} dBFS")

        if args.auto_not_cry:
            path = save_clip(audio, not_dir, "not_cry")
            print(f"Saved: {path}")
            continue

        cmd = input("Label? [Enter]=not_cry, [c]=cry, [q]=quit: ").strip().lower()
        if cmd == "q":
            print("Bye.")
            break
        elif cmd == "c":
            path = save_clip(audio, cry_dir, "cry")
            print(f"Saved: {path}")
        else:  # default not_cry
            path = save_clip(audio, not_dir, "not_cry")
            print(f"Saved: {path}")


if __name__ == "__main__":
    main()
