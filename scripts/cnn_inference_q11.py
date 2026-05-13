#!/usr/bin/env python3
"""
FP6 — Référence Python de l'inférence Q11 (= miroir du futur code C).

Charge models/cnn_vrai_faux.keras + normalization_params.npz, quantifie tout en
Q11 (comme export_weights.py), puis effectue l'inférence en arithmétique
*strictement entière* : int16 pour les valeurs, int32 pour les accumulateurs,
décalages binaires uniquement.

Le test compare la prédiction Q11 à la prédiction Keras (float) sur tout le
dataset. Si ça matche bien, c'est la garantie que la même logique transcrite
en C produira les bonnes prédictions sur l'Arduino.

Usage :
    /opt/miniconda3/bin/python3 scripts/cnn_inference_q11.py
"""
from pathlib import Path
import numpy as np
import tensorflow as tf


Q_BITS = 11
Q_SCALE = 1 << Q_BITS         # 2048
INV_STD_Q_BITS = 24
INT16_MIN, INT16_MAX = -32768, 32767


PROJECT_ROOT = Path(__file__).parent.parent
MODEL_PATH   = PROJECT_ROOT / "models" / "cnn_vrai_faux.keras"
NORM_PATH    = PROJECT_ROOT / "models" / "normalization_params.npz"
DATASET_DIR  = PROJECT_ROOT / "dataset"


# ---------------------------------------------------------------------------
# Quantification (identique à export_weights.py)
# ---------------------------------------------------------------------------
def to_q11(x):
    q = np.round(np.asarray(x, dtype=np.float64) * Q_SCALE)
    return np.clip(q, INT16_MIN, INT16_MAX).astype(np.int16)


# ---------------------------------------------------------------------------
# Couches d'inférence Q11 (chaque opération en entier strict)
# ---------------------------------------------------------------------------
def normalize_q11(mfcc_raw_i16, mean_i16, inv_std_q24):
    """MFCC brut int16 → MFCC normalisé Q11.

    Formule embarquée :
        x_norm = saturate(((raw - mean) * inv_std_q24) >> 13)

    Le >> 13 amène de Q24 à Q11 (24-11=13).
    """
    out = np.zeros_like(mfcc_raw_i16, dtype=np.int16)
    F, C = mfcc_raw_i16.shape
    for f in range(F):
        for c in range(C):
            diff = int(mfcc_raw_i16[f, c]) - int(mean_i16[c])
            acc = diff * int(inv_std_q24[c])
            acc >>= 13
            if acc > INT16_MAX: acc = INT16_MAX
            elif acc < INT16_MIN: acc = INT16_MIN
            out[f, c] = acc
    return out


def conv2d_relu_q11(x, w, b):
    """Convolution 2D Q11 (kernel 3×3, padding=valid) + ReLU + saturation int16.

    x : (H, W, C_in)   int16 Q11
    w : (C_out, 3, 3, C_in) int16 Q11
    b : (C_out,) int16 Q11
    """
    H, W, C_in = x.shape
    C_out, KH, KW, _ = w.shape
    out_H, out_W = H - KH + 1, W - KW + 1
    y = np.zeros((out_H, out_W, C_out), dtype=np.int16)
    for h in range(out_H):
        for ww in range(out_W):
            for co in range(C_out):
                # bias en Q11 → décaler à Q22 pour matcher acc
                acc = int(b[co]) << Q_BITS
                for kh in range(KH):
                    for kw in range(KW):
                        for ci in range(C_in):
                            acc += int(w[co, kh, kw, ci]) * int(x[h + kh, ww + kw, ci])
                # back to Q11
                acc >>= Q_BITS
                if acc < 0:
                    acc = 0     # ReLU
                if acc > INT16_MAX: acc = INT16_MAX
                y[h, ww, co] = acc
    return y


def maxpool_2x2_q11(x):
    """MaxPool 2×2, stride 2.

    x : (H, W, C)   int16
    return (H//2, W//2, C)
    """
    H, W, C = x.shape
    out_H, out_W = H // 2, W // 2
    y = np.zeros((out_H, out_W, C), dtype=np.int16)
    for h in range(out_H):
        for ww in range(out_W):
            for c in range(C):
                a = int(x[2*h,     2*ww,     c])
                b = int(x[2*h + 1, 2*ww,     c])
                c2 = int(x[2*h,     2*ww + 1, c])
                d = int(x[2*h + 1, 2*ww + 1, c])
                y[h, ww, c] = max(a, b, c2, d)
    return y


def dense_q11(x, w, b, with_relu):
    """Couche dense Q11 + ReLU optionnel + saturation int16.

    x : (D_in,)        int16 Q11
    w : (D_in, D_out)  int16 Q11
    b : (D_out,)       int16 Q11
    """
    D_in, D_out = w.shape
    y = np.zeros(D_out, dtype=np.int16)
    for o in range(D_out):
        acc = int(b[o]) << Q_BITS
        for i in range(D_in):
            acc += int(w[i, o]) * int(x[i])
        acc >>= Q_BITS
        if with_relu and acc < 0:
            acc = 0
        if acc > INT16_MAX: acc = INT16_MAX
        elif acc < INT16_MIN: acc = INT16_MIN
        y[o] = acc
    return y


