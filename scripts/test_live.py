#!/usr/bin/env python3
"""
Test live du CNN entraîné : tu dis un mot dans le micro, Python te dit
si c'est "vrai" ou "faux" + sa confiance.

Usage :
    /opt/miniconda3/bin/python3 scripts/test_live.py

Le script lance fp3_recv.py pour capturer un nouveau enregistrement,
puis applique le modèle CNN entraîné par train_cnn.py.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import tensorflow as tf


PROJECT_ROOT = Path(__file__).parent.parent
MODEL_PATH   = PROJECT_ROOT / "models" / "cnn_vrai_faux.keras"
NORM_PATH    = PROJECT_ROOT / "models" / "normalization_params.npz"


def main():
    # --- Vérifie que le modèle existe
    if not MODEL_PATH.exists() or not NORM_PATH.exists():
        print(f"❌ Modèle non trouvé. Lance d'abord :")
        print(f"   python3 scripts/train_cnn.py")
        sys.exit(1)

    print("=" * 65)
    print("  TEST LIVE du CNN vrai/faux")
    print("=" * 65)
    print(f"\n  Chargement modèle : {MODEL_PATH.name}")
    model = tf.keras.models.load_model(MODEL_PATH)

    norm = np.load(NORM_PATH)
    mean = norm["mean"]
    std  = norm["std"]
    print(f"  Paramètres normalisation : mean={mean.shape}, std={std.shape}")

    print(f"\n  📋 Procédure :")
    print(f"    1. Tu appuies sur Entrée (clavier)")
    print(f"    2. Le script attend ~1.5 s puis affiche 'En attente du header magic'")
    print(f"    3. À ce moment, appuie sur D2 + dis 'vrai' ou 'faux'")
    print(f"    4. Python te dit ce qu'il a entendu\n")

    n_correct = 0
    n_total   = 0

    while True:
        try:
            cmd = input(f"  ▶ Appuie sur Entrée pour TESTER (ou 'q' pour quitter) : ").strip().lower()
        except KeyboardInterrupt:
            print("\n  Bye.")
            break

        if cmd == "q":
            break

        # --- Capture
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            wav_path = Path(tmp.name)
        mfcc_path = wav_path.with_suffix(".mfcc.npy")

        print(f"\n  ⏳ Capture en cours... appuie sur D2 + dis ton mot\n")
        result = subprocess.run(
            ["python3", "scripts/fp3_recv.py",
             "--out", str(wav_path),
             "--timeout", "120"],
            check=False,
        )

        if result.returncode != 0 or not mfcc_path.exists():
            print(f"\n  ❌ Échec capture\n")
            wav_path.unlink(missing_ok=True)
            continue

        # --- Charge le MFCC capturé
        mfcc = np.load(mfcc_path)
        wav_path.unlink(missing_ok=True)
        mfcc_path.unlink(missing_ok=True)

        # --- Normalise (mêmes mean/std que pendant le training)
        mfcc_norm = (mfcc.astype(np.float32) - mean) / std
        mfcc_norm = mfcc_norm[np.newaxis, ..., np.newaxis]   # (1, 62, 13, 1)

        # --- Prédit
        probs = model.predict(mfcc_norm, verbose=0)[0]   # (2,) → [P_vrai, P_faux]
        pred = int(np.argmax(probs))
        confidence = float(probs[pred])
        word = "vrai" if pred == 0 else "faux"

        # --- Affichage
        print(f"\n  ┌─────────────────────────────────────────────┐")
        if confidence > 0.9:
            indicator = "🟢"
        elif confidence > 0.7:
            indicator = "🟡"
        else:
            indicator = "🟠"
        print(f"  │ {indicator} Prédiction : « {word.upper()} »  ({confidence*100:.1f}%)")
        print(f"  │   P(vrai) = {probs[0]:.3f}")
        print(f"  │   P(faux) = {probs[1]:.3f}")
        print(f"  └─────────────────────────────────────────────┘")

        # --- Optionnel : feedback utilisateur pour stats
        try:
            expected = input(f"  Vraie réponse (v=vrai, f=faux, Entrée=skip) : ").strip().lower()
            if expected in ("v", "vrai"):
                n_total += 1
                if pred == 0: n_correct += 1
            elif expected in ("f", "faux"):
                n_total += 1
                if pred == 1: n_correct += 1
        except KeyboardInterrupt:
            print()
            break

        if n_total > 0:
            print(f"  📊 Score depuis le début : {n_correct}/{n_total} = {n_correct/n_total*100:.0f}%\n")

    if n_total > 0:
        print(f"\n  Score final : {n_correct}/{n_total} = {n_correct/n_total*100:.0f}%")


if __name__ == "__main__":
    main()
