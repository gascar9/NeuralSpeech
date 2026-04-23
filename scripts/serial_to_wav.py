"""
FP3 — Réception audio série → fichier .wav

Usage :
    python scripts/serial_to_wav.py COM3
    python scripts/serial_to_wav.py COM3 mon_enregistrement.wav

Protocole Arduino :
    1. La ligne texte "FP3:START\r\n" marque le début du flux binaire.
    2. Suit immédiatement 16 000 octets = 8000 int16 little-endian (1 s @ 8 kHz).
"""

import sys
import struct
import wave
import serial

PORT     = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD     = 250000
N        = 16000         # 2 s à 8 kHz
FS       = 8000
GAIN     = 16            # ADC 12 bits centré → plage 16 bits complète
OUT_FILE = sys.argv[2] if len(sys.argv) > 2 else "electronique.wav"

print(f"Connexion sur {PORT} @ {BAUD} baud...")
with serial.Serial(PORT, BAUD, timeout=30) as ser:
    print("En attente du bouton (appuyez sur le bouton de l'Arduino)...")
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            print(f"  < {line}")
        if line == "FP3:START":
            break

    print(f"Réception de {N * 2} octets...")
    raw = ser.read(N * 2)

    # Vider le reste (ligne "[FP3] Transfert termine.")
    ser.readline()

if len(raw) != N * 2:
    print(f"ERREUR : {len(raw)} octets reçus, {N * 2} attendus.")
    sys.exit(1)

samples_raw = struct.unpack(f"<{N}h", raw)
samples = [max(-32768, min(32767, s * GAIN)) for s in samples_raw]

with wave.open(OUT_FILE, "w") as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(FS)
    wf.writeframes(struct.pack(f"<{N}h", *samples))

print(f"Fichier '{OUT_FILE}' généré — {N} échantillons, {FS} Hz, mono 16 bits.")
print("Ouvrez-le dans Audacity pour valider (ET4).")
