#!/usr/bin/env python3
"""
Capture du dataset vrai/faux — version interactive contrôlée par Entrée.

Pour chaque capture :
  1. Tu appuies sur Entrée (clavier) pour lancer la capture
  2. fp3_recv.py se prépare (~1.5 s, reset Arduino)
  3. Il affiche "En attente du header magic"
  4. Tu appuies sur D2 (bouton Arduino) ET tu dis le mot
  5. Le firmware enregistre 1 s, dump le WAV + MFCC
  6. Le script revient à toi pour la capture suivante

Usage :
    .venv/bin/python3 scripts/capture_dataset.py vrai gaspard 25
"""
import argparse
import glob
import subprocess
import sys
from pathlib import Path


def detect_port():
    """Trouve le port série de l'Arduino Due (auto-détection)."""
    candidates = []
    for pattern in ("/dev/tty.usbmodem*", "/dev/cu.usbmodem*",
                    "/dev/ttyACM*", "COM*"):
        candidates.extend(glob.glob(pattern))
    # tty.usbmodem en premier (callin port, plus stable que cu sur macOS)
    candidates.sort(key=lambda p: 0 if "tty" in p else 1)
    return candidates[0] if candidates else None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("word", choices=["vrai", "faux"],
                    help="mot à enregistrer")
    ap.add_argument("speaker", help="nom du locuteur (gaspard, maxence, ...)")
    ap.add_argument("count", type=int, help="nombre d'enregistrements à faire")
    ap.add_argument("--start", type=int, default=None,
                    help="numéro de départ (défaut : auto-détecté)")
    ap.add_argument("--port", default=None,
                    help="port série (défaut : auto-détection)")
    args = ap.parse_args()

    # --- Auto-détection du port si non spécifié
    port = args.port or detect_port()
    if not port:
        print("\n  ❌ Aucun port série usbmodem détecté.")
        print("     Branche l'Arduino Due et ferme le moniteur série PlatformIO.")
        print("     Ports disponibles :")
        import os
        for f in sorted(os.listdir("/dev")):
            if "usbmodem" in f or "ttyACM" in f:
                print(f"       /dev/{f}")
        sys.exit(1)

    out_dir = Path("dataset") / args.word
    out_dir.mkdir(parents=True, exist_ok=True)

    # Auto-détection du prochain numéro
    if args.start is None:
        existing = sorted(out_dir.glob(f"{args.word}_{args.speaker}_*.wav"))
        if existing:
            last_num = int(existing[-1].stem.split("_")[-1])
            start_num = last_num + 1
        else:
            start_num = 1
    else:
        start_num = args.start

    print(f"\n{'═' * 70}")
    print(f"  CAPTURE DATASET — mode interactif")
    print(f"  Mot       : '{args.word.upper()}'")
    print(f"  Locuteur  : {args.speaker}")
    print(f"  Port série: {port}")
    print(f"  Captures  : N°{start_num} → N°{start_num + args.count - 1} "
          f"({args.count} captures)")
    print(f"  Dossier   : {out_dir}/")
    print(f"{'═' * 70}\n")
    print(f"  📋 Procédure pour chaque capture :")
    print(f"    1. Tu appuies sur Entrée (CLAVIER) — le script lance fp3_recv.py")
    print(f"    2. Le script attend ~1.5 s puis affiche :")
    print(f"         'En attente du header magic (appuyez sur D2 de l'Arduino Due)'")
    print(f"    3. À ce MOMENT-LÀ, tu appuies sur D2 (BOUTON Arduino)")
    print(f"    4. Tu dis '{args.word.upper()}' clairement (le firmware enregistre 1 s)")
    print(f"    5. Le fichier WAV + MFCC est sauvegardé")
    print(f"    6. Retour à toi pour la capture suivante\n")
    print(f"  ⚠ Important : D2 = bouton Arduino, PAS Entrée du clavier !")
    print(f"     D2 ne déclenche rien tant que fp3_recv.py n'est pas en attente.\n")

    try:
        input("  ▶ Appuie sur Entrée pour DÉMARRER la session...")
    except KeyboardInterrupt:
        print("\n  Annulé.")
        return

    success_count = 0
    skip_count = 0

    for i in range(args.count):
        num = start_num + i
        filename = out_dir / f"{args.word}_{args.speaker}_{num:03d}.wav"

        print(f"\n{'─' * 70}")
        print(f"  [{i+1:2d}/{args.count}]  CAPTURE N°{num}  →  {filename.name}")
        print(f"{'─' * 70}")

        try:
            cmd = input(f"  Prêt à dire '{args.word.upper()}' ? "
                        f"[Entrée=lancer | s=skip | q=quit] : ").strip().lower()
        except KeyboardInterrupt:
            print("\n\n  Interrompu par utilisateur.")
            break

        if cmd == "q":
            print("  Sortie demandée.")
            break
        if cmd == "s":
            print("  → Capture sautée.")
            skip_count += 1
            continue

        print(f"\n  ⏳ Démarrage de fp3_recv.py (attends ~1.5 s + le message")
        print(f"     'En attente du header magic', PUIS appuie sur D2 + dis '{args.word.upper()}')\n")

        try:
            result = subprocess.run(
                ["python3", "scripts/fp3_recv.py",
                 "--port", port,
                 "--out", str(filename),
                 "--timeout", "120"],
                check=False,
            )
            if result.returncode == 0 and filename.exists():
                success_count += 1
                size_kb = filename.stat().st_size / 1024
                print(f"\n  ✅ OK — {filename.name} ({size_kb:.1f} Kio)")
            else:
                print(f"\n  ❌ Échec (code retour {result.returncode}). "
                      f"Tu peux retenter en relançant le script.")
        except KeyboardInterrupt:
            print(f"\n\n  Interrompu pendant la capture {num}.")
            break

    print(f"\n{'═' * 70}")
    print(f"  TERMINÉ")
    print(f"    Captures réussies : {success_count} / {args.count}")
    if skip_count:
        print(f"    Captures sautées  : {skip_count}")
    print(f"    Total dans {out_dir}/ : "
          f"{len(list(out_dir.glob(f'{args.word}_*.wav')))} fichiers .wav")
    print(f"{'═' * 70}\n")


if __name__ == "__main__":
    main()
