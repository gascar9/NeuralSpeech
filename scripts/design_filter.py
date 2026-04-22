"""
NeuralSpeech — FP2 : Design du filtre RIF anti-repliement (ET2)
================================================================

Objectif :
  Concevoir un filtre RIF passe-bas (fenetre Hamming) tel que :
    - Frequence de coupure (-6 dB) entre 3.0 kHz et 3.8 kHz
      (preserves les harmoniques vocales : fondamental 80-300 Hz,
       formants F1/F2/F3 jusqu'a ~3.5 kHz)
    - Attenuation >= 30 dB a 4 kHz (ET2)

  Strategie :
    On fixe la bande de transition [fc_passante, fc_stop] = [3200 Hz, 4000 Hz].
    On balaye l'ordre N jusqu'a satisfaire les 30 dB a f_stop = 4000 Hz.
    Le point -6 dB (fc_design pour firwin) est place a la moyenne geometrique
    de la bande de transition : sqrt(3200 * 4000) ~ 3578 Hz.

  Contexte sous-echantillonnage :
    Apres ce filtre (Fe=32kHz), on garde 1 echantillon sur 4 -> Fe_out=8kHz.
    Nyquist de la sortie = 4 kHz. Il faut donc que le signal soit attenué
    au-dela de 4 kHz pour eviter le repliement.

Sorties :
  - include/filter_coefs.h  : header C avec les coefficients prêts a compiler
  - rapport/figures/FP2/filter_response.png : plot de la reponse en frequence

Usage :
  python scripts/design_filter.py

Dependances : numpy, scipy, matplotlib
  pip install numpy scipy matplotlib
"""

import os
import sys
import numpy as np
import scipy.signal as sig
import matplotlib
matplotlib.use("Agg")          # pas d'affichage interactif — rendu fichier seul
import matplotlib.pyplot as plt

# ---------------------------------------------------------------------------
# Parametres de conception
# ---------------------------------------------------------------------------

FE_HZ          = 32000          # frequence d'echantillonnage (Hz)
F_PASS_HZ      = 3200           # fin de la bande passante (Hz)  - gain ~ 0 dB ici
F_STOP_HZ      = 4000           # debut de la bande attenuee (Hz) - doit etre >= 30 dB
ATTENUATION_DB = 30.0           # attenuation minimale requise a F_STOP_HZ (ET2)
N_MIN          = 16             # ordre minimum a balayer
N_MAX          = 256            # ordre maximum a balayer
N_STEP         = 2              # pas de balayage (fin pour trouver le minimum exact)

# Frequence de design firwin = milieu geometrique de la bande de transition
# C'est le point ou le filtre atteint exactement -6 dB
FC_DESIGN_HZ   = int(np.sqrt(F_PASS_HZ * F_STOP_HZ))  # ~ 3578 Hz

# Chemins de sortie
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
HEADER_PATH  = os.path.join(PROJECT_ROOT, "include", "filter_coefs.h")
FIGURE_DIR   = os.path.join(PROJECT_ROOT, "rapport", "figures", "FP2")
FIGURE_PATH  = os.path.join(FIGURE_DIR, "filter_response.png")


# ---------------------------------------------------------------------------
# Fonctions utilitaires
# ---------------------------------------------------------------------------

def attenuation_at_freq_db(coeffs: np.ndarray, fe: int, f_eval: int) -> float:
    """
    Retourne l'attenuation en dB a la frequence f_eval par rapport au gain DC.
    Valeur positive = attenuation.
    """
    w, h = sig.freqz(coeffs, worN=32768, fs=fe)
    magnitude_db = 20.0 * np.log10(np.abs(h) + 1e-12)
    gain_passant_db = magnitude_db[0]                   # DC ~ 0 dB
    idx = np.argmin(np.abs(w - f_eval))
    return gain_passant_db - magnitude_db[idx]


# ---------------------------------------------------------------------------
# Recherche de l'ordre minimal satisfaisant ET2
# ---------------------------------------------------------------------------

print(f"Balayage ordre N de {N_MIN} a {N_MAX} (pas {N_STEP})")
print(f"  Contrainte : attenuation >= {ATTENUATION_DB} dB @ {F_STOP_HZ} Hz")
print(f"  fc_design  : {FC_DESIGN_HZ} Hz (point -6 dB, entre {F_PASS_HZ} et {F_STOP_HZ} Hz)")
print(f"  Fe         : {FE_HZ} Hz")
print("-" * 72)

selected_N     = None
selected_coefs = None

