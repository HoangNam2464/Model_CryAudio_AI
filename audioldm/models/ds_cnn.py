from __future__ import annotations
import torch
import torch.nn as nn
import torch.nn.functional as F


class DSConvBlock(nn.Module):
    def __init__(self, in_ch: int, out_ch: int, k: int = 3):
        super().__init__()
        self.dw = nn.Conv2d(in_ch, in_ch, k, padding=k // 2, groups=in_ch, bias=False)
        self.pw = nn.Conv2d(in_ch, out_ch, 1, bias=False)
        self.bn1 = nn.BatchNorm2d(in_ch)
        self.bn2 = nn.BatchNorm2d(out_ch)

    def forward(self, x):
        x = F.relu(self.bn1(self.dw(x)))
        x = F.relu(self.bn2(self.pw(x)))
        return x


class CryNetDS(nn.Module):
    """
    Depthwise separable CNN for MFCC 20x25 input: x.shape = [B, 1, 20, 25].
    Keep parameter count small for MCU targets.
    """

    def __init__(self, num_classes: int = 2):
        super().__init__()
        self.stem = nn.Conv2d(1, 16, 3, padding=1, bias=False)
        self.bn0 = nn.BatchNorm2d(16)
        self.b1 = DSConvBlock(16, 32)
        self.b2 = DSConvBlock(32, 48)
        self.b3 = DSConvBlock(48, 64)
        self.head = nn.Conv2d(64, num_classes, 1, bias=True)

    def forward(self, x):
        x = F.relu(self.bn0(self.stem(x)))
        x = self.b1(x)
        x = self.b2(x)
        x = self.b3(x)
        x = self.head(x)       # [B, num_classes, H, W]
        x = x.mean(dim=[2, 3]) # global average pooling
        return x


def build_ds_cnn(num_classes: int = 2):
    return CryNetDS(num_classes=num_classes)
