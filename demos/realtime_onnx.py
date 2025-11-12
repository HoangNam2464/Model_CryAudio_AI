import argparse
import queue
import sys
import time
from collections import deque
from pathlib import Path

import numpy as np
import onnxruntime as ort
import sounddevice as sd
import scipy.signal as sg

sys.path.append(str(Path(__file__).resolve().parents[1]))
from audioldm.audio_processing import (  # noqa: E402
    SR,
    TARGET_FRAMES,
    ensure_frame_length,
    load_standardization,
    pad_or_trim,
    standardize,
    to_logmel,
)


def rms(y: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(y))) + 1e-9)


def make_features(
    y: np.ndarray,
    mean: np.ndarray | None,
    std: np.ndarray | None,
    noise_logmel: np.ndarray | None = None,
) -> np.ndarray:
    """Match the exact preprocessing used for training/evaluation."""
    logmel = to_logmel(y, SR)
    if noise_logmel is not None:
        logmel = ensure_frame_length(logmel, TARGET_FRAMES)
        noise = ensure_frame_length(noise_logmel, TARGET_FRAMES)
        logmel = np.maximum(logmel - noise, -80.0)
    else:
        logmel = ensure_frame_length(logmel, TARGET_FRAMES)
    logmel = standardize(logmel, mean, std)
    return np.expand_dims(logmel, axis=(0, 1)).astype(np.float32)


def _update_streak(current: float, active: bool, chunk_dt: float, clamp: float) -> float:
    """Accumulate consecutive ON/OFF time; reset immediately when condition breaks."""
    if active:
        return min(current + chunk_dt, clamp)
    return 0.0


def bandpass_filter(y: np.ndarray, sr: int, low: float, high: float, order: int = 4) -> np.ndarray:
    nyq = sr / 2.0
    low_n = max(10.0, low) / nyq
    high_n = min(sr * 0.49, high) / nyq
    if not (0 < low_n < high_n < 1):
        return y
    b, a = sg.butter(order, [low_n, high_n], btype="band")
    return sg.lfilter(b, a, y)


def run_stream(
    model_path: str,
    on_thr: float = 0.65,
    off_thr: float = 0.40,
    ema_alpha: float = 0.35,
    stable_on: float = 0.6,
    stable_off: float = 0.6,
    min_on: float = 0.8,
    min_off: float = 0.4,
    calib_sec: float = 1.5,
    gate_mul: float = 1.5,
    off_rms_mul: float = 1.4,
    use_gate: bool = True,
    block_dur: float = 0.25,
    smooth_win: float = 0.8,
    use_prefilter: bool = False,
    filter_low: float = 300.0,
    filter_high: float = 3500.0,
    use_noise_sub: bool = False,
    device: int | None = None,
):
    if not Path(model_path).exists():
        raise FileNotFoundError(f"Missing ONNX model at: {model_path}")

    sess = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
    input_name = sess.get_inputs()[0].name
    mean, std = load_standardization()

    audio_q: queue.Queue[np.ndarray] = queue.Queue()
    cry_on = False
    hold_until = 0.0
    next_on_ok_at = 0.0
    on_streak = 0.0
    off_streak = 0.0
    ema_prob = None

    prob_hist: deque[tuple[float, float]] = deque()
    hist_weight = 0.0
    hist_time = 0.0
    noise_logmel: np.ndarray | None = None

    def audio_cb(indata, frames, time_info, status):
        if status:
            print(status, file=sys.stderr)
        audio_q.put(indata.copy().flatten())

    print("Listening... Ctrl+C to stop")
    print(
        f"on={on_thr:.2f}, off={off_thr:.2f}, ema={ema_alpha}, "
        f"stable_on={stable_on:.2f}s, stable_off={stable_off:.2f}s, "
        f"min_on={min_on:.1f}s, min_off={min_off:.1f}s, gate_mul={gate_mul}, "
        f"block_dur={block_dur:.2f}s, smooth_win={smooth_win:.2f}s, "
        f"off_rms_mul={off_rms_mul}, "
        f"{'bandpass' if use_prefilter else 'raw'} "
        f"{'noise_sub' if use_noise_sub else ''} "
        f"{'(GATE OFF)' if not use_gate else ''}"
    )
    print(f"Calibrating {calib_sec:.1f}s...")

    blocksize = max(1, int(block_dur * SR))

    with sd.InputStream(
        samplerate=SR,
        channels=1,
        device=device,
        callback=audio_cb,
        blocksize=blocksize,
    ):
        calib_end = time.time() + calib_sec
        rms_vals = []
        buf = np.zeros(int(SR * 2.0), dtype=np.float32)

        noise_specs = []

        while time.time() < calib_end:
            chunk = audio_q.get()
            if chunk.size == 0:
                continue
            buf = np.concatenate([buf[len(chunk) :], chunk])
            window = pad_or_trim(buf, sr=SR)
            if use_prefilter:
                window = bandpass_filter(window, SR, filter_low, filter_high)
            rms_vals.append(rms(window))
            if use_noise_sub:
                noise_specs.append(ensure_frame_length(to_logmel(window, SR)))

        amb_rms = np.median(rms_vals) if rms_vals else 1e-3
        gate_rms = max(amb_rms * gate_mul, amb_rms + 1e-4)
        print(f"Ambient RMS = {amb_rms:.6f} -> noise gate = {gate_rms:.6f}\n")

        if use_noise_sub and noise_specs:
            noise_logmel = np.mean(noise_specs, axis=0)

        while True:
            chunk = audio_q.get()
            if chunk.size == 0:
                continue
            buf = np.concatenate([buf[len(chunk) :], chunk])

            # 1) conditioning (match training preprocessing)
            window = pad_or_trim(buf, sr=SR)
            if use_prefilter:
                window = bandpass_filter(window, SR, filter_low, filter_high)
            curr_rms = rms(window)
            feat = make_features(window, mean, std, noise_logmel)
            chunk_dt = len(chunk) / SR

            # 2) inference
            logits = sess.run(None, {input_name: feat})[0][0]
            probs = np.exp(logits) / np.exp(logits).sum()
            p_cry = float(probs[0])
            p_not = float(probs[1])

            # 3) smoothing window (stabilise cry prob)
            if smooth_win > 0:
                prob_hist.append((p_cry, chunk_dt))
                hist_weight += p_cry * chunk_dt
                hist_time += chunk_dt
                while prob_hist and hist_time > smooth_win + 1e-9:
                    old_prob, old_dt = prob_hist.popleft()
                    hist_weight -= old_prob * old_dt
                    hist_time -= old_dt
                proc_prob = hist_weight / hist_time if hist_time > 0 else p_cry
            else:
                proc_prob = p_cry

            # 4) EMA smoothing
            ema_prob = proc_prob if ema_prob is None else (ema_alpha * proc_prob + (1 - ema_alpha) * ema_prob)

            # 5) gate state
            gated_silent = (curr_rms < gate_rms) if use_gate else False
            off_rms_ok = True if not cry_on else (curr_rms <= gate_rms * off_rms_mul)

            # 6) hysteresis + debounce + hold
            now = time.time()
            want_on = ema_prob >= on_thr
            want_off = ((ema_prob <= off_thr) and off_rms_ok) or gated_silent

            on_streak = _update_streak(on_streak, want_on, chunk_dt, stable_on * 3.0)
            off_streak = _update_streak(off_streak, want_off, chunk_dt, stable_off * 3.0)

            if (not cry_on) and (now >= next_on_ok_at) and (on_streak >= stable_on):
                cry_on = True
                hold_until = now + min_on
                off_streak = 0.0
                print(f"\n>>> CRY ON <<< prob={ema_prob:.2f} (rms={curr_rms:.5f})")

            if cry_on and (now >= hold_until) and (off_streak >= stable_off):
                cry_on = False
                next_on_ok_at = now + min_off
                on_streak = 0.0
                print(f"\n<<< CRY OFF >>> prob={ema_prob:.2f} (rms={curr_rms:.5f})")

            # 7) live readout
            if smooth_win > 0:
                cry_display = f"{proc_prob:.2f} (raw={p_cry:.2f})"
            else:
                cry_display = f"{p_cry:.2f}"
            line = (
                f"cry={cry_display}  ema={ema_prob:.2f}  not_cry={p_not:.2f}  "
                f"rms={curr_rms:.5f}  {'GATED' if gated_silent else '     '}  "
                f"{'ON' if cry_on else 'OFF'}"
            )
            print(line, end="\r", flush=True)


