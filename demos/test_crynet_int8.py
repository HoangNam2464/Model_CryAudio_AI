import numpy as np
import librosa
import tensorflow as tf
import os

# ==============================
# ⚙️ Cấu hình
# ==============================
MODEL_PATH = "crynet_int8.tflite"      # đường dẫn model
AUDIO_PATH = "test_audio.wav"          # file âm thanh cần test
CLASS_NAMES = ["cry", "not_cry"]       # 2 lớp
SAMPLE_RATE = 16000                    # tần số lấy mẫu
DURATION = 2.0                         # độ dài 2 giây

# ==============================
# 🎵 Xử lý âm thanh
# ==============================
def pad_or_trim(y, sr, duration=2.0):
    target_len = int(sr * duration)
    if len(y) > target_len:
        y = y[:target_len]
    elif len(y) < target_len:
        y = np.pad(y, (0, target_len - len(y)))
    return y

def extract_logmel(y, sr=16000, n_mels=64):
    """Trích xuất đặc trưng log-mel để đưa vào model"""
    mel_spec = librosa.feature.melspectrogram(
        y=y, sr=sr, n_mels=n_mels, n_fft=1024, hop_length=160
    )
    logmel = librosa.power_to_db(mel_spec, ref=np.max)
    return logmel.astype(np.float32)

# ==============================
# 🧠 Nạp mô hình TFLite
# ==============================
interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

print("✅ Model loaded successfully!")
print(f"Input shape: {input_details[0]['shape']}")
print(f"Output shape: {output_details[0]['shape']}")

# ==============================
# 🔊 Nạp & tiền xử lý âm thanh
# ==============================
if not os.path.exists(AUDIO_PATH):
    raise FileNotFoundError(f"Không tìm thấy file âm thanh: {AUDIO_PATH}")

y, sr = librosa.load(AUDIO_PATH, sr=SAMPLE_RATE, mono=True)
y = pad_or_trim(y, sr, DURATION)
logmel = extract_logmel(y, sr=sr)

# chuẩn hóa về đúng shape mô hình (1, 64, 128, 1)
logmel = np.expand_dims(logmel, axis=(0, -1))
logmel = logmel.astype(np.float32)

# ==============================
# 🚀 Dự đoán
# ==============================
interpreter.set_tensor(input_details[0]['index'], logmel)
interpreter.invoke()
output_data = interpreter.get_tensor(output_details[0]['index'])

pred_idx = int(np.argmax(output_data))
pred_label = CLASS_NAMES[pred_idx]
confidence = float(np.max(output_data))

print("=====================================")
print(f"🎯 Dự đoán: {pred_label}")
print(f"🔢 Xác suất: {confidence:.4f}")
print(f"📊 Vector đầu ra: {output_data}")
print("=====================================")
