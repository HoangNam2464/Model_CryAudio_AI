# audioldm/models/crynet.py
from __future__ import annotations
import torch
import torch.nn as nn
import torch.nn.functional as F

class ConvBlock(nn.Module):
    def __init__(self, in_ch: int, out_ch: int, pool: tuple[int, int] = (2, 2), p: float = 0.1):
        super().__init__()
        self.conv1 = nn.Conv2d(in_ch, out_ch, kernel_size=3, padding=1, bias=False)
        self.bn1   = nn.BatchNorm2d(out_ch)
        self.conv2 = nn.Conv2d(out_ch, out_ch, kernel_size=3, padding=1, bias=False)
        self.bn2   = nn.BatchNorm2d(out_ch)
        self.pool  = nn.MaxPool2d(kernel_size=pool) if pool else nn.Identity()
        self.drop  = nn.Dropout(p)

    def forward(self, x):
        x = F.relu(self.bn1(self.conv1(x)))
        x = F.relu(self.bn2(self.conv2(x)))
        x = self.pool(x)
        x = self.drop(x)
        return x

class CryNet(nn.Module):
    def __init__(self, n_mels: int = 64, n_classes: int = 2, width: int = 32, p: float = 0.2):
        super().__init__()
        self.features = nn.Sequential(
            ConvBlock(1,        width,      pool=(2, 2), p=p),
            ConvBlock(width,    width * 2,  pool=(2, 2), p=p),
            ConvBlock(width*2,  width * 4,  pool=(2, 2), p=p),
            ConvBlock(width*4,  width * 4,  pool=(1, 2), p=p),
        )
        self.gap = nn.AdaptiveAvgPool2d((1, 1))
        self.head = nn.Sequential(
            nn.Flatten(),
            nn.Linear(width * 4, width * 2),
            nn.ReLU(inplace=True),
            nn.Dropout(p),
            nn.Linear(width * 2, n_classes),
        )

    def forward(self, x):
        x = self.features(x)
        x = self.gap(x)
        x = self.head(x)
        return x

    @torch.no_grad()
    def predict_proba(self, x):
        self.eval()
        logits = self.forward(x)
        return torch.softmax(logits, dim=-1)

    @torch.no_grad()
    def predict(self, x):
        proba = self.predict_proba(x)
        return proba.argmax(dim=-1)

def build_crynet_small(n_mels=64, n_classes=2):
    """Mô hình nhỏ cho ESP32 hoặc realtime"""
    return CryNet(n_mels=n_mels, n_classes=n_classes, width=16)

def build_crynet_large(n_mels=64, n_classes=2):
    """Mô hình lớn hơn cho PC / GPU"""
    return CryNet(n_mels=n_mels, n_classes=n_classes, width=32)

def build_crynet(model_size: str = "small", n_mels: int = 64, n_classes: int = 2):
    """Chọn kích thước mô hình (small hoặc large)"""
    if model_size == "small":
        return build_crynet_small(n_mels=n_mels, n_classes=n_classes)
    elif model_size == "large":
        return build_crynet_large(n_mels=n_mels, n_classes=n_classes)
    else:
        raise ValueError(f"Unknown model_size: {model_size}")

