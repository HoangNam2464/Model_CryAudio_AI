import sys, os
sys.path.append(os.path.dirname(os.path.dirname(__file__)))

import librosa
import soundfile as sf
import numpy as np
from pathlib import Path

# -------------------------------------------------------------
# ⚙️ Cấu hình
# -------------------------------------------------------------
SR = 16000                          # tần số lấy mẫu
SOURCE_DIR = Path("data_new/cry")   # thư mục gốc chứa các file cry
AUG_DIR = Path("data_new/cry_aug")  # nơi lưu file augment

AUG_DIR.mkdir(parents=True, exist_ok=True)

# -------------------------------------------------------------
# 🎵 Các hàm augment
# -------------------------------------------------------------
def add_noise(y, noise_level=0.005):
    """Thêm nhiễu trắng vào tín hiệu."""
    noise = np.random.randn(len(y)) * noise_level
    return y + noise

def time_stretch(y, rate):
    """Giãn hoặc nén thời gian."""
    return librosa.effects.time_stretch(y, rate=rate)

def pitch_shift(y, sr, n_steps):
    """Dịch cao độ lên hoặc xuống n_steps bán cung."""
    return librosa.effects.pitch_shift(y, sr=sr, n_steps=n_steps)


# -------------------------------------------------------------
# 🧩 Hàm xử lý từng file
# -------------------------------------------------------------
def process_file(path: Path):
    """Tạo các phiên bản augment cho 1 file cry."""
    y, sr = librosa.load(path, sr=SR)
    y = y.astype(float)  # tránh lỗi float32/float64
    base = path.stem

    # 1️⃣ Giữ bản gốc (copy)
    sf.write(AUG_DIR / f"{base}_orig.wav", y, sr)

    # 2️⃣ Time stretch (±10%)
    for rate in [0.9, 1.1]:
        try:
            y_stretch = time_stretch(y, rate)
            sf.write(AUG_DIR / f"{base}_stretch{rate:.1f}.wav", y_stretch, sr)
        except Exception as e:
            print(f"⚠️ Skip stretch {rate} for {base}: {e}")

    # 3️⃣ Pitch shift (±2 bán cung)
    for step in [-2, 2]:
        try:
            y_pitch = pitch_shift(y, sr, n_steps=step)
            sf.write(AUG_DIR / f"{base}_pitch{step:+d}.wav", y_pitch, sr)
        except Exception as e:
            print(f"⚠️ Skip pitch {step} for {base}: {e}")

    # 4️⃣ Add noise nhẹ
    y_noise = add_noise(y, 0.01)
    sf.write(AUG_DIR / f"{base}_noise.wav", y_noise, sr)


# -------------------------------------------------------------
# 🚀 Chạy chính
# -------------------------------------------------------------
def main():
    cry_dir = SOURCE_DIR
    if not cry_dir.exists():
        print(f"❌ Không tìm thấy thư mục {cry_dir}")
        return

    files = list(cry_dir.glob("*.wav"))
    print(f"🎧 Found {len(files)} cry files — augmenting...")

    for f in files:
        process_file(f)

    print(f"✅ Augmented files saved to: {AUG_DIR}")


if __name__ == "__main__":
    main()
