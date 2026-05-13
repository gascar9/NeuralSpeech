#!/usr/bin/env python3
"""
Diagnostic : où se trouve le mot dans le buffer 1s pour vrai vs faux ?

Hypothèse à tester : le CNN classerait sur la POSITION temporelle du mot
plutôt que sur son contenu phonétique.

Méthode :
  1. Pour chaque MFCC (62×13), calcule l'énergie par frame.
     L'énergie = somme des |MFCC[f]| sur les 13 coefs. Une frame "pleine"
     (le mot) a une grande énergie ; une frame "silencieuse" en a peu.
  2. Trouve la frame où l'énergie est maximale (= centre approximatif du mot).
  3. Trace l'histogramme de cette "frame pic" pour vrai vs faux.

Si les deux distributions se superposent → hypothèse réfutée.
Si elles sont décalées → hypothèse confirmée → time-shift augmentation nécessaire.

Usage :
    /opt/miniconda3/bin/python3 scripts/diag_word_position.py
"""
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt


PROJECT_ROOT = Path(__file__).parent.parent
DATASET_DIR  = PROJECT_ROOT / "dataset"
FIGURES_DIR  = PROJECT_ROOT / "rapport" / "figures"
FIGURES_DIR.mkdir(parents=True, exist_ok=True)


def frame_energy(mfcc):
    """Énergie par frame = somme des |MFCC| sur les 13 coefs.

    mfcc : (62, 13) int16 Q11
    return : (62,) float32
    """
    return np.abs(mfcc.astype(np.float32)).sum(axis=1)


def main():
    peaks_vrai, peaks_faux = [], []
    energies_vrai, energies_faux = [], []

    for label, word in enumerate(["vrai", "faux"]):
        word_dir = DATASET_DIR / word
        for npy_path in sorted(word_dir.glob(f"{word}_*.mfcc.npy")):
            mfcc = np.load(npy_path)
            if mfcc.shape != (62, 13):
                continue
            e = frame_energy(mfcc)
            peak = int(np.argmax(e))
            if word == "vrai":
                peaks_vrai.append(peak)
                energies_vrai.append(e)
            else:
                peaks_faux.append(peak)
                energies_faux.append(e)

    peaks_vrai = np.array(peaks_vrai)
    peaks_faux = np.array(peaks_faux)
    energies_vrai = np.array(energies_vrai)   # (N_vrai, 62)
    energies_faux = np.array(energies_faux)   # (N_faux, 62)

    print("=" * 65)
    print("  DIAGNOSTIC — position du mot dans le buffer")
    print("=" * 65)
    print(f"\n  Vrai : {len(peaks_vrai)} samples")
    print(f"    Frame pic moyen   : {peaks_vrai.mean():5.1f} ± {peaks_vrai.std():.1f}")
    print(f"    Frame pic min/max : {peaks_vrai.min()} / {peaks_vrai.max()}")
    print(f"\n  Faux : {len(peaks_faux)} samples")
    print(f"    Frame pic moyen   : {peaks_faux.mean():5.1f} ± {peaks_faux.std():.1f}")
    print(f"    Frame pic min/max : {peaks_faux.min()} / {peaks_faux.max()}")

    # Test statistique simple
    diff = abs(peaks_vrai.mean() - peaks_faux.mean())
    print(f"\n  Différence des moyennes : {diff:.1f} frames "
          f"({diff * 0.016 * 1000:.0f} ms)")
    pooled_std = np.sqrt((peaks_vrai.std()**2 + peaks_faux.std()**2) / 2)
    print(f"  Écart-type combiné      : {pooled_std:.1f} frames")
    if pooled_std > 0:
        cohen_d = diff / pooled_std
        print(f"  Cohen's d               : {cohen_d:.2f}  "
              f"({'≥ 0.5 → effet notable' if cohen_d >= 0.5 else '< 0.5 → effet faible'})")

    # === Figure 1 : histogrammes des positions du pic ===
    fig, axes = plt.subplots(1, 2, figsize=(13, 4))

    bins = np.arange(0, 63, 2)
    axes[0].hist(peaks_vrai, bins=bins, alpha=0.6, label=f"vrai (n={len(peaks_vrai)})",
                 color="tab:blue", edgecolor="black")
    axes[0].hist(peaks_faux, bins=bins, alpha=0.6, label=f"faux (n={len(peaks_faux)})",
                 color="tab:red", edgecolor="black")
    axes[0].axvline(peaks_vrai.mean(), color="tab:blue", linestyle="--", linewidth=2,
                    label=f"moy vrai = {peaks_vrai.mean():.1f}")
    axes[0].axvline(peaks_faux.mean(), color="tab:red", linestyle="--", linewidth=2,
                    label=f"moy faux = {peaks_faux.mean():.1f}")
    axes[0].set_xlabel("Frame où l'énergie est maximale (0–61)")
    axes[0].set_ylabel("Nombre de samples")
    axes[0].set_title("Position du pic d'énergie\n(= centre du mot dans le buffer 1s)")
    axes[0].legend()
    axes[0].grid(alpha=0.3)

    # === Figure 2 : courbes d'énergie moyennes ===
    mean_e_vrai = energies_vrai.mean(axis=0)
    mean_e_faux = energies_faux.mean(axis=0)
    std_e_vrai  = energies_vrai.std(axis=0)
    std_e_faux  = energies_faux.std(axis=0)
    frames = np.arange(62)

    axes[1].plot(frames, mean_e_vrai, color="tab:blue", linewidth=2, label="vrai (moyenne)")
    axes[1].fill_between(frames, mean_e_vrai - std_e_vrai, mean_e_vrai + std_e_vrai,
                          color="tab:blue", alpha=0.2, label="vrai ±1σ")
    axes[1].plot(frames, mean_e_faux, color="tab:red", linewidth=2, label="faux (moyenne)")
    axes[1].fill_between(frames, mean_e_faux - std_e_faux, mean_e_faux + std_e_faux,
                          color="tab:red", alpha=0.2, label="faux ±1σ")
    axes[1].set_xlabel("Frame (0–61, 16 ms chacune)")
    axes[1].set_ylabel("Énergie moyenne (Σ|MFCC|)")
    axes[1].set_title("Profil d'énergie temporel — vrai vs faux")
    axes[1].legend()
    axes[1].grid(alpha=0.3)

    plt.tight_layout()
    out_path = FIGURES_DIR / "diag_word_position.png"
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"\n  ✓ Figure sauvée : {out_path}")
    plt.close()

    # === Verdict ===
    print(f"\n  ─── VERDICT ───")
    if abs(peaks_vrai.mean() - peaks_faux.mean()) > 5:
        print(f"  ⚠ Les positions moyennes diffèrent de {diff:.1f} frames.")
        print(f"    Le CNN PEUT apprendre la position plutôt que le contenu.")
        print(f"    → Time-shift augmentation NÉCESSAIRE.")
    else:
        print(f"  ✓ Positions moyennes similaires (Δ = {diff:.1f} frames).")
        print(f"    Le biais position est probablement faible.")
        print(f"    Le problème vient probablement d'ailleurs (manque de données,")
        print(f"    variabilité acoustique, etc.).")


if __name__ == "__main__":
    main()
