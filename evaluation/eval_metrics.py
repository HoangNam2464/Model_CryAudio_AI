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
from sklearn.metrics import (
    accuracy_score,
    precision_score,
    recall_score,
    f1_score,
    classification_report,
    confusion_matrix,
)

# ✅ Model & class names mới (2 lớp)
ONNX_MODEL = "artifacts/crynet_small.onnx"   # đường dẫn ONNX 2 lớp
CLASS_NAMES = ["cry", "not_cry"]

DURATION = 2.0        # độ dài mỗi mẫu âm thanh (giây)

# -------------------------------------------------------------
# 🎵 Load & tiền xử lý âm thanh
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
    # Đưa về (1, 1, 64, 128)
    return np.expand_dims(logmel, axis=(0, 1)).astype(np.float32)


# -------------------------------------------------------------
# 📈 Đánh giá mô hình ONNX
# -------------------------------------------------------------
def evaluate(data_root="data_new/"):
    if not os.path.exists(ONNX_MODEL):
        raise FileNotFoundError(f"❌ Không tìm thấy model: {ONNX_MODEL}")

    ort_session = ort.InferenceSession(ONNX_MODEL)
    input_name = ort_session.get_inputs()[0].name

    preds, labels = [], []
    for label, folder in enumerate(CLASS_NAMES):
        folder_path = os.path.join(data_root, folder)
        if not os.path.exists(folder_path):
            print(f"⚠️ Bỏ qua {folder_path} (không tồn tại)")
            continue

        for fname in os.listdir(folder_path):
            if not fname.lower().endswith(".wav"):
                continue

            path = os.path.join(folder_path, fname)
            y = load_wav(path)
            feat = extract_features(y)
            out = ort_session.run(None, {input_name: feat})[0]
            pred = np.argmax(out)
            preds.append(pred)
            labels.append(label)

    # 🔹 Tính metric
    acc = accuracy_score(labels, preds)
    prec = precision_score(labels, preds, average="macro", zero_division=0)
    rec = recall_score(labels, preds, average="macro", zero_division=0)
    f1 = f1_score(labels, preds, average="macro", zero_division=0)

    print("\n===== 📊 Evaluation Summary (2 lớp: cry / not_cry) =====")
    print(f"Accuracy  : {acc:.3f}")
    print(f"Precision : {prec:.3f}")
    print(f"Recall    : {rec:.3f}")
    print(f"F1-score  : {f1:.3f}")

    # 🔹 Báo cáo chi tiết từng lớp
    print("\n📑 Classification Report:")
    print(classification_report(labels, preds, target_names=CLASS_NAMES, zero_division=0))

    print("\n🧮 Confusion Matrix:")
    print(confusion_matrix(labels, preds))

    print("\n✅ Evaluation complete.")


if __name__ == "__main__":
    evaluate()
