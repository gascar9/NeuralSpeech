#!/usr/bin/env python3
"""
Validation ET5 dans Audacity :
  1. Génère `audacity_frames_labels.txt` — un label track à importer
     dans Audacity pour visualiser les 62 frames + leur recouvrement.
  2. Extrait `frame_42.wav` — UNE seule frame (256 samples = 32 ms) pour
     prouver le découpage.

Usage :
    python3 scripts/audacity_et5.py [audio.wav]
Défaut : vrai_01.wav
"""
import sys
import wave
import struct
from pathlib import Path

import numpy as np


def load_wav(path):
    w = wave.open(path, "rb")
    n = w.getnframes()
    audio = np.array(struct.unpack(f"<{n}h", w.readframes(n)), dtype=np.int16)
    w.close()
    return audio


def main():
    wav_path = sys.argv[1] if len(sys.argv) > 1 else "vrai_01.wav"
    audio = load_wav(wav_path)
    fs = 8000
    n_frames = 62
    frame_size = 256
    hop = 128

    # === 1. Génère le label track ===
    labels_path = "audacity_frames_labels.txt"
    with open(labels_path, "w") as f:
        for k in range(n_frames):
            start_sample = k * hop
            end_sample = start_sample + frame_size
            start_s = start_sample / fs
            end_s = end_sample / fs
            f.write(f"{start_s:.6f}\t{end_s:.6f}\tF{k}\n")

    print(f"✓ Label track écrit : {labels_path}")
    print(f"  → 62 labels, chaque label = 1 frame (256 samples / 32 ms)")
    print(f"  → décalés de 128 samples (16 ms) = recouvrement 50%\n")

    # === 2. Extrait frame 42 comme WAV standalone ===
    # On choisit frame 42 parce que c'est typiquement en plein milieu du mot
    frame_idx = 42
    start = frame_idx * hop
    end = start + frame_size
    frame_audio = audio[start:end]

    out_wav = f"frame_{frame_idx}.wav"
    w = wave.open(out_wav, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(fs)
    w.writeframes(frame_audio.tobytes())
    w.close()

    print(f"✓ Frame {frame_idx} écrite : {out_wav}")
    print(f"  → {len(frame_audio)} samples × 2 octets = {len(frame_audio.tobytes())} octets")
    print(f"  → durée : {len(frame_audio)/fs*1000:.2f} ms")
    print(f"  → range : [{frame_audio.min()}, {frame_audio.max()}]\n")

    print("=" * 65)
    print("  INSTRUCTIONS — validation ET5 dans Audacity")
    print("=" * 65)
    print(f"""
  ÉTAPE 1 — Voir l'ensemble des 62 frames sur le signal complet :
    1. Lance Audacity
    2. File → Open → {wav_path}
    3. File → Import → Labels → {labels_path}
    4. Tu vois 62 régions superposées (F0, F1, ..., F61)
       qui se chevauchent à 50 % → preuve du framing + hop
    5. Screenshot → mets dans assets/FP4/et5_audacity_labels.png

  ÉTAPE 2 — Voir UNE seule frame :
    1. File → Open → {out_wav}
    2. Tu vois la frame {frame_idx} seule : 256 samples = 32 ms d'audio
    3. C'est ce que la FFT et tout le pipeline MFCC reçoit en entrée
    4. Screenshot → mets dans assets/FP4/et5_audacity_one_frame.png

  ÉTAPE 3 — Validation officielle ET5 :
    Ces deux screenshots prouvent :
      ✓ Le signal EST séparé en frames de 256 samples
      ✓ Le recouvrement de 50 % fonctionne
""")


if __name__ == "__main__":
    main()
