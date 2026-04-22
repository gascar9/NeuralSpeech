"""
NeuralSpeech — FP2 : Tracer la réponse en fréquence du filtre installé
======================================================================

Lit les coefficients Q15 depuis `include/filter_coefs.h`, les convertit en
float, et trace la réponse en fréquence avec annotations du gabarit ET2.

Sortie :
  rapport/figures/FP2/filter_response.png

Usage :
  python3 scripts/plot_filter_response.py

Ce script est AGNOSTIQUE de l'outil qui a généré le filtre (T-Filter,
scipy.signal.firwin, etc.) : il se contente de parser le tableau Q15
présent dans le header C et de le plotter.
"""

import os
import re
import sys
import numpy as np
import scipy.signal as sig
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---------------------------------------------------------------------------
# Paramètres du gabarit (doivent rester en phase avec filter_coefs.h)
# ---------------------------------------------------------------------------
FE_HZ          = 32000
F_PASS_HZ      = 2800          # fin bande passante (T-Filter actuel)
F_STOP_HZ      = 4000          # début bande coupée (ET2)
ATTENUATION_DB = 30.0          # seuil ET2

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
HEADER_PATH  = os.path.join(PROJECT_ROOT, "include", "filter_coefs.h")
FIGURE_DIR   = os.path.join(PROJECT_ROOT, "rapport", "figures", "FP2")
FIGURE_PATH  = os.path.join(FIGURE_DIR, "filter_response.png")


def parse_q15_coefs(header_path: str) -> np.ndarray:
    """Extrait FILTER_COEFS_Q15 du header C et retourne un array int16."""
    with open(header_path, "r") as f:
        text = f.read()

    m = re.search(
        r"FILTER_COEFS_Q15\s*\[\s*FILTER_TAPS\s*\]\s*=\s*\{([^}]*)\}",
        text,
        re.DOTALL,
    )
    if not m:
        print("ERREUR : FILTER_COEFS_Q15[] introuvable dans", header_path)
        sys.exit(1)

    nums = re.findall(r"-?\d+", m.group(1))
    return np.array([int(n) for n in nums], dtype=np.int16)


def main() -> None:
    coefs_q15 = parse_q15_coefs(HEADER_PATH)
    coefs_f   = coefs_q15.astype(np.float64) / 32768.0
    n_taps    = len(coefs_q15)

    # Réponse en fréquence sur 32768 points
    w, h = sig.freqz(coefs_f, worN=32768, fs=FE_HZ)
    magnitude_db = 20.0 * np.log10(np.abs(h) + 1e-12)
    dc_gain_db   = magnitude_db[0]
    magnitude_db = magnitude_db - dc_gain_db   # normalisation gain DC = 0 dB

    # Recherche de l'atténuation à 4 kHz
    idx_4k = int(np.argmin(np.abs(w - F_STOP_HZ)))
    att_4k = -magnitude_db[idx_4k]
    f_6db  = w[int(np.argmin(np.abs(magnitude_db + 6.0)))]

    # ---- Plot ----
    os.makedirs(FIGURE_DIR, exist_ok=True)
    fig, ax = plt.subplots(figsize=(10, 4.2))

    ax.plot(w, magnitude_db,
            color="#d62728", linewidth=1.8,
            label=f"RIF Parks-McClellan — {n_taps} taps")

    # Zone bande passante
    ax.axvspan(0, F_PASS_HZ, color="#1f77b4", alpha=0.08)
    ax.text(F_PASS_HZ / 2, -4, "Bande\npassante",
            ha="center", va="top", fontsize=9, color="#1f77b4")

    # Seuil ET2
    ax.axhline(-ATTENUATION_DB, color="red",  linestyle="--", linewidth=1.2,
               label=f"Contrainte ET2 : -{int(ATTENUATION_DB)} dB")
    ax.axvline(F_STOP_HZ,      color="orange",linestyle=":",  linewidth=1.2,
               label=f"f_stop = {F_STOP_HZ/1000:.0f} kHz (ET2)")
    ax.axvline(F_PASS_HZ,      color="black", linestyle=":",  linewidth=0.8, alpha=0.6,
               label=f"f_pass = {F_PASS_HZ/1000:.1f} kHz")
    ax.axvline(f_6db,          color="purple",linestyle=":",  linewidth=0.8, alpha=0.6,
               label=f"fc (-6 dB) = {f_6db/1000:.2f} kHz")
    ax.axvline(FE_HZ / 2,      color="gray",  linestyle=":",  linewidth=0.6, alpha=0.4,
               label=f"Nyquist ({FE_HZ/2000:.0f} kHz)")

    # Point -X dB @ 4 kHz
    ax.plot(F_STOP_HZ, -att_4k, "o", color="red", markersize=8)
    ax.annotate(f"-{att_4k:.1f} dB\n@ {F_STOP_HZ/1000:.0f} kHz",
                xy=(F_STOP_HZ, -att_4k),
                xytext=(F_STOP_HZ + 1500, -att_4k + 6),
                fontsize=10, color="red",
                arrowprops=dict(arrowstyle="->", color="red", lw=1))

    ax.set_xlim(0, FE_HZ / 2)
    ax.set_ylim(-90, 5)
    ax.set_xlabel("Fréquence (Hz)")
    ax.set_ylabel("Amplitude (dB)")
    ax.set_title(
        f"FP2 — Réponse en fréquence : RIF Parks-McClellan\n"
        f"{n_taps} taps  |  fc (-6 dB) = {f_6db/1000:.2f} kHz  |  "
        f"Att @ {F_STOP_HZ/1000:.0f} kHz = {att_4k:.1f} dB  |  Fe = {FE_HZ/1000:.0f} kHz"
    )
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper right", fontsize=8, framealpha=0.9)

    plt.tight_layout()
    plt.savefig(FIGURE_PATH, dpi=150)
    plt.close()

    print("Plot enregistré :", FIGURE_PATH)
    print(f"  Filtre : {n_taps} taps")
    print(f"  fc (-6 dB) : {f_6db:.0f} Hz")
    print(f"  Atténuation @ {F_STOP_HZ} Hz : {att_4k:.2f} dB")
    print(f"  ET2 ({ATTENUATION_DB} dB) : {'OK' if att_4k >= ATTENUATION_DB else 'KO'}")


if __name__ == "__main__":
    main()
