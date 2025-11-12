from __future__ import annotations
import random
from pathlib import Path
from typing import List
import numpy as np
import torch
from torch.utils.data import Dataset
from torch.utils.data.sampler import WeightedRandomSampler

from .audio_processing import (
    load_audio,
    to_logmel,
    standardize,
    load_standardization,
    pad_or_trim,
    ensure_frame_length,
    SR,
    TARGET_FRAMES,
)

CLASS_NAMES = ["cry", "not_cry"]
CLASS_TO_ID = {name: idx for idx, name in enumerate(CLASS_NAMES)}

def list_audio_files(root: Path) -> List[Path]:
    """Trả về danh sách tất cả file audio trong thư mục."""
    exts = {".wav", ".mp3", ".flac", ".ogg", ".m4a"}
    return [p for p in root.rglob("*") if p.suffix.lower() in exts]

class CryDataset(Dataset):
    def __init__(self, data_dir: str | Path, split: str = "train",
                 val_ratio: float = 0.15, seed: int = 42,
                 use_global_stats: bool = True):
        super().__init__()
        self.data_dir = Path(data_dir)

        all_pairs = []
        for cname in CLASS_NAMES:
            cdir = self.data_dir / cname
            files = list_audio_files(cdir)
            all_pairs += [(f, CLASS_TO_ID[cname]) for f in files]

        rng = random.Random(seed)
        rng.shuffle(all_pairs)

        n_val = int(len(all_pairs) * val_ratio)
        if split == "train":
            chosen = all_pairs[n_val:]
        elif split == "val":
            chosen = all_pairs[:n_val]
        else:  # 'all' hoặc 'test'
            chosen = all_pairs
        self.index = [(path, lab, 0) for path, lab in chosen]

        # Global stats để chuẩn hoá (nếu có)
        self.mean, self.std = load_standardization() if use_global_stats else (None, None)

    def __len__(self):
        return len(self.index)

    def __getitem__(self, i: int):
        path, lab, _ = self.index[i]
        y, sr = load_audio(path, sr=SR)
        y = pad_or_trim(y, sr=sr)
        logmel = to_logmel(y, sr=sr)
        logmel = standardize(logmel, self.mean, self.std)
        logmel = ensure_frame_length(logmel, TARGET_FRAMES)

        x = torch.from_numpy(logmel).unsqueeze(0)  # (1, n_mels, T)
        ylab = torch.tensor(lab, dtype=torch.long)
        return x, ylab

    def get_labels(self) -> List[int]:
        return [lab for _, lab, _ in self.index]

def make_weighted_sampler(labels: List[int]) -> WeightedRandomSampler:
    """Sampler để cân bằng lớp khi training."""
    from collections import Counter
    cnt = Counter(labels)
    weights = [1.0 / cnt[int(l)] for l in labels]
    return WeightedRandomSampler(weights, num_samples=len(labels), replacement=True)

def labels_from_dataset(ds: CryDataset) -> List[int]:
    """Helper để lấy toàn bộ nhãn của dataset."""
    return ds.get_labels()
