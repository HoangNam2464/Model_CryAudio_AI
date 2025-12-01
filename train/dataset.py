from pathlib import Path
import numpy as np
import tensorflow as tf
import librosa
import random

from config import (
    SR,
    VAL_SPLIT,
    TEST_SPLIT,
)
from preprocess import normalize_gain, pad_or_trim, extract_mfcc
from augment import augment_wave


def list_files(root: Path):
    return [p for p in root.glob("*.wav")]


def load_noise_pool(noise_dir: Path):
    pool = []
    if noise_dir and noise_dir.exists():
        for p in noise_dir.glob("*.wav"):
            n, _ = librosa.load(p, sr=SR, mono=True)
            pool.append(n)
    return pool


def split_dataset(files, val_ratio=VAL_SPLIT, test_ratio=TEST_SPLIT, seed=42):
    rng = random.Random(seed)
    rng.shuffle(files)
    n = len(files)
    n_test = int(n * test_ratio)
    n_val = int(n * val_ratio)
    test = files[:n_test]
    val = files[n_test : n_test + n_val]
    train = files[n_test + n_val :]
    return train, val, test


def make_ds(cry_files, not_cry_files, noise_pool, batch_size=64, augment=False):
    files = cry_files + not_cry_files
    labels = [1] * len(cry_files) + [0] * len(not_cry_files)

    def gen():
        for path, lab in zip(files, labels):
            y, _ = librosa.load(path, sr=SR, mono=True)
            if augment:
                y = augment_wave(y, noise_pool)
            y = normalize_gain(y)
            y = pad_or_trim(y)
            mfcc = extract_mfcc(y)  # (20,25)
            yield mfcc[..., None], np.int64(lab)

    ds = tf.data.Dataset.from_generator(
        gen,
        output_signature=(
            tf.TensorSpec(shape=(20, 25, 1), dtype=tf.float32),
            tf.TensorSpec(shape=(), dtype=tf.int64),
        ),
    )
    ds = ds.shuffle(2048).batch(batch_size).prefetch(tf.data.AUTOTUNE)
    return ds