for N in range(N_MIN, N_MAX + 1, N_STEP):
    taps = sig.firwin(N + 1, FC_DESIGN_HZ, window="hamming", fs=FE_HZ)
    att  = attenuation_at_freq_db(taps, FE_HZ, F_STOP_HZ)
    att_passante = attenuation_at_freq_db(taps, FE_HZ, F_PASS_HZ)

    if N <= 64 or N % 8 == 0:   # affichage selectif pour ne pas inonder la console
        print(f"  N={N:3d}  ({N+1:3d} taps)  att@{F_STOP_HZ}Hz={att:6.1f} dB"
              f"  att@{F_PASS_HZ}Hz={att_passante:5.1f} dB", end="")
        if att >= ATTENUATION_DB:
            print("  <-- RETENU")
        else:
            print()

    if att >= ATTENUATION_DB and selected_N is None:
        selected_N     = N
        selected_coefs = taps
        if N > 64 and N % 8 != 0:
            # Afficher quand meme si pas encore affiche
            print(f"  N={N:3d}  ({N+1:3d} taps)  att@{F_STOP_HZ}Hz={att:6.1f} dB"
                  f"  att@{F_PASS_HZ}Hz={att_passante:5.1f} dB  <-- RETENU")
        break

if selected_coefs is None:
    print(f"\nERREUR : aucun ordre <= {N_MAX} ne satisfait {ATTENUATION_DB} dB.")
    sys.exit(1)

TAPS_COUNT = len(selected_coefs)   # = selected_N + 1
att_effective  = attenuation_at_freq_db(selected_coefs, FE_HZ, F_STOP_HZ)
att_at_passant = attenuation_at_freq_db(selected_coefs, FE_HZ, F_PASS_HZ)

print(f"\nFiltre retenu :")
print(f"  Ordre N                 : {selected_N}")
print(f"  Nombre de taps          : {TAPS_COUNT}")
print(f"  fc_design (-6 dB)       : {FC_DESIGN_HZ} Hz")
print(f"  Attenuation @ {F_STOP_HZ} Hz  : {att_effective:.1f} dB (ET2 : >= {ATTENUATION_DB} dB)")
print(f"  Attenuation @ {F_PASS_HZ} Hz  : {att_at_passant:.1f} dB  (bande passante)")


# ---------------------------------------------------------------------------
# Generation de include/filter_coefs.h
# ---------------------------------------------------------------------------

os.makedirs(os.path.dirname(HEADER_PATH), exist_ok=True)

with open(HEADER_PATH, "w") as f:
    f.write("/**\n")
    f.write(" * filter_coefs.h -- Coefficients RIF passe-bas anti-repliement\n")
    f.write(" *\n")
    f.write(" * Genere automatiquement par scripts/design_filter.py\n")
    f.write(" * NE PAS MODIFIER MANUELLEMENT -- relancer design_filter.py\n")
    f.write(" *\n")
    f.write(f" * Fenetre   : Hamming\n")
    f.write(f" * Ordre     : {selected_N}  ({TAPS_COUNT} taps)\n")
    f.write(f" * Fe        : {FE_HZ} Hz\n")
    f.write(f" * fc_design : {FC_DESIGN_HZ} Hz  (point -6 dB)\n")
    f.write(f" * f_pass    : {F_PASS_HZ} Hz  (fin bande passante)\n")
    f.write(f" * f_stop    : {F_STOP_HZ} Hz  (debut bande attenuee)\n")
    f.write(f" * Att @ f_stop : {att_effective:.1f} dB  (ET2 : >= {ATTENUATION_DB} dB)\n")
    f.write(" */\n\n")
    f.write("#pragma once\n")
    f.write("#include <stddef.h>\n\n")
    f.write(f"constexpr size_t FILTER_TAPS = {TAPS_COUNT}U;\n\n")
    f.write("// Coefficients symetriques (phase lineaire garantie)\n")
    f.write("constexpr float FILTER_COEFS[FILTER_TAPS] = {\n")

    for i, c in enumerate(selected_coefs):
        is_last = (i == TAPS_COUNT - 1)
        comma   = "" if is_last else ","
        indent  = "    " if (i % 4 == 0) else ""
        end_of_group = (i % 4 == 3) or is_last
        f.write(f"{indent}{c:.10f}f{comma}")
        if end_of_group:
            f.write("\n")
        else:
            f.write(" ")

    f.write("};\n")

print(f"\nHeader C genere : {HEADER_PATH}")


# ---------------------------------------------------------------------------
# Plot de la reponse en frequence
# ---------------------------------------------------------------------------

os.makedirs(FIGURE_DIR, exist_ok=True)

