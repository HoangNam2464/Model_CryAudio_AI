import sys
import os

sys.path.append(os.path.dirname(os.path.dirname(__file__)))

import numpy as np
import librosa
import onnxruntime as ort
from audioldm.audio_processing import (
    to_logmel,
    standardize,
    load_standardization,
    pad_or_trim,
    ensure_frame_length,
    SR,
    TARGET_FRAMES,
)
from sklearn.metrics import accuracy_score, precision_score, recall_score, f1_score

# ✅ Mô hình 2 lớp mới
ONNX_MODEL = "artifacts/crynet_small.onnx"
CLASS_NAMES = ["cry", "not_cry"]

DURATION = 2.0  # mỗi mẫu 2 giây


# -------------------------------------------------------------
# 🎵 Xử lý âm thanh đầu vào
# -------------------------------------------------------------
def load_wav(path, sr=SR, duration=DURATION):
    y, _ = librosa.load(path, sr=sr)
    y = pad_or_trim(y, sr=sr, duration=duration)
    return y


def extract_features(y, sr=SR):
    logmel = to_logmel(y, sr)
    mean, std = load_standardization("audioldm/standardization.npz")
    logmel = standardize(logmel, mean, std)

    logmel = ensure_frame_length(logmel, TARGET_FRAMES)
    return np.expand_dims(logmel, axis=(0, 1)).astype(np.float32)


# -------------------------------------------------------------
# 📊 Đánh giá mô hình với nhiều threshold
# -------------------------------------------------------------
def evaluate(data_root="data_new/"):
    if not os.path.exists(ONNX_MODEL):
        raise FileNotFoundError(f"❌ Không tìm thấy model: {ONNX_MODEL}")

    ort_session = ort.InferenceSession(ONNX_MODEL)
    input_name = ort_session.get_inputs()[0].name

    preds, labels, probs = [], [], []

    # Duyệt qua từng thư mục lớp
    for label, folder in enumerate(CLASS_NAMES):
        folder_path = os.path.join(data_root, folder)
        if not os.path.exists(folder_path):
            print(f"⚠️ Bỏ qua {folder_path} (không tồn tại)")
            continue

        for fname in os.listdir(folder_path):
            if not fname.lower().endswith(".wav"):
                continue

            y = load_wav(os.path.join(folder_path, fname))
            feat = extract_features(y)
            out = ort_session.run(None, {input_name: feat})[0]  # softmax (2,)
            probs.append(out[0])  # xác suất [cry, not_cry]
            labels.append(label)

    probs = np.array(probs)    # (N, 2)
    labels = np.array(labels)  # (N,)

    # Lấy xác suất "cry" (index=0)
    cry_probs = probs[:, 0]
    print("\n===== 📈 Threshold Sweep (2 lớp: cry / not_cry) =====")

    best_f1, best_th = -1, 0
    for th in np.linspace(0.1, 0.9, 9):
        preds_bin = (cry_probs >= th).astype(int)
        labels_bin = (labels == 0).astype(int)

        acc = accuracy_score(labels_bin, preds_bin)
        prec = precision_score(labels_bin, preds_bin, zero_division=0)
        rec = recall_score(labels_bin, preds_bin, zero_division=0)
        f1 = f1_score(labels_bin, preds_bin, zero_division=0)

        print(f"Threshold={th:.1f} | Acc={acc:.2f}, Prec={prec:.2f}, Rec={rec:.2f}, F1={f1:.2f}")

        if f1 > best_f1:
            best_f1, best_th = f1, th

    print("\n✅ Best threshold =", round(best_th, 2), f"(F1={best_f1:.3f})")


if __name__ == "__main__":
    evaluate()
