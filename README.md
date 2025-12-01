# AudioCryProject — AI pipeline (DS-CNN, MFCC 20×25)

Repo này chỉ còn phần AI huấn luyện/convert cho mô hình DS-CNN nhỏ, dùng MFCC 20×25 (hop ~80 ms). Mã firmware/Log-Mel/CryNet đã được loại bỏ.

---

## Cấu trúc
```
AudioCryProject/
- audioldm/          # DS-CNN model, MFCC processing, dataset
- artifacts/         # (sinh ra sau train/export)
- convert/           # export_ds_cnn.py, convert_ds_cnn_int8.py, tflite_to_cc.py
- data_new/          # dữ liệu audio (cry/, not_cry/)
- dataset_tools/     # utility cho dữ liệu
- train/             # (nếu có script huấn luyện bổ sung)
- requirements.txt
```

---

## Thiết lập môi trường
```bash
python -m venv venv
source venv/Scripts/activate    # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

---

## Chuẩn bị dữ liệu
- WAV mono 16 kHz (~2 s) vào:
  - `data_new/cry/`
  - `data_new/not_cry/`

---

## Huấn luyện DS-CNN (MFCC 20×25)
```bash
python -m audioldm.train_crynet --data_dir data_new --epochs 30
```
Sinh:
- `artifacts/best_ds_cnn.pth`
- `audioldm/standardization_mfcc.npz` (mean/std MFCC)

---

## Export
1) ONNX:
```bash
python convert/export_ds_cnn.py --ckpt artifacts/best_ds_cnn.pth --out artifacts/ds_cnn.onnx
```
2) TFLite INT8 (cần onnx-tf, và thư mục calib WAV đặt tại `convert/calib/`):
```bash
python convert/convert_ds_cnn_int8.py --onnx artifacts/ds_cnn.onnx
```
3) C array cho nhúng:
```bash
python convert/tflite_to_cc.py artifacts/ds_cnn_int8.tflite --var_name ds_cnn_model --output artifacts/ds_cnn_model.cc
```

---

## Đánh giá / infer nhanh
- `audioldm/evaluate.py` — chạy trên tập val (MFCC 20×25, model DS-CNN).
- `audioldm/infer_file.py` — infer 1 file WAV với checkpoint DS-CNN.
- `realtime.py` — test mic realtime (16 kHz, DS-CNN, MFCC 20×25).

---

## Quy trình nhanh: thu thêm dữ liệu, huấn luyện, kiểm tra

1) **Thu thêm not_cry/cry bằng mic** (mono 16 kHz, 2 giây/clip):
   - Thủ công: `python mic_labeler.py --device_idx <ID_mic> --win_sec 2.0 --root data_new`
     - Enter → lưu `not_cry`, `c` → lưu `cry`, `q` → thoát.
   - Ghi liên tục not_cry: `python mic_labeler.py --device_idx <ID_mic> --win_sec 2.0 --root data_new --auto_not_cry`
   - Để biết ID mic: `python -m sounddevice`

2) **Huấn luyện lại DS-CNN** (tính lại mean/std):
   ```bash
   # xóa stats cũ nếu cần: del audioldm\standardization_mfcc.npz
   python -m audioldm.train_crynet --data_dir data_new --epochs 30 --recompute_stats_if_missing
   ```
   - Checkpoint tốt nhất: `artifacts/best_ds_cnn.pth`

3) **Kiểm tra toàn bộ tập dữ liệu** (tỉ lệ phân biệt cry/not_cry):
   - Dùng `audioldm/evaluate.py` hoặc script soát toàn bộ (ví dụ):
     ```bash
     python -m audioldm.evaluate --data_dir data_new --ckpt artifacts/best_ds_cnn.pth
     ```
   - Hoặc tự viết nhỏ: load checkpoint, quét `data_new/cry` và `data_new/not_cry` → tính confusion.

4) **Test realtime trên mic** (giảm false positive):
   ```bash
   python realtime.py --threshold 0.7 --margin 0.2 --vote_win 3 --rms_gate_db -55 --prefilter --window_sec 2.0 --device_idx <ID_mic>
   ```
   - `threshold`/`margin`/`vote_win` giúp tránh nhầm nhạc.

5) **Export sau khi hài lòng**:
   ```bash
   python convert/export_ds_cnn.py --ckpt artifacts/best_ds_cnn.pth --out artifacts/ds_cnn.onnx
   python convert/convert_ds_cnn_int8.py --onnx artifacts/ds_cnn.onnx   # cần calib WAV ở convert/calib/
   python convert/tflite_to_cc.py artifacts/ds_cnn_int8.tflite --var_name ds_cnn_model --output artifacts/ds_cnn_model.cc
   ```

---

## Artifacts cần giữ (sau khi bạn chạy các bước trên)
- `audioldm/standardization_mfcc.npz`
- `artifacts/best_ds_cnn.pth`
- `artifacts/ds_cnn.onnx`
- `artifacts/ds_cnn_int8.tflite`
- `artifacts/ds_cnn_model.cc`
