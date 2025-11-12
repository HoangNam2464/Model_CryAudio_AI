# -*- coding: utf-8 -*-
"""
audioldm package
----------------
Gói chính cho toàn bộ pipeline nhận diện tiếng khóc em bé.
Bao gồm các module:
- models: Kiến trúc mạng CryNet (small / large)
- dataset: Xử lý dữ liệu âm thanh huấn luyện (cry / not_cry)
- audio_processing: Tiền xử lý âm thanh (log-mel, chuẩn hóa)
"""

from .models.crynet import build_crynet_small, build_crynet_large, build_crynet
from . import audio_processing
from . import dataset

__all__ = [
    "build_crynet_small",
    "build_crynet_large",
    "build_crynet",
    "audio_processing",
    "dataset",
]
