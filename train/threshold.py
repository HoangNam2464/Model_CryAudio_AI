import numpy as np
from sklearn.metrics import precision_recall_curve


def select_threshold(probs, y_true, recall_min=0.95, prec_min=0.7):
    prec, rec, thr = precision_recall_curve(y_true, probs)
    mask = rec >= recall_min
    if np.any(mask):
        idx = np.argmax(prec[mask])
        tau = thr[mask][idx]
        if prec[mask][idx] < prec_min:
            tau = thr[np.argmax(rec)]
    else:
        tau = 0.5
    return float(tau)