if __name__ == "__main__":
    ap = argparse.ArgumentParser("Realtime ONNX baby-cry detection (tuned for normal volume)")
    ap.add_argument("--model", default="artifacts/crynet_small.onnx")
    ap.add_argument("--on", type=float, default=0.65)
    ap.add_argument("--off", type=float, default=0.40)
    ap.add_argument("--ema", type=float, default=0.35)
    ap.add_argument("--stable_on", type=float, default=0.6, help="Seconds cry prob must stay above --on before switching ON")
    ap.add_argument("--stable_off", type=float, default=0.6, help="Seconds cry prob must stay below --off before switching OFF")
    ap.add_argument("--min_on", type=float, default=0.8)
    ap.add_argument("--min_off", type=float, default=0.4)
    ap.add_argument("--calib", type=float, default=1.5)
    ap.add_argument("--gate_mul", type=float, default=1.5)
    ap.add_argument("--off_rms_mul", type=float, default=1.4, help="When ON, require RMS to drop below gate*mul before allowing OFF")
    ap.add_argument("--no_gate", action="store_true", help="Disable noise gate for ON/OFF")
    ap.add_argument("--block_dur", type=float, default=0.25, help="Seconds of audio per inference hop (controls latency vs stability)")
    ap.add_argument("--smooth_win", type=float, default=0.8, help="Seconds of probability to average before EMA (0 disables)")
    ap.add_argument("--use_prefilter", action="store_true", help="Enable band-pass filter before feature extraction")
    ap.add_argument("--filter_low", type=float, default=300.0, help="Band-pass lower cutoff (Hz)")
    ap.add_argument("--filter_high", type=float, default=3500.0, help="Band-pass upper cutoff (Hz)")
    ap.add_argument("--use_noise_sub", action="store_true", help="Estimate ambient log-mel during calibration and subtract it (requires consistent preprocessing at training for best results)")
    ap.add_argument("--device", type=int, default=None)
    args = ap.parse_args()

    run_stream(
        model_path=args.model,
        on_thr=args.on,
        off_thr=args.off,
        ema_alpha=args.ema,
        stable_on=args.stable_on,
        stable_off=args.stable_off,
        min_on=args.min_on,
        min_off=args.min_off,
        calib_sec=args.calib,
        gate_mul=args.gate_mul,
        off_rms_mul=args.off_rms_mul,
        block_dur=args.block_dur,
        smooth_win=args.smooth_win,
        use_prefilter=args.use_prefilter,
        filter_low=args.filter_low,
        filter_high=args.filter_high,
        use_noise_sub=args.use_noise_sub,
        use_gate=(not args.no_gate),
        device=args.device,
    )
