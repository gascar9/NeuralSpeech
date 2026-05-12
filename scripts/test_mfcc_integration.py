#!/usr/bin/env python3
"""
FP4 integration test — bit-exact comparison firmware ↔ Python mirror.

Two modes:
  --self-check : runs the Python mirror against itself with synthetic audio
                 (verifies the mirror is internally consistent)
  --firmware   : sends test audio to the Due via serial, receives MFCC dump,
                 compares with Python mirror for bit-exact match.

Usage:
  .venv/bin/python3 scripts/test_mfcc_integration.py --self-check
  .venv/bin/python3 scripts/test_mfcc_integration.py --firmware --port /dev/cu.usbmodem101
"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent.parent))
from scripts.mfcc_reference import compute_mfcc


def gen_test_audio(kind: str) -> np.ndarray:
    """Generate one of the canonical test audios."""
    fs = 8000
    n = 8000
    t = np.arange(n) / fs
    if kind == "sin_1khz":
        return (16000 * np.sin(2 * np.pi * 1000 * t)).astype(np.int16)
    if kind == "silence":
        return np.zeros(n, dtype=np.int16)
    if kind == "white_noise":
        rng = np.random.default_rng(42)
        return rng.integers(-8000, 8000, size=n, dtype=np.int16)
    raise ValueError(f"unknown kind: {kind}")


def self_check():
    """Run the mirror on three test audios and verify it produces non-degenerate output."""
    print("=== SELF-CHECK (Python mirror only) ===")
    for kind in ["sin_1khz", "silence", "white_noise"]:
        audio = gen_test_audio(kind)
        mfcc = compute_mfcc(audio)
        print(f"\n{kind}:")
        print(f"  shape: {mfcc.shape}")
        print(f"  range: [{mfcc.min()}, {mfcc.max()}]")
        print(f"  first frame: {mfcc[0]}")
        if kind != "silence":
            assert mfcc.any(), f"all zeros for {kind} — likely a bug"
    print("\n[OK] self-check passed")


def firmware_check(port: str):
    """Send test audio to firmware, receive MFCC, compare bit-exact."""
    import struct
    import time
    import serial

    TEST_AUDIO_KIND = "sin_1khz"
    audio = gen_test_audio(TEST_AUDIO_KIND)

    # Compute reference
    reference_mfcc = compute_mfcc(audio)

    print(f"=== FIRMWARE CHECK (port={port}, audio={TEST_AUDIO_KIND}) ===")
    print(f"Reference MFCC shape: {reference_mfcc.shape}")

    # Open serial, send audio, await MFCC dump
    # Protocol (defined in src/main.cpp test mode):
    #   PC -> Due : 0xDEADC0DE (4 bytes) + 8000 * int16 LE audio (16000 bytes)
    #   Due -> PC : 0xCAFEBABE (4) + 62 (uint32 LE) + 13 (uint32 LE) + 62*13*int16 LE + 0xC0DEBABE
    ser = serial.Serial(port, 250_000, timeout=10.0)
    time.sleep(1.5)
    ser.reset_input_buffer()

    # Send command: enter test mode
    ser.write(b"\xDE\xAD\xC0\xDE")
    ser.write(audio.tobytes())
    print("Audio sent, waiting for MFCC dump...")

    # Wait for MFCC magic header
    deadline = time.time() + 10.0
    buf = bytearray()
    while time.time() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        buf += chunk
        if buf.endswith(b"\xCA\xFE\xBA\xBE"):
            break
    else:
        print("[FAIL] MFCC magic header not received")
        return False

    n_frames = struct.unpack("<I", ser.read(4))[0]
    n_coefs  = struct.unpack("<I", ser.read(4))[0]
    assert n_frames == 62 and n_coefs == 13, f"unexpected dims: {n_frames}x{n_coefs}"

    payload = ser.read(n_frames * n_coefs * 2)
    footer = ser.read(4)
    assert footer == b"\xC0\xDE\xBA\xBE", f"bad footer: {footer.hex()}"

    firmware_mfcc = np.frombuffer(payload, dtype=np.int16).reshape(n_frames, n_coefs)

    # Compare
    diff = firmware_mfcc.astype(int) - reference_mfcc.astype(int)
    max_abs_diff = np.abs(diff).max()
    n_diff = (diff != 0).sum()

    print(f"Firmware vs Python diff:")
    print(f"  max abs diff : {max_abs_diff}")
    print(f"  n different  : {n_diff} / {62 * 13}")

    if max_abs_diff == 0:
        print("[OK] BIT-EXACT match firmware ↔ Python")
        return True
    else:
        print("[FAIL] divergence detected")
        # Save artefacts for debug
        np.save("test_firmware_mfcc.npy", firmware_mfcc)
        np.save("test_reference_mfcc.npy", reference_mfcc)
        print("Saved test_firmware_mfcc.npy and test_reference_mfcc.npy for inspection")
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-check", action="store_true",
                    help="run mirror Python only (no firmware needed)")
    ap.add_argument("--firmware", action="store_true",
                    help="run firmware comparison test")
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    args = ap.parse_args()

    if args.self_check:
        self_check()
    elif args.firmware:
        ok = firmware_check(args.port)
        sys.exit(0 if ok else 1)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