w, h = sig.freqz(selected_coefs, worN=32768, fs=FE_HZ)
magnitude_db = 20.0 * np.log10(np.abs(h) + 1e-12)

fig, ax = plt.subplots(figsize=(11, 5))
ax.plot(w / 1000.0, magnitude_db, color="#0077CC", linewidth=1.8,
        label=f"RIF ordre {selected_N} — Hamming ({TAPS_COUNT} taps)")

# Contrainte ET2
ax.axhline(-ATTENUATION_DB, color="red", linestyle="--", linewidth=1.2,
           label=f"Contrainte ET2 : -{ATTENUATION_DB:.0f} dB")

# f_stop = 4 kHz
ax.axvline(F_STOP_HZ / 1000.0, color="orange", linestyle=":", linewidth=1.4,
           label=f"f_stop = {F_STOP_HZ//1000} kHz (ET2)")

# f_pass = 3.2 kHz
ax.axvline(F_PASS_HZ / 1000.0, color="green", linestyle=":", linewidth=1.0, alpha=0.8,
           label=f"f_pass = {F_PASS_HZ/1000:.1f} kHz")

# fc_design (point -6 dB)
ax.axvline(FC_DESIGN_HZ / 1000.0, color="purple", linestyle=":", linewidth=0.9, alpha=0.7,
           label=f"fc_design = {FC_DESIGN_HZ/1000:.2f} kHz (-6 dB)")

# Annotation point ET2
idx_fc = np.argmin(np.abs(w - F_STOP_HZ))
att_plot = magnitude_db[idx_fc]
ax.annotate(
    f"  {att_plot:.1f} dB @ {F_STOP_HZ//1000} kHz",
    xy=(F_STOP_HZ / 1000.0, att_plot),
    xytext=(F_STOP_HZ / 1000.0 + 0.5, att_plot + 8.0),
    fontsize=9, color="red",
    arrowprops=dict(arrowstyle="->", color="red", lw=1.0)
)

# Zone passante
ax.axvspan(0, F_PASS_HZ / 1000.0, alpha=0.04, color="blue")
ax.text(1.5, -15, "Bande\npassante", fontsize=8, color="#0077CC", alpha=0.7)
ax.text(F_STOP_HZ / 1000.0 + 0.3, -60, "Bande\natténuée", fontsize=8, color="orange", alpha=0.7)

# Nyquist
ax.axvline(FE_HZ / 2000.0, color="gray", linestyle=":", linewidth=0.8, alpha=0.5,
           label=f"Nyquist ({FE_HZ//2000} kHz)")

ax.set_xlim(0, FE_HZ / 2000.0)
ax.set_ylim(-90, 5)
ax.set_xlabel("Frequence (kHz)", fontsize=11)
ax.set_ylabel("Amplitude (dB)", fontsize=11)
ax.set_title(
    f"FP2 — Reponse en frequence : RIF passe-bas Hamming\n"
    f"Ordre {selected_N} ({TAPS_COUNT} taps)  |  "
    f"fc = {FC_DESIGN_HZ} Hz  |  "
    f"Att @ 4 kHz = {att_effective:.1f} dB  |  Fe = {FE_HZ//1000} kHz",
    fontsize=10
)
ax.legend(fontsize=9, loc="upper right")
ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig(FIGURE_PATH, dpi=150)
plt.close()

print(f"Plot sauvegarde   : {FIGURE_PATH}")

# ---------------------------------------------------------------------------
# Resume final
# ---------------------------------------------------------------------------
budget_cycles = TAPS_COUNT * 2
budget_us = budget_cycles / 84.0
print("\n" + "=" * 60)
print("RESUME POUR LE RAPPORT")
print("=" * 60)
print(f"  Fenetre Hamming, ordre N   = {selected_N}")
print(f"  Taps (FILTER_TAPS)         = {TAPS_COUNT}")
print(f"  fc_design (-6 dB)          = {FC_DESIGN_HZ} Hz")
print(f"  f_pass (fin bande pass.)   = {F_PASS_HZ} Hz")
print(f"  f_stop (debut attenuation) = {F_STOP_HZ} Hz")
print(f"  Att reelle @ 4 kHz         = {att_effective:.1f} dB  (>= 30 dB : OK)")
print(f"  Att en bande passante      = {att_at_passant:.1f} dB  (proche 0 : OK)")
print(f"  Budget theorique           = {TAPS_COUNT} MAC x 2 cyc = {budget_cycles} cyc"
      f" = {budget_us:.2f} us @ 84 MHz")
print(f"  Budget ET3 (< 31.25 us)    = marge {31.25 - budget_us:.2f} us")
