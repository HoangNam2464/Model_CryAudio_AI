from __future__ import annotations
import argparse
import torch
from torch.utils.data import DataLoader
from sklearn.metrics import classification_report, confusion_matrix

# ✅ dùng import tuyệt đối để tránh lỗi khi chạy ngoài package
from audioldm.models.crynet import build_crynet
from audioldm.dataset import CryDataset

CLASS_NAMES = ["cry", "not_cry"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data_dir", type=str, default="data_new",
                    help="Thư mục chứa dữ liệu (có cry/ và not_cry/)")
    ap.add_argument("--ckpt", type=str,
                    default="artifacts/best_crynet_small.pth",
                    help="Đường dẫn tới mô hình .pth cần đánh giá")
    ap.add_argument("--batch_size", type=int, default=64)
    ap.add_argument("--num_workers", type=int, default=0)
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"👉 Using device: {device}")

    # 🧩 Dataset validation
    ds = CryDataset(args.data_dir, split="val", use_global_stats=True)
    dl = DataLoader(
        ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
    )

    # 🧠 Load model (2 lớp)
    model = build_crynet(model_size="small", n_mels=64, n_classes=2).to(device)
    state = torch.load(args.ckpt, map_location=device)

    # Cho phép cả dạng {"model": state_dict} hoặc state_dict trực tiếp
    if "model" in state:
        state = state["model"]
    model.load_state_dict(state)
    model.eval()

    # 🔍 Evaluate
    y_true, y_pred = [], []
    with torch.no_grad():
        for xb, yb in dl:
            xb = xb.to(device)
            logits = model(xb)
            y_true.extend(yb.cpu().numpy().tolist())
            y_pred.extend(logits.argmax(dim=-1).cpu().numpy().tolist())

    # 📊 Báo cáo kết quả
    print("\n===== 📈 Evaluation Report =====")
    print(classification_report(
        y_true,
        y_pred,
        target_names=["cry", "not_cry"],
        digits=4
    ))
    print("===== 🧮 Confusion Matrix =====")
    print(confusion_matrix(y_true, y_pred))
    print("\n✅ Done.")


if __name__ == "__main__":
    main()
