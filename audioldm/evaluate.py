from __future__ import annotations
import argparse
import torch
from torch.utils.data import DataLoader
from sklearn.metrics import classification_report, confusion_matrix

from audioldm.models.ds_cnn import build_ds_cnn
from audioldm.dataset import CryDataset

CLASS_NAMES = ["cry", "not_cry"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data_dir", type=str, default="data_new", help="Thư mục chứa dữ liệu cry/not_cry")
    ap.add_argument("--ckpt", type=str, default="artifacts/best_ds_cnn.pth", help="Đường dẫn model .pth")
    ap.add_argument("--batch_size", type=int, default=64)
    ap.add_argument("--num_workers", type=int, default=0)
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")

    ds = CryDataset(args.data_dir, split="val", use_global_stats=True)
    dl = DataLoader(
        ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
    )

    model = build_ds_cnn(num_classes=2).to(device)
    state = torch.load(args.ckpt, map_location=device)
    if isinstance(state, dict) and "model" in state:
        state = state["model"]
    model.load_state_dict(state)
    model.eval()

    y_true, y_pred = [], []
    with torch.no_grad():
        for xb, yb in dl:
            xb = xb.to(device)
            logits = model(xb)
            y_true.extend(yb.cpu().numpy().tolist())
            y_pred.extend(logits.argmax(dim=-1).cpu().numpy().tolist())

    print("\n===== Evaluation Report =====")
    print(classification_report(
        y_true,
        y_pred,
        target_names=CLASS_NAMES,
        digits=4
    ))
    print("===== Confusion Matrix =====")
    print(confusion_matrix(y_true, y_pred))
    print("\nDone.")


if __name__ == "__main__":
    main()
