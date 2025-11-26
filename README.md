# AudioCryESP32DevKit - ESP32 AI Cry Detection Firmware

Firmware ESP32 nhan dien tieng khoc tre em va day trang thai/GPS len server. Phat trien bang PlatformIO (Arduino framework) tren VSCode.

## Huong dan ket noi Wi-Fi va xem IP
- Khi chua co Wi-Fi hop le, board bat SoftAP cau hinh: SSID `AudioCry-Setup-XXXX`, password `12345678` (XXXX la 4 hex cuoi MAC).
- Ket noi dien thoai/laptop vao AP tren, mo trinh duyet: `http://192.168.4.1/wifi`.
- Nhap SSID va password Wi-Fi nha ban, bam **Luu** (co nut **Xoa** de xoa Wi-Fi cu).
- Doi khoang 5-15 giay: khi ket noi thanh cong, Serial log se in `[WiFi] Link active: SSID=... IP=...` va LED Wi-Fi (neu co) sang.
- Lay IP cua board:
  - Mo Serial Monitor 115200 va tim dong IP nhu tren.
  - Hoac truy cap `http://<ip-cua-board>/wifi` de xem SSID/IP, `http://<ip-cua-board>/status` de lay JSON trang thai (cry/gps/last_event).
- Neu sai mat khau/khong tim thay AP, board tiep tuc giu SoftAP de nhap lai Wi-Fi.

## Cau truc thu muc
AudioCryESP32/
- lib/                 # Thu vien noi bo + third-party
  - CryDetector/       # AI inference nhan dien tieng khoc
  - Gps/               # Xu ly module GPS NEO-6M
  - RestClient/        # GUI du lieu REST API
  - TflmInfer/         # TensorFlow Lite Micro runtime
  - ... submodule khac (flatbuffers, kissfft, gemmlowp, tflite-micro-minimal)
- src/                 # Ma nguon chinh (main.cpp, wifi, api_server, ...)
- include/             # Header chung
- platformio.ini       # Cau hinh build PlatformIO
- README.md            # Tai lieu huong dan

## Cach build va upload firmware
- Cai VSCode + extension PlatformIO.
- Clone du an (ke ca submodule): `git clone --recursive https://gitlab.com/tranvuonghung/sumo.git`
- Mo thu muc `AudioCryESP32` trong VSCode.
- Cam ESP32 qua USB, chon cong COM dung.
- Upload: `pio run --target upload`
- Theo doi log: `pio device monitor` (115200 baud).

## Cap nhat submodule khi pull/clone
Chay: `git submodule update --init --recursive`

## Tac gia
- Nguoi phat trien: nom_05
- Du an goc: SUMO / Model_CryESP32
- GitLab: https://gitlab.com/tranvuonghung/sumo

## Luu y khac
- ESP32-S3 chi ho tro 2.4 GHz Wi-Fi; bat che do Mixed b/g/n tren router.
- Nhap dung SSID/password, khong de ky tu trang o dau/cuoi.
