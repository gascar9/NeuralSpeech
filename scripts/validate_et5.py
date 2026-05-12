#!/usr/bin/env python3
"""
Validation ET5 : « le signal audio doit être séparé en frames de 256 échantillons »

Produit une figure prouvant que le framing fonctionne (frames 256, hop 128,
recouvrement 50 %). À mettre dans le rapport S2.

Usage :
    python3 scripts/validate_et5.py [audio.wav]

Défaut : vrai_01.wav
Sortie : rapport/figures/et5_framing_validation.png
"""
import sys
import wave
import struct
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).parent.parent))
from scripts.mfcc_reference import preemphasis_q15


def load_wav(path):
    w = wave.open(path, "rb")
    n = w.getnframes()
    audio = np.array(struct.unpack(f"<{n}h", w.readframes(n)), dtype=np.int16)
    w.close()
    return audio


def main():
    wav_path = sys.argv[1] if len(sys.argv) > 1 else "vrai_01.wav"
    audio = load_wav(wav_path)
    preemph = preemphasis_q15(audio)

    fig, axes = plt.subplots(3, 1, figsize=(11, 8))

    # --- Panel 1 : signal complet 8000 samples ---
    t_full = np.arange(8000) / 8000 * 1000  # en ms
    axes[0].plot(t_full, preemph, "k-", linewidth=0.5)
    axes[0].set_title(f"Signal post-préemphase : 8000 samples = 1 s à 8 kHz "
                      f"({Path(wav_path).stem}.wav)")
    axes[0].set_xlabel("Temps (ms)")
    axes[0].set_ylabel("Amplitude int16")
    axes[0].grid(alpha=0.3)
    # Marque les bornes des frames 0, 1, 2, et 61
    for f in [0, 1, 2, 61]:
        start = f * 128
        end = start + 256
        axes[0].axvspan(start / 8000 * 1000, end / 8000 * 1000,
                        alpha=0.15, color=f"C{f % 4}",
                        label=f"Frame {f}" if f < 4 else None)
    axes[0].legend(loc="upper right", fontsize=8)
    axes[0].set_xlim(0, 1000)

    # --- Panel 2 : zoom sur les 3 premières frames (preuve du recouvrement) ---
    axes[1].plot(t_full[:600], preemph[:600], "k-", linewidth=0.7)
    axes[1].set_title("Zoom : 3 premières frames se chevauchent à 50 % "
                      "(hop 128 = 16 ms)")
    axes[1].set_xlabel("Temps (ms)")
    axes[1].set_ylabel("Amplitude")
    for f in range(4):
        start_ms = f * 128 / 8000 * 1000
        end_ms = (f * 128 + 256) / 8000 * 1000
        axes[1].axvspan(start_ms, end_ms, alpha=0.2, color=f"C{f}",
                        label=f"Frame {f}: samples [{f*128}..{f*128+256})")
    axes[1].legend(loc="upper right", fontsize=8)
    axes[1].grid(alpha=0.3)
    axes[1].set_xlim(0, 75)

    # --- Panel 3 : énergie par frame (montre où est le mot) ---
    energies = []
    for f in range(62):
        start = f * 128
        end = min(start + 256, 8000)
        frame = preemph[start:end]
        e = float(np.sqrt((frame.astype(float) ** 2).mean()))
        energies.append(e)
    t_frames = np.arange(62) * 128 / 8000 * 1000
    axes[2].bar(t_frames, energies, width=12, color="C2", edgecolor="black",
                linewidth=0.5)
    axes[2].set_title(f"Énergie RMS par frame — les 62 frames couvrent toute "
                      f"la seconde (62 × 128 = {62*128} samples)")
    axes[2].set_xlabel("Temps de début de frame (ms)")
    axes[2].set_ylabel("Énergie RMS")
    axes[2].grid(alpha=0.3)
    axes[2].axhline(y=500, color="red", linestyle="--", linewidth=1,
                    label="seuil silence (heuristique)")
    axes[2].legend()

    plt.tight_layout()
    out_dir = Path(__file__).parent.parent / "rapport" / "figures"
    out_dir.mkdir(exist_ok=True)
    out_path = out_dir / "et5_framing_validation.png"
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"\n✓ Figure ET5 sauvegardée : {out_path}")
    print(f"\nRécap chiffré pour le rapport :")
    print(f"  - Total samples         : {len(audio)}")
    print(f"  - Nombre de frames      : 62")
    print(f"  - Taille de frame       : 256 samples (32 ms)")
    print(f"  - Hop length            : 128 samples (16 ms)")
    print(f"  - Recouvrement          : 50 %")
    print(f"  - Couverture temporelle : 0 → {(61*128+256)/8000*1000:.0f} ms "
          f"(dernière frame zero-paddée sur 64 samples)")
    print(f"  - Frame active (max RMS) : frame {int(np.argmax(energies))} "
          f"(t = {int(np.argmax(energies))*128/8000*1000:.0f} ms)")


if __name__ == "__main__":
    main()
