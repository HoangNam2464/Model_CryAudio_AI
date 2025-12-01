from pathlib import Path

# Paths (update if your data is elsewhere)
DATA_ROOT = Path("data")
CRY_DIR = DATA_ROOT / "cry"
NOT_CRY_DIR = DATA_ROOT / "not_cry"
NOISE_DIR = DATA_ROOT / "noise"  # optional, used for augmentation

# Audio / feature params
SR = 16000
FRAME_LEN = 512
HOP = 1280           # ~80 ms
N_MFCC = 20
N_FRAMES = 25
SEGMENT_SEC = 2.0
TARGET_SAMPLES = int(SR * SEGMENT_SEC)

# Training params
VAL_SPLIT = 0.15
TEST_SPLIT = 0.1
EPOCHS = 40
EARLY_STOP_PATIENCE = 5

# Grid search space
LR_LIST = [1e-4, 3e-4, 1e-3]
BS_LIST = [16, 32, 64]
F0_LIST = [12, 16, 20]

# Threshold selection
RECALL_MIN = 0.95
PREC_MIN = 0.70

# Calibration
CALIB_FILES_MAX = 200
