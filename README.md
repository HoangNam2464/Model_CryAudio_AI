# 🎧 AudioCryProject — Pipeline huấn luyện & xuất mô hình nhận diện tiếng khóc

Hệ thống huấn luyện và xuất mô hình phân loại hai lớp **(cry / not_cry)** có thể chạy được cả trên **máy tính (desktop)** và **ESP32**.  
Dự án này là nền tảng chính cho firmware **AudioCryESP32DevKit**.

---

## 🧩 Cấu trúc thư mục

```
AudioCryProject/
├── audioldm/          # Mã huấn luyện + tiền xử lý (PyTorch)
├── artifacts/         # Checkpoint, model ONNX / TFLite / C array
├── convert/           # Công cụ chuyển đổi định dạng mô hình
├── data_new/          # Dữ liệu âm thanh (cry/, not_cry/)
├── demos/             # Script demo ONNX (offline + realtime)
├── evaluation/        # Đánh giá, kiểm thử mô hình
├── dataset_tools/     # Công cụ hỗ trợ xử lý dữ liệu
└── requirements.txt   # Danh sách thư viện Python cần cài đặt
```

---

## ⚙️ 1. Cài đặt môi trường

```bash
python -m venv venv
.
env\Scripts ctivate
pip install -r requirements.txt
```

---

## 🎙️ 2. Chuẩn bị dữ liệu huấn luyện

Đặt các file âm thanh WAV **mono 16 kHz** (~2 giây) vào hai thư mục sau:
- `data_new/cry/`
- `data_new/not_cry/`

Lần huấn luyện đầu tiên sẽ tự động tính toán giá trị **mean/std toàn cục** và lưu trong `audioldm/standardization.npz`.  
Các script hỗ trợ chia dữ liệu, tăng cường dữ liệu (augmentation),... nằm trong thư mục `dataset_tools/`.

---

## 🧠 3. Huấn luyện mô hình CryNet

```bash
# Mô hình nhỏ - phù hợp cho ESP32
python -m audioldm.train_crynet --model small --data_dir data_new --epochs 30

# Mô hình lớn - cho inference trên desktop
python -m audioldm.train_crynet --model large --data_dir data_new --epochs 30
```

Kết quả huấn luyện (checkpoint, log, biểu đồ) sẽ được lưu trong thư mục `artifacts/`.

---

## 🧾 4. Xuất mô hình

```bash
# Xuất sang ONNX
python convert/export_onnx.py --model small --ckpt artifacts/best_crynet_small.pth

# Chuyển đổi sang TensorFlow SavedModel (yêu cầu onnx-tf)
python convert/convert_crynet.py artifacts/crynet_small.onnx

# Chuyển đổi sang INT8 TFLite (dùng mẫu hiệu chỉnh trong convert/export_tf/assets/calib/)
python convert/convert_crynet_int8.py

# Chuyển TFLite thành mảng C cho firmware ESP32
python convert/tflite_to_cc.py artifacts/crynet_int8.tflite --var_name crynet_int8_model --output artifacts/crynet_int8_model.cc
```

Các file tạo ra sẽ nằm trong `artifacts/`:

- `crynet_small.onnx`
- `crynet_fp32.tflite`, `crynet_fp16.tflite`, `crynet_int8.tflite`
- `crynet_int8_model.cc` (mảng C để nhúng trực tiếp vào firmware)

---

## 📊 5. Đánh giá & demo mô hình

| Script | Chức năng |
|---------|------------|
| `evaluation/eval_metrics.py` | Tính confusion matrix & báo cáo accuracy |
| `evaluation/eval_thresholds.py` | Quét ngưỡng phát hiện |
| `demos/predict_one.py` | Chạy inference ONNX trên 1 file âm thanh |
| `demos/offline_test.py` | Kiểm thử hàng loạt file trong thư mục |
| `demos/realtime_onnx.py` | Demo nhận diện tiếng khóc thời gian thực (mic) |

### 🧩 Demo thời gian thực (Realtime ONNX)
Thiết lập đề xuất cho mic thu trong môi trường yên tĩnh:

```bash
python demos/realtime_onnx.py --model artifacts/crynet_small.onnx --on 0.70 --off 0.18 --ema 0.15 --stable_on 1.0 --stable_off 2.3 --min_on 2.0 --min_off 1.5 --block_dur 0.25 --smooth_win 0.8
```

**Giải thích tham số:**
- `--smooth_win`: khoảng thời gian trượt trung bình xác suất (0–1.5s)  
- `--block_dur`: độ dài mỗi khung xử lý (0.2–0.4s, càng nhỏ càng nhanh nhưng tốn CPU hơn)  
- `--use_prefilter`, `--filter_low`, `--filter_high`: lọc dải tần (band-pass)  
- `--use_noise_sub`: khử nhiễu nền (noise subtraction)

Tất cả script demo đều dùng chung hàm tiền xử lý (`pad_or_trim`, `to_logmel`, `standardize`, `ensure_frame_length`).

---

## 🔩 6. Tích hợp với firmware ESP32

Firmware ESP32 sẽ nhận mô hình **INT8 TFLite** và tái hiện đúng pipeline tiền xử lý:

1. Ghi 1.3s âm thanh PCM ở 16kHz (20 800 mẫu).  
2. Tạo spectrogram log-mel kích thước 64×128 (Hann window 25ms, hop 10ms, FFT 512).  
3. Chuẩn hóa theo mean/std trong `standardization.npz`.  
4. Lượng tử hóa đầu vào theo scale/zero-point và chạy inference.

### Cách triển khai
- Sao chép `artifacts/crynet_int8.tflite` vào `AudioCryESP32/models/` (nếu firmware load TFLite trực tiếp).  
- Hoặc nhúng file `artifacts/crynet_int8_model.cc` vào code C++ với:
  ```cpp
  extern const unsigned char crynet_int8_model[];
  ```

Sau đó build & flash như thông thường bằng **PlatformIO**.

---

## 👤 Tác giả
- **Người phát triển:** nom_05  
- **Dự án:** AudioCryProject (AI model) → AudioCryESP32DevKit (Firmware)  
- **Liên kết GitLab:** [https://gitlab.com/tranvuonghung/sumo](https://gitlab.com/tranvuonghung/sumo)

---

📘 *Mục tiêu:*  
Huấn luyện mô hình AI nhẹ, có khả năng nhận diện tiếng khóc chính xác và hoạt động hiệu quả trên ESP32 trong hệ thống giám sát trẻ em thông minh.
