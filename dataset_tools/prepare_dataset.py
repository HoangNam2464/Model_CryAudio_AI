import sys, os
sys.path.append(os.path.dirname(os.path.dirname(__file__)))

import librosa
import soundfile as sf

# -------------------------------------------------------------
# ⚙️ Cấu hình
# -------------------------------------------------------------
SR = 16000        # tần số lấy mẫu
DURATION = 2.0    # độ dài mỗi mẫu (giây)
SAMPLES = int(SR * DURATION)

# ✅ Chỉ còn 2 lớp
CLASSES = ["cry", "not_cry"]

# -------------------------------------------------------------
# 🎵 Hàm xử lý từng file âm thanh
# -------------------------------------------------------------
def process_file(in_path, out_path):
    """Đọc file, resample, cắt hoặc đệm cho đúng 2 giây."""
    try:
        y, _ = librosa.load(in_path, sr=SR, mono=True)
    except Exception as e:
        print(f"⚠️ Lỗi đọc {in_path}: {e}")
        return

    # Cắt hoặc đệm tín hiệu
    if len(y) < SAMPLES:
        y = librosa.util.fix_length(y, size=SAMPLES)
    else:
        y = y[:SAMPLES]

    # Ghi lại file chuẩn hóa
    sf.write(out_path, y, SR)


# -------------------------------------------------------------
# 🧩 Chuẩn bị toàn bộ dataset
# -------------------------------------------------------------
def prepare(input_root="data_goc", output_root="data_new"):
    """
    Chuẩn hóa toàn bộ dữ liệu đầu vào (âm thanh 16kHz, 2 giây)
    - input_root: thư mục chứa dữ liệu gốc
    - output_root: thư mục xuất dữ liệu chuẩn hóa
    """
    for label in CLASSES:
        in_dir = os.path.join(input_root, label)
        out_dir = os.path.join(output_root, label)

        if not os.path.exists(in_dir):
            print(f"⚠️ Bỏ qua vì chưa có thư mục: {in_dir}")
            continue

        os.makedirs(out_dir, exist_ok=True)

        for fname in os.listdir(in_dir):
            if not fname.lower().endswith(".wav"):
                continue

            in_path = os.path.join(in_dir, fname)
            out_path = os.path.join(out_dir, fname)

            process_file(in_path, out_path)
            print(f"✅ Processed: {fname} -> {out_dir}")


# -------------------------------------------------------------
# 🚀 Chạy chính
# -------------------------------------------------------------
if __name__ == "__main__":
    prepare()
