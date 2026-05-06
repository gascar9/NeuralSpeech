"""Mirror Python implementation of the firmware MFCC pipeline (Q15 strict).

This is the bit-exact oracle for `scripts/test_mfcc_integration.py`.
Each function mirrors the corresponding C++ function in lib/mfcc/src/.
"""
import numpy as np
from scripts import mfcc_tables as tables

ALPHA_Q15 = 31785  # round(0.97 * 32768)


def preemphasis_q15(audio_int16: np.ndarray) -> np.ndarray:
    """Mirror of preemphasis_q15() in lib/mfcc/src/preemphasis.cpp"""
    out = np.zeros_like(audio_int16)
    prev = 0
    for i, x in enumerate(audio_int16):
        scaled = (ALPHA_Q15 * prev) >> 15
        y = int(x) - scaled
        out[i] = max(-32768, min(32767, y))
        prev = int(x)
    return out
