#!/usr/bin/env python3
"""
FP6 — Test croisé : inférence ON-CHIP vs inférence Python.

Lance fp3_recv.py pour capturer + récupérer le MFCC du firmware, ET
parse en parallèle les lignes [FP6] qui contiennent le verdict on-chip.
Compare les deux prédictions.

Si l'inférence on-chip et Python disent la même chose, on a une preuve que
le code C est correct.

Usage :
    /opt/miniconda3/bin/python3 scripts/test_cnn_onchip.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import tensorflow as tf


PROJECT_ROOT = Path(__file__).parent.parent
MODEL_PATH   = PROJECT_ROOT / "models" / "cnn_vrai_faux.keras"
NORM_PATH    = PROJECT_ROOT / "models" / "normalization_params.npz"


def parse_fp6_lines(serial_text: str):
    """Extrait le verdict et les logits des lignes [FP6] capturées par fp3_recv.py.

    Format attendu :
        [FP6] === RESULTAT INFERENCE === VRAI
        [FP6] logits Q11 : vrai=1234 faux=-567
        [FP6] timing : mfcc=17234 us, cnn=9871 us, total=27105 us
    """
    verdict = None
    logits  = None
    timing  = None
    m = re.search(r"\[FP6\] === RESULTAT INFERENCE === (VRAI|FAUX)", serial_text)
    if m:
        verdict = m.group(1)
    m = re.search(r"\[FP6\] logits Q11 : vrai=(-?\d+) faux=(-?\d+)", serial_text)
    if m:
        logits = (int(m.group(1)), int(m.group(2)))
    m = re.search(r"\[FP6\] timing : mfcc=(\d+) us, cnn=(\d+) us", serial_text)
    if m:
        timing = (int(m.group(1)), int(m.group(2)))
    return verdict, logits, timing


def main():
    print("=" * 65)
    print("  FP6 — Test croisé : inférence ON-CHIP vs Python")
    print("=" * 65)

    model = tf.keras.models.load_model(MODEL_PATH)
    norm  = np.load(NORM_PATH)
    mean  = norm["mean"]
    std   = norm["std"]

    print(f"\n  📋 Procédure : appui Entrée → D2 → dis 'vrai' ou 'faux'\n")

    n_total = 0
    n_match = 0
    n_correct_chip = 0
    n_correct_py = 0

    while True:
        try:
            cmd = input(f"  ▶ Entrée pour TESTER (q=quitter) : ").strip().lower()
        except KeyboardInterrupt:
            break
        if cmd == "q":
            break

        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            wav_path = Path(tmp.name)
        mfcc_path = wav_path.with_suffix(".mfcc.npy")

        log_path = wav_path.with_suffix(".serial.log")
        print(f"\n  ⏳ Capture (appuie D2 + dis ton mot)...\n")

        with open(log_path, "w") as lf:
            result = subprocess.run(
                ["python3", "scripts/fp3_recv.py",
                 "--out", str(wav_path), "--timeout", "120"],
                stdout=lf, stderr=subprocess.STDOUT,
                check=False,
            )

        if result.returncode != 0 or not mfcc_path.exists():
            print(f"  ❌ Échec capture\n")
            wav_path.unlink(missing_ok=True)
            log_path.unlink(missing_ok=True)
            continue

        # === Inférence Python ===
        mfcc = np.load(mfcc_path)
        mfcc_norm = ((mfcc.astype(np.float32) - mean) / std)[np.newaxis, ..., np.newaxis]
        probs = model.predict(mfcc_norm, verbose=0)[0]
        py_class = int(np.argmax(probs))
        py_verdict = "VRAI" if py_class == 0 else "FAUX"
        py_conf = float(probs[py_class])

        # === Inférence on-chip (lue depuis le log série) ===
        serial_text = log_path.read_text()
        chip_verdict, chip_logits, chip_timing = parse_fp6_lines(serial_text)

        # Nettoyage
        wav_path.unlink(missing_ok=True)
        mfcc_path.unlink(missing_ok=True)
        log_path.unlink(missing_ok=True)

        if chip_verdict is None:
            print(f"  ⚠ Pas de ligne [FP6] dans la sortie série. Le firmware tourne-t-il bien ?\n")
            continue

        match = (chip_verdict == py_verdict)
        n_total += 1
        if match: n_match += 1

        try:
            truth = input(f"  Vraie réponse (v/f/Entrée=skip) : ").strip().lower()
        except KeyboardInterrupt:
            print(); break
        truth_verdict = None
        if truth in ("v", "vrai"): truth_verdict = "VRAI"
        elif truth in ("f", "faux"): truth_verdict = "FAUX"

        if truth_verdict:
            if chip_verdict == truth_verdict: n_correct_chip += 1
            if py_verdict == truth_verdict: n_correct_py += 1

        # === Rendu console ===
        print(f"\n  ┌─────────────────────────────────────────────────┐")
        print(f"  │  ON-CHIP : {chip_verdict:<6s}  logits={chip_logits}")
        if chip_timing:
            print(f"  │  ⏱  mfcc={chip_timing[0]/1000:.1f} ms  cnn={chip_timing[1]/1000:.1f} ms")
        print(f"  │  PYTHON  : {py_verdict:<6s}  conf={py_conf*100:.1f}%")
        print(f"  │  Accord   : {'✅ MATCH' if match else '❌ MISMATCH'}")
        if truth_verdict:
            print(f"  │  Vérité   : {truth_verdict}")
        print(f"  └─────────────────────────────────────────────────┘")

        if n_total > 0:
            print(f"  📊 Cumul : on-chip↔py {n_match}/{n_total} = {n_match/n_total*100:.0f}%", end="")
            if n_correct_chip + n_correct_py > 0:
                print(f"  |  chip vs vérité {n_correct_chip}/{n_total}, "
                      f"py vs vérité {n_correct_py}/{n_total}")
            else:
                print()

    if n_total > 0:
        print(f"\n  Bilan final : on-chip ↔ Python = {n_match}/{n_total} "
              f"({n_match/n_total*100:.0f}%)")
        if n_correct_chip > 0 or n_correct_py > 0:
            print(f"               on-chip vs vérité  = {n_correct_chip}/{n_total}")
            print(f"               Python  vs vérité  = {n_correct_py}/{n_total}")


if __name__ == "__main__":
    main()