# ---------------------------------------------------------------------------
# Pipeline complet
# ---------------------------------------------------------------------------
class CNN_Q11:
    """Inférence CNN strictement en arithmétique entière Q11."""

    def __init__(self, model, mean, std):
        # Quantifier tous les poids une seule fois
        self.mean_i16  = np.clip(np.round(mean), INT16_MIN, INT16_MAX).astype(np.int16)
        inv_std = 1.0 / std
        self.inv_std_q24 = np.round(inv_std * (1 << INV_STD_Q_BITS)).astype(np.int32)

        c1w, c1b = model.get_layer("conv1").get_weights()
        c2w, c2b = model.get_layer("conv2").get_weights()
        d1w, d1b = model.get_layer("dense1").get_weights()
        d2w, d2b = model.get_layer("output").get_weights()

        self.c1_w = to_q11(np.transpose(c1w, (3, 0, 1, 2)))  # (8,3,3,1)
        self.c1_b = to_q11(c1b)
        self.c2_w = to_q11(np.transpose(c2w, (3, 0, 1, 2)))  # (16,3,3,8)
        self.c2_b = to_q11(c2b)
        self.d1_w = to_q11(d1w)
        self.d1_b = to_q11(d1b)
        self.d2_w = to_q11(d2w)
        self.d2_b = to_q11(d2b)

    def predict(self, mfcc_raw_i16):
        """Reçoit un MFCC int16 (62,13), renvoie (class_id, logits)."""
        x = normalize_q11(mfcc_raw_i16, self.mean_i16, self.inv_std_q24)  # (62,13)
        x = x.reshape(62, 13, 1)
        x = conv2d_relu_q11(x, self.c1_w, self.c1_b)   # (60,11,8)
        x = maxpool_2x2_q11(x)                          # (30,5,8)
        x = conv2d_relu_q11(x, self.c2_w, self.c2_b)   # (28,3,16)
        x = maxpool_2x2_q11(x)                          # (14,1,16)
        x = x.flatten()                                 # (224,)
        x = dense_q11(x, self.d1_w, self.d1_b, with_relu=True)   # (32,)
        logits = dense_q11(x, self.d2_w, self.d2_b, with_relu=False)  # (2,)
        return int(np.argmax(logits)), logits


# ---------------------------------------------------------------------------
# Comparaison Q11 vs Keras float
# ---------------------------------------------------------------------------
def main():
    print("=" * 70)
    print("  FP6 — Validation Q11 contre Keras float")
    print("=" * 70)

    model = tf.keras.models.load_model(MODEL_PATH)
    norm  = np.load(NORM_PATH)
    cnn_q11 = CNN_Q11(model, norm["mean"], norm["std"])

    # Charge tout le dataset
    X, y_true, names = [], [], []
    for label, word in enumerate(["vrai", "faux"]):
        for npy in sorted((DATASET_DIR / word).glob(f"{word}_*.mfcc.npy")):
            mfcc = np.load(npy)
            if mfcc.shape == (62, 13):
                X.append(mfcc.astype(np.int16))
                y_true.append(label)
                names.append(npy.name)
    X = np.array(X, dtype=np.int16)
    y_true = np.array(y_true)
    print(f"\n  Dataset chargé : {len(X)} samples ({(y_true==0).sum()} vrai, "
          f"{(y_true==1).sum()} faux)")

    # Préd Keras (référence float)
    # Recharge mean/std float pour normaliser comme à l'entraînement
    mean_f = norm["mean"].astype(np.float32)
    std_f  = norm["std"].astype(np.float32)
    X_norm_f = ((X.astype(np.float32) - mean_f) / std_f)[..., np.newaxis]
    y_keras = np.argmax(model.predict(X_norm_f, verbose=0), axis=1)
    keras_acc = float((y_keras == y_true).mean())

    # Préd Q11 (notre inférence entière)
    print(f"\n  Inférence Q11 sur {len(X)} samples...", end=" ", flush=True)
    import time
    t0 = time.time()
    y_q11 = np.zeros(len(X), dtype=int)
    for i, mfcc in enumerate(X):
        y_q11[i], _ = cnn_q11.predict(mfcc)
    duration = time.time() - t0
    q11_acc = float((y_q11 == y_true).mean())
    print(f"({duration:.1f} s, {duration/len(X)*1000:.1f} ms/sample)")

    # Compare les deux
    agreement = float((y_keras == y_q11).mean())

    print(f"\n  Keras float : {keras_acc*100:.1f}% accuracy")
    print(f"  Q11 int     : {q11_acc*100:.1f}% accuracy")
    print(f"  Accord K↔Q : {agreement*100:.1f}% ({(y_keras==y_q11).sum()}/{len(X)})")

    if agreement < 0.95:
        print(f"\n  ⚠ Accord < 95% : la quantification dégrade significativement")
        print(f"  Différences :")
        diffs = np.where(y_keras != y_q11)[0]
        for i in diffs[:5]:
            print(f"    {names[i]:>35s}  keras={y_keras[i]}  q11={y_q11[i]}")
    else:
        print(f"\n  ✓ Quantification Q11 conforme : la math C produira les mêmes "
              f"prédictions que Keras.")

    print("\n" + "=" * 70)


if __name__ == "__main__":
    main()
