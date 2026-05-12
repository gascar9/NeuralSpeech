#!/usr/bin/env python3
"""
Script pédagogique — montre comment la chaîne MFCC découpe un enregistrement
en 62 frames et produit une matrice 62×13 de coefficients.

Usage :
    python3 scripts/explain_mfcc.py [nom_fichier.wav]

Défaut : vrai_01.wav
"""
import sys
import wave
import struct
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent.parent))
from scripts.mfcc_reference import (
    preemphasis_q15, hamming_q15, fft_q15_radix2,
    magnitude_squared, mel_filter_bank, log2_q15, dct_q15, compute_mfcc,
)


def load_wav(path):
    w = wave.open(path, "rb")
    n = w.getnframes()
    audio = np.array(struct.unpack(f"<{n}h", w.readframes(n)), dtype=np.int16)
    w.close()
    return audio


def main():
    wav_path = sys.argv[1] if len(sys.argv) > 1 else "vrai_01.wav"
    audio = load_wav(wav_path)
    print(f"\nFichier : {wav_path}")
    print(f"  Total samples : {len(audio)} (= 1 seconde à 8 kHz)")
    print(f"  Plage         : [{audio.min()}, {audio.max()}]\n")

    # === ÉTAPE 1 : préemphase ===
    preemph = preemphasis_q15(audio)

    # === ÉTAPE 2 : DÉCOUPAGE EN FRAMES — preuve visuelle ===
    print("=" * 62)
    print("  ÉTAPE 2 — Découpage en 62 frames de 256 samples (hop 128)")
    print("=" * 62)
    energies = []
    for f in range(62):
        start = f * 128
        end = min(start + 256, 8000)
        frame = preemph[start:end]
        e = int(np.sqrt((frame.astype(float) ** 2).mean()))
        energies.append(e)

    print(f"\n  Énergie RMS par frame (62 frames) :\n")
    e_max = max(energies)
    for f in range(62):
        bar = "█" * int(40 * energies[f] / max(e_max, 1))
        t_ms = f * 128 / 8000 * 1000
        print(f"   Frame {f:2d} (t={t_ms:5.0f} ms) : RMS={energies[f]:5d}  {bar}")

    active = int(np.argmax(energies))
    print(f"\n  → Frame la plus active : {active} (t = {active*128/8000*1000:.0f} ms)")
    print(f"  → Les frames silencieuses (énergie faible) sont normales : ")
    print(f"    1 seconde d'enregistrement, le mot dure ~300 ms, le reste = silence.\n")

    # === MATRICE MFCC COMPLÈTE ===
    print("=" * 62)
    print("  MATRICE MFCC FINALE — 62 × 13")
    print("=" * 62)
    mfcc = compute_mfcc(audio)
    print(f"\n  Shape    : {mfcc.shape}")
    print(f"  Range    : [{mfcc.min()}, {mfcc.max()}]\n")

    print(f"  Aperçu — 5 frames silencieuses (début, fin) puis 5 frames vocales :")
    print(f"  Format : MFCC[frame] = [coef0, coef1, ..., coef12]\n")
    interesting = [0, 5, 10, 15, 20, active-2, active, active+2, 55, 60]
    for f in interesting:
        if 0 <= f < 62:
            label = "← VOIX" if energies[f] > 1000 else "  silence"
            vals = ", ".join(f"{v:>6d}" for v in mfcc[f])
            print(f"   MFCC[{f:2d}] = [{vals}]  {label}")

    # === SIGNIFICATION DES COEFFICIENTS ===
    print()
    print("=" * 62)
    print("  SIGNIFICATION DES 13 COEFFICIENTS MFCC")
    print("=" * 62)
    print("""
    coef 0  : ÉNERGIE totale (≈ volume sonore)
              Frame silencieuse → ~-32767, frame vocale → grande valeur

    coef 1  : Pente globale du spectre (grave vs aigu)

    coef 2  : Forme générale (en U ou en cloche)

    coef 3-5: FORMANTS F1, F2, F3 → identifie la VOYELLE
              (/ɛ/ "vrai" ≠ /o/ "faux" → coefs différents)

    coef 6-9: Détails sur les formants supérieurs

    coef 10-12: Microstructure → consonnes, voisement
                (V vibre / F souffle → coefs différents)
    """)


if __name__ == "__main__":
    main()
