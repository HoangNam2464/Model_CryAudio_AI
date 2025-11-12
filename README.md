# ⚙️ AudioCryESP32DevKit — ESP32 AI Cry Detection Firmware

## 🧩 Giới thiệu
**AudioCryESP32DevKit** là firmware chạy trên ESP32, tích hợp mô hình AI để nhận diện tiếng khóc trẻ em và truyền dữ liệu định vị GPS thời gian thực về máy chủ.  
Dự án được phát triển bằng **PlatformIO** trên **VSCode**, sử dụng mô hình AI đã huấn luyện từ `AudioCryProject`.

---

## 🛠 Cấu trúc thư mục
```
AudioCryESP32/
│
├── lib/                 # Thư viện người dùng và third-party
│   ├── CryDetector/     # Nhận diện tiếng khóc (AI inference trên ESP32)
│   ├── Gps/             # Xử lý module NEO-6M GPS
│   ├── RestClient/      # Gửi dữ liệu REST API tới server Laravel/Vue
│   ├── TflmInfer/       # TensorFlow Lite Micro runtime cho model .tflite
│   ├── flatbuffers/     # (submodule, bỏ qua khi push)
│   ├── gemmlowp/        # (submodule, bỏ qua khi push)
│   ├── kissfft/         # (submodule, bỏ qua khi push)
│   └── tflite-micro-minimal/  # (submodule, bỏ qua khi push)
│
├── src/                 # Mã nguồn chính (main.cpp, wifi, mqtt, log, ...)
├── include/             # Header chung
├── .gitignore           # Bỏ qua file build, log, cache, libs bên thứ ba
├── platformio.ini       # Cấu hình build PlatformIO
└── README.md            # Tài liệu hướng dẫn
```

---

## 🧠 Các thư viện chính sử dụng
| Thành phần | Mô tả |
|-------------|-------|
| **TensorFlow Lite Micro (TFLM)** | Chạy mô hình nhận diện tiếng khóc (.tflite) trên ESP32 |
| **Flatbuffers** | Parser cho mô hình TFLite |
| **KissFFT** | Tính toán FFT cho xử lý tín hiệu âm thanh |
| **Gemmlowp** | Toán học ma trận tối ưu cho ESP32 |
| **NEO-6M GPS** | Lấy vị trí (vĩ độ, kinh độ) |
| **MAX98357A** | Giải mã I2S cho âm thanh phát loa |
| **INMP441 Mic** | Ghi âm tín hiệu tiếng khóc (I2S input) |

---

## 🔧 Cách build & upload firmware
1. Cài [**VSCode**](https://code.visualstudio.com/) và extension [**PlatformIO**](https://platformio.org/install/ide?install=vscode)
2. Clone dự án:
   ```bash
   git clone --recursive https://gitlab.com/tranvuonghung/sumo.git
   ```
3. Mở thư mục `AudioCryESP32` trong VSCode  
4. Kết nối ESP32 qua USB  
5. Nhấn **PlatformIO → Upload** hoặc chạy:
   ```bash
   pio run --target upload
   ```
6. Theo dõi log:
   ```bash
   pio device monitor
   ```

---

## 🚀 Khi clone hoặc pull về máy khác
Vì dự án dùng **submodule** (Flatbuffers, KissFFT, v.v.), cần tải về đầy đủ bằng:
```bash
git submodule update --init --recursive
```

---

## 👤 Tác giả
- **Người phát triển:** nom_05  
- **Dự án:** SUMO → Model_CryESP32  
- **Liên kết GitLab:** [https://gitlab.com/tranvuonghung/sumo](https://gitlab.com/tranvuonghung/sumo)

---

📘 *Mục tiêu của dự án:*  
Tạo nền tảng firmware ESP32 hỗ trợ mô hình AI nhận diện tiếng khóc và định vị GPS chính xác, mở rộng khả năng giám sát trẻ em thông minh trong hệ sinh thái **AudioCry**.
