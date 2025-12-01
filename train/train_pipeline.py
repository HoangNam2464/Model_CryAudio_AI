import itertools
from pathlib import Path
import numpy as np
import tensorflow as tf
from sklearn.metrics import precision_recall_fscore_support

from config import (
    CRY_DIR,
    NOT_CRY_DIR,
    NOISE_DIR,
    LR_LIST,
    BS_LIST,
    F0_LIST,
    EPOCHS,
    EARLY_STOP_PATIENCE,
    RECALL_MIN,
    PREC_MIN,
)
from dataset import list_files, split_dataset, make_ds, load_noise_pool
from model import build_model
from threshold import select_threshold
from export import convert_int8, tflite_to_cc


def eval_metrics(model, ds):
    y_true = []
    y_prob = []
    for x, y in ds:
        p = model.predict(x, verbose=0)[:, 1]
        y_true.append(y.numpy())
        y_prob.append(p)
    y_true = np.concatenate(y_true)
    y_prob = np.concatenate(y_prob)
    pred = (y_prob >= 0.5).astype(int)
    p, r, f1, _ = precision_recall_fscore_support(y_true, pred, average=None, labels=[0, 1])
    return float(p[1]), float(r[1]), y_true, y_prob


def main():
    cry_files = list_files(CRY_DIR)
    not_files = list_files(NOT_CRY_DIR)
    noise_pool = load_noise_pool(NOISE_DIR)
    print(f"Found cry={len(cry_files)} not_cry={len(not_files)} noise_pool={len(noise_pool)}")
    train_cry, val_cry, test_cry = split_dataset(cry_files)
    train_not, val_not, test_not = split_dataset(not_files)

    best = None
    for lr, bs, f0 in itertools.product(LR_LIST, BS_LIST, F0_LIST):
        train_ds = make_ds(train_cry, train_not, noise_pool, batch_size=bs, augment=True)
        val_ds = make_ds(val_cry, val_not, noise_pool, batch_size=bs, augment=False)
        model = build_model(base_filters=f0)
        model.compile(
            optimizer=tf.keras.optimizers.Adam(lr),
            loss="sparse_categorical_crossentropy",
            metrics=["accuracy"],
        )
        cb = [
            tf.keras.callbacks.EarlyStopping(
                monitor="val_accuracy", patience=EARLY_STOP_PATIENCE, restore_best_weights=True
            )
        ]
        model.fit(train_ds, validation_data=val_ds, epochs=EPOCHS, callbacks=cb, verbose=2)
        p1, r1, y_true, y_prob = eval_metrics(model, val_ds)
        print(f"[cfg lr={lr} bs={bs} f0={f0}] precision_CRY={p1:.3f} recall_CRY={r1:.3f}")
        if best is None or r1 > best["recall"]:
            best = {
                "model": model,
                "precision": p1,
                "recall": r1,
                "cfg": (lr, bs, f0),
                "y_true": y_true,
                "y_prob": y_prob,
                "val_ds": val_ds,
            }

    # Threshold selection
    tau = select_threshold(best["y_prob"], best["y_true"], recall_min=RECALL_MIN, prec_min=PREC_MIN)
    print(f"Selected threshold tau={tau:.3f}")

    # Save model
    out_dir = Path("artifacts_ds_tiny")
    out_dir.mkdir(exist_ok=True)
    saved_model_dir = out_dir / "saved_model"
    best["model"].save(saved_model_dir, include_optimizer=False)

    # Quantize
    calib_files = train_cry + train_not
    convert_int8(saved_model_dir, calib_files, out_path=out_dir / "cry_tiny_int8.tflite")
    tflite_to_cc(out_dir / "cry_tiny_int8.tflite", out_dir / "cry_tiny_int8_model.cc")

    # Report
    print("Best config:", best["cfg"])
    print(f"Precision_CRY={best['precision']:.3f}, Recall_CRY={best['recall']:.3f}")
    print(f"Use threshold_on={tau:.3f}, threshold_off={tau*0.6:.3f}")


if __name__ == "__main__":
    main()
