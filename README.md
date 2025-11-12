# AudioCryProject

Pipeline for training and exporting a two-class (cry / not_cry) classifier that runs both on desktop and on an ESP32 target.

```
AudioCryProject/
├── audioldm/          # Training + preprocessing code (PyTorch)
├── artifacts/         # Checkpoints, exported ONNX / TFLite / C arrays
├── convert/           # Conversion utilities
├── data_new/          # Dataset root (cry/, not_cry/)
├── demos/             # ONNX inference helpers (offline + realtime)
├── evaluation/        # Validation scripts
├── dataset_tools/     # Optional dataset utilities
└── requirements.txt
```

## 1. Install environment

```bash
python -m venv venv
.\venv\Scripts\activate
pip install -r requirements.txt
```

## 2. Prepare dataset

Place mono 16 kHz WAV files (~2 s) inside:
- `data_new/cry/`
- `data_new/not_cry/`

The first training run computes global mean/std and stores them in `audioldm/standardization.npz`. Dataset helpers (augmentation, splitting, etc.) live in `dataset_tools/`.

## 3. Train CryNet

```bash
# small footprint model (recommended for ESP32)
python -m audioldm.train_crynet --model small --data_dir data_new --epochs 30

# larger model for desktop inference
python -m audioldm.train_crynet --model large --data_dir data_new --epochs 30
```

Artifacts (best checkpoint, training log) are saved in `artifacts/`.

## 4. Export models

```bash
# ONNX export
python convert/export_onnx.py --model small --ckpt artifacts/best_crynet_small.pth

# TensorFlow SavedModel (requires onnx-tf)
python convert/convert_crynet.py artifacts/crynet_small.onnx

# INT8 TFLite (uses convert/export_tf/assets/calib/*.wav for calibration)
python convert/convert_crynet_int8.py

# Convert TFLite into a C array for ESP32 firmware
python convert/tflite_to_cc.py artifacts/crynet_int8.tflite --var_name crynet_int8_model --output artifacts/crynet_int8_model.cc
```

Generated files appear under `artifacts/`:

- `crynet_small.onnx`
- `crynet_fp32.tflite`, `crynet_fp16.tflite`, `crynet_int8.tflite`
- `crynet_int8_model.cc` (C array for firmware embedding)

## 5. Evaluate / demos

- `evaluation/eval_metrics.py` – confusion matrix + report on `data_new/`
- `evaluation/eval_thresholds.py` – sweep detection thresholds
- `demos/predict_one.py` – run ONNX model on a single file
- `demos/offline_test.py` – batch evaluate a folder
- `demos/realtime_onnx.py` – realtime microphone demo (requires `sounddevice`)

### Realtime demo (suggested settings)

The following profile yielded stable detections on a typical indoor setup (micro close to source, moderate background noise):

```
python demos/realtime_onnx.py --model artifacts/crynet_small.onnx --on 0.70 --off 0.18 --ema 0.15 --stable_on 1.0 --stable_off 2.3 --min_on 2.0 --min_off 1.5 --block_dur 0.25 --smooth_win 0.8
```

Notes:
- `--smooth_win` (0–1.5 s) averages raw probabilities before the EMA step; increase for smoother readouts, decrease for faster reaction.
- `--block_dur` (0.2–0.4 s) controls the hop size; shorter blocks reduce latency at the cost of more CPU usage.
- Optional cleanup in noisy environments: add `--use_prefilter --filter_low 300 --filter_high 3500` for band-pass filtering, and `--use_noise_sub` to subtract ambient log-mel energy captured during calibration.

All scripts automatically share the preprocessing utilities (`pad_or_trim`, `to_logmel`, `standardize`, `ensure_frame_length`).

## 6. ESP32 firmware (AudioCryESP32)

The firmware consumes the INT8 model and mirrors the training preprocessing:

1. Capture 1.3 s PCM at 16 kHz (20 800 samples).  
2. Produce a 64×128 log-mel spectrogram (Hann window 25 ms, hop 10 ms, FFT 512).  
3. Apply the stored mean/std vectors (`standardization.npz`).  
4. Quantise with the model input scale/zero-point and run inference.

Deployment options:
- Copy `artifacts/crynet_int8.tflite` into `AudioCryESP32/models/` (if the firmware loads TFLite flatbuffers).
- Or embed the generated `artifacts/crynet_int8_model.cc` directly in the firmware (`extern const unsigned char crynet_int8_model[];`).

Build and flash via PlatformIO (or your chosen toolchain) as usual.
