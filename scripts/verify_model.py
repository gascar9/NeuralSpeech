#!/usr/bin/env python3
"""
Vérifie que le modèle entraîné classe correctement TOUT le dataset.

Si on a 90+ % d'accuracy ici → le modèle est sain.
Si on est en-dessous → le modèle est mal entraîné, à retrainer.
"""
from pathlib import Path
import numpy as np
import tensorflow as tf

PROJECT_ROOT = Path(__file__).parent.parent
MODEL_PATH   = PROJECT_ROOT / "models" / "cnn_vrai_faux.keras"
NORM_PATH    = PROJECT_ROOT / "models" / "normalization_params.npz"
DATASET_DIR  = PROJECT_ROOT / "dataset"


def main():
    print("=" * 65)
    print("  Vérification : modèle vs dataset complet")
    print("=" * 65)

    model = tf.keras.models.load_model(MODEL_PATH)
    norm = np.load(NORM_PATH)
    mean, std = norm["mean"], norm["std"]

    X, y_true, names = [], [], []
    for label, word in enumerate(["vrai", "faux"]):
        for npy in sorted((DATASET_DIR / word).glob(f"{word}_*.mfcc.npy")):
            mfcc = np.load(npy)
            if mfcc.shape == (62, 13):
                X.append(mfcc); y_true.append(label); names.append(npy.name)
    X = np.array(X, dtype=np.int16)
    y_true = np.array(y_true)

    X_norm = ((X.astype(np.float32) - mean) / std)[..., np.newaxis]
    probs = model.predict(X_norm, verbose=0)
    y_pred = np.argmax(probs, axis=1)
    confs = np.max(probs, axis=1)

    acc = float((y_pred == y_true).mean())
    n_vrai_ok = int(((y_pred == 0) & (y_true == 0)).sum())
    n_faux_ok = int(((y_pred == 1) & (y_true == 1)).sum())
    n_vrai_tot = int((y_true == 0).sum())
    n_faux_tot = int((y_true == 1).sum())

    print(f"\n  Total                    : {len(X)} samples")
    print(f"  Accuracy                 : {acc*100:.1f}%  ({(y_pred==y_true).sum()}/{len(X)})")
    print(f"  Vrai correctement classé : {n_vrai_ok}/{n_vrai_tot}")
    print(f"  Faux correctement classé : {n_faux_ok}/{n_faux_tot}")
    print(f"  Confiance moyenne        : {confs.mean()*100:.1f}%")

    # Confiances par classe
    confs_vrai = confs[y_true == 0]
    confs_faux = confs[y_true == 1]
    print(f"  Confiance moy. sur vrai  : {confs_vrai.mean()*100:.1f}%")
    print(f"  Confiance moy. sur faux  : {confs_faux.mean()*100:.1f}%")

    # 5 plus gros échecs
    wrong = np.where(y_pred != y_true)[0]
    if len(wrong) > 0:
        print(f"\n  Échecs ({len(wrong)} samples) :")
        for i in wrong[:10]:
            tr = "vrai" if y_true[i] == 0 else "faux"
            pr = "vrai" if y_pred[i] == 0 else "faux"
            print(f"    {names[i]:>30s}  truth={tr}  pred={pr}  conf={confs[i]*100:.0f}%")


if __name__ == "__main__":
    main()
