from __future__ import annotations
import argparse
import json
import random
from pathlib import Path
import numpy as np
import torch
from torch.utils.data import DataLoader
from torch.optim import AdamW
from torch.optim.lr_scheduler import CosineAnnealingLR
from sklearn.metrics import f1_score, accuracy_score, precision_recall_fscore_support
from tqdm import tqdm
from torch import amp
from torch.cuda.amp import GradScaler

# ✅ sửa import: dùng tuyệt đối để tránh lỗi relative import
from audioldm.models.crynet import build_crynet_small, build_crynet_large
from audioldm.dataset import CryDataset, labels_from_dataset, make_weighted_sampler
from audioldm.audio_processing import load_standardization, save_standardization

def set_seed(seed: int = 1234):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


def compute_dataset_stats(ds: CryDataset, max_files: int = 200):
    arrs = []
    for i in range(min(max_files, len(ds))):
        x, _ = ds[i]
        x = x.numpy()
        if x.ndim == 3:
            x = x[0]
        arrs.append(x)
    big = np.stack(arrs, axis=0)
    mean = big.mean(axis=(0, 2))
    std = big.std(axis=(0, 2)) + 1e-6
    return mean.astype(np.float32), std.astype(np.float32)


def train_one_epoch(model, loader, optimizer, scaler, device, n_classes=2):
    model.train()
    losses, y_true, y_pred = [], [], []
    criterion = torch.nn.CrossEntropyLoss()

    for xb, yb in tqdm(loader, desc="train", leave=False):
        xb, yb = xb.to(device), yb.to(device)
        optimizer.zero_grad(set_to_none=True)

        if device.type == "cuda":
            with amp.autocast(device_type="cuda"):
                logits = model(xb)
                loss = criterion(logits, yb)
            scaler.scale(loss).backward()
            scaler.step(optimizer)
            scaler.update()
        else:
            logits = model(xb)
            loss = criterion(logits, yb)
            loss.backward()
            optimizer.step()

        losses.append(loss.item())
        y_true.extend(yb.detach().cpu().tolist())
        y_pred.extend(logits.argmax(dim=-1).detach().cpu().tolist())

    acc = accuracy_score(y_true, y_pred)
    f1 = f1_score(y_true, y_pred, average="macro", zero_division=0)
    return float(np.mean(losses)), acc, f1


@torch.no_grad()
def evaluate(model, loader, device, n_classes=2):
    model.eval()
    losses, y_true, y_pred = [], [], []
    criterion = torch.nn.CrossEntropyLoss()

    for xb, yb in tqdm(loader, desc="val", leave=False):
        xb, yb = xb.to(device), yb.to(device)
        if device.type == "cuda":
            with amp.autocast(device_type="cuda"):
                logits = model(xb)
                loss = criterion(logits, yb)
        else:
            logits = model(xb)
            loss = criterion(logits, yb)

        losses.append(loss.item())
        y_true.extend(yb.detach().cpu().tolist())
        y_pred.extend(logits.argmax(dim=-1).detach().cpu().tolist())

    acc = accuracy_score(y_true, y_pred)
    p, r, f1, _ = precision_recall_fscore_support(
        y_true, y_pred, average="macro", zero_division=0
    )
    return float(np.mean(losses)), acc, p, r, f1


# ------------------------------
# Main
# ------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data_dir", type=str, default="data_new")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--batch_size", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--model", choices=["small", "large"], default="small")
    ap.add_argument("--num_workers", type=int, default=0)
    ap.add_argument("--recompute_stats_if_missing", action="store_true")
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--log_json", type=str, default="training_log.json")
    args = ap.parse_args()

    set_seed(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"👉 Using device: {device}")
    if device.type == "cuda":
        try:
            print("GPU:", torch.cuda.get_device_name(0))
        except Exception:
            pass

    artifacts_dir = Path("artifacts")
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    # 1️⃣ Chuẩn hóa dataset nếu cần
    mean, std = load_standardization()
    if mean is None or std is None or args.recompute_stats_if_missing:
        raw_train = CryDataset(args.data_dir, split="train", use_global_stats=False)
        mean, std = compute_dataset_stats(raw_train, max_files=200)
        save_standardization(mean, std)
        print("✅ Saved new standardization.npz")

    # 2️⃣ Dataset có chuẩn hoá
    train_ds = CryDataset(args.data_dir, split="train", use_global_stats=True)
    val_ds = CryDataset(args.data_dir, split="val", use_global_stats=True)

    # 3️⃣ DataLoader
    try:
        train_labels = labels_from_dataset(train_ds)
        sampler = make_weighted_sampler(train_labels)
        shuffle = False
    except Exception:
        sampler, shuffle = None, True
        print("ℹ️ Weighted sampler disabled, fallback shuffle=True")

    train_loader = DataLoader(
        train_ds,
        batch_size=args.batch_size,
        sampler=sampler,
        shuffle=shuffle,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
    )
    val_loader = DataLoader(
        val_ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
    )

    # 4️⃣ Model + Optimizer
    n_classes = 2
    if args.model == "small":
        model = build_crynet_small(n_mels=64, n_classes=n_classes).to(device)
        ckpt_path = artifacts_dir / "best_crynet_small.pth"
    else:
        model = build_crynet_large(n_mels=64, n_classes=n_classes).to(device)
        ckpt_path = artifacts_dir / "best_crynet_large.pth"

    optimizer = AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = CosineAnnealingLR(optimizer, T_max=args.epochs)
    scaler = GradScaler(enabled=(device.type == "cuda"))

    # 5️⃣ Train loop
    best_f1 = -1.0
    log = {"loss": [], "val_acc": [], "val_f1": []}

    for epoch in range(1, args.epochs + 1):
        print(f"\nEpoch {epoch}/{args.epochs}")
        tr_loss, tr_acc, tr_f1 = train_one_epoch(model, train_loader, optimizer, scaler, device, n_classes)
        vl_loss, vl_acc, vl_p, vl_r, vl_f1 = evaluate(model, val_loader, device, n_classes)
        scheduler.step()

        print(f"train: loss={tr_loss:.4f} acc={tr_acc:.3f} f1={tr_f1:.3f}")
        print(f"  val: loss={vl_loss:.4f} acc={vl_acc:.3f} P={vl_p:.3f} R={vl_r:.3f} F1={vl_f1:.3f}")

        log["loss"].append(tr_loss)
        log["val_acc"].append(vl_acc)
        log["val_f1"].append(vl_f1)
        with open(args.log_json, "w", encoding="utf-8") as f:
            json.dump(log, f)

        if vl_f1 > best_f1:
            best_f1 = vl_f1
            torch.save(model.state_dict(), ckpt_path)
            print(f"✅ Saved new best model to {ckpt_path} (F1={vl_f1:.3f})")

    print(f"Done. Best F1={best_f1:.3f}")


if __name__ == "__main__":
    main()
