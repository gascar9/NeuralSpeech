"""
NeuralSpeech — FP3 : Récepteur série → WAV (ET4)
=================================================

Écoute le port série, détecte le bloc binaire dumpé par le firmware
(délimité par les magic bytes), et écrit un fichier .wav mono 8 kHz
importable directement dans Audacity.

Protocole série (voir src/main.cpp, section FP3) :
  [0xAA 0x55 0xAA 0x55]        ← magic header (4 octets)
  [uint32 nb_samples LE]       ← = 8000 typiquement
  [nb_samples × int16 LE]      ← = 16000 octets = 1 s d'audio à 8 kHz
  [0xDE 0xAD 0xBE 0xEF]        ← magic footer (4 octets)

Les messages texte [FP1]/[FP2]/[FP3] peuvent s'intercaler dans le flux
série pendant le dump — le parser se base uniquement sur les magic bytes.

Usage :
  python3 scripts/fp3_recv.py                         # auto-detect du port
  python3 scripts/fp3_recv.py --port /dev/tty.usbmodem1101
  python3 scripts/fp3_recv.py --out mon_enregistrement.wav

Dépendance : pyserial (`pip install pyserial`)
"""

from __future__ import annotations

import argparse
import datetime as _dt
import glob
import os
import struct
import sys
import time
import wave

try:
    import serial
except ImportError:
    print("ERREUR : la lib pyserial est requise.")
    print("        Installe-la avec :  pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Constantes protocole (doivent correspondre à src/main.cpp)
# ---------------------------------------------------------------------------
HEADER        = b"\xAA\x55\xAA\x55"
FOOTER        = b"\xDE\xAD\xBE\xEF"
BAUDRATE      = 250_000
SAMPLE_RATE   = 8_000
EXPECTED_SAMPLES = 8_000     # 1 s @ 8 kHz


def detect_port() -> str | None:
    """Retourne le premier port série 'usb' détecté, ou None."""
    candidates = []
    for pattern in ("/dev/tty.usbmodem*", "/dev/ttyACM*", "/dev/ttyUSB*",
                    "COM*"):
        candidates.extend(glob.glob(pattern))
    return candidates[0] if candidates else None


def read_until(ser: serial.Serial, needle: bytes, timeout_s: float = 60.0) -> bytes:
    """Lit le flux série jusqu'à trouver la séquence `needle` (incluse).

    Stocke aussi les octets "orphelins" (messages texte entre captures)
    et les logue à la fin pour information.
    """
    deadline = time.time() + timeout_s
    buffer   = bytearray()
    while time.time() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        buffer += chunk
        if buffer.endswith(needle):
            return bytes(buffer)
    raise TimeoutError(
        f"Aucun header magic reçu en {timeout_s:.0f} s. "
        f"La Due est-elle bien branchée ? Le bouton a-t-il été pressé ?"
    )


def extract_text_garbage(raw: bytes) -> str:
    """Extrait les octets ASCII imprimables du pré-header (debug texte)."""
    # on retire le header à la fin
    pre = raw[:-len(HEADER)]
    try:
        return pre.decode("utf-8", errors="replace").strip()
    except Exception:
        return ""


def save_wav(path: str, samples_bytes: bytes) -> None:
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)          # int16 = 2 octets
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(samples_bytes)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=None,
                    help="port série (auto-détection si omis)")
    ap.add_argument("--baudrate", type=int, default=BAUDRATE,
                    help=f"baudrate (défaut {BAUDRATE})")
    ap.add_argument("--out", default=None,
                    help="nom du fichier .wav (défaut : horodaté)")
    ap.add_argument("--timeout", type=float, default=120.0,
                    help="délai max d'attente du header (secondes)")
    ap.add_argument("--quiet", action="store_true",
                    help="n'affiche pas les messages texte reçus")
    args = ap.parse_args()

    # ---- choix du port ----
    port = args.port or detect_port()
    if not port:
        print("ERREUR : aucun port série usbmodem/ttyACM/COM détecté.")
        print("        Spécifie-le avec --port /chemin/du/port")
        return 1
    print(f"Port série : {port} @ {args.baudrate} baud")

    # ---- nom du fichier de sortie ----
    out_path = args.out
    if not out_path:
        stamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = f"recording_{stamp}.wav"
    out_path = os.path.abspath(out_path)

    # ---- ouverture série ----
    try:
        ser = serial.Serial(port, args.baudrate, timeout=0.1)
    except serial.SerialException as e:
        print(f"ERREUR ouverture série : {e}")
        print("        Ferme le monitor PlatformIO si il est encore ouvert.")
        return 1

    # Évite le faux déclenchement : le kernel macOS/Linux peut garder en cache
    # des octets d'une session précédente (jusqu'à 16-48 Kio). Sans flush, le
    # parser les prendrait pour une nouvelle capture.
    # On laisse aussi l'Arduino Due redémarrer après le pulse DTR de l'ouverture
    # (typique ~1 s pour que le bootloader finisse et que setup() tourne).
    print("Flush buffer série + attente reset Arduino (~1.5 s) ...")
    time.sleep(1.5)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    print(f"\nEn attente du header magic (appuyez sur D2 de l'Arduino Due) ...")
    print(f"Délai max : {args.timeout:.0f} s")

    # ---- attente du header ----
    try:
        raw_before = read_until(ser, HEADER, timeout_s=args.timeout)
    except TimeoutError as e:
        print(f"\n{e}")
        ser.close()
        return 2

    # affiche les textes reçus avant le header (debug)
    garbage = extract_text_garbage(raw_before)
    if garbage and not args.quiet:
        print("\n--- Messages série reçus avant le header ---")
        print(garbage)
        print("---\n")

    # ---- lecture longueur ----
    len_bytes = ser.read(4)
    if len(len_bytes) != 4:
        print("ERREUR : longueur nb_samples non reçue.")
        ser.close()
        return 3
    nb_samples = struct.unpack("<I", len_bytes)[0]
    print(f"Header OK. Annonce : {nb_samples} samples.")

    if nb_samples != EXPECTED_SAMPLES:
        print(f"WARNING : nb_samples ({nb_samples}) != attendu "
              f"({EXPECTED_SAMPLES}). On tente quand même.")

    # ---- lecture payload PCM ----
    payload_size = nb_samples * 2
    payload = bytearray()
    start = time.time()
    while len(payload) < payload_size:
        chunk = ser.read(payload_size - len(payload))
        if chunk:
            payload += chunk
        if time.time() - start > 10.0:
            print(f"ERREUR : payload incomplet ({len(payload)}/{payload_size} octets).")
            ser.close()
            return 4
    elapsed = time.time() - start
    print(f"Payload PCM reçu : {payload_size} octets en {elapsed*1000:.0f} ms "
          f"({payload_size/elapsed/1024:.1f} Kio/s)")

    # ---- vérif footer ----
    footer = ser.read(4)
    if footer != FOOTER:
        print(f"WARNING : footer incorrect ({footer.hex()} != {FOOTER.hex()})")
        print("          Le WAV sera écrit quand même.")

    # ---- écriture WAV ----
    save_wav(out_path, bytes(payload))
    size_kb = os.path.getsize(out_path) / 1024
    print(f"\n✓ WAV écrit : {out_path}  ({size_kb:.1f} Kio)")
    print(f"  Mono, 16 bits, {SAMPLE_RATE} Hz, {nb_samples/SAMPLE_RATE:.2f} s")
    print("\nOuvre-le dans Audacity :  File → Open → " + os.path.basename(out_path))

    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
