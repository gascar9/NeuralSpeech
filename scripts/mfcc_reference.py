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


def hamming_q15(frame_int16: np.ndarray) -> np.ndarray:
    """Mirror of hamming_q15() in lib/mfcc/src/hamming.cpp"""
    out = np.zeros(256, dtype=np.int16)
    for n in range(256):
        windowed = (int(frame_int16[n]) * int(tables.HAMMING_LUT[n])) >> 15
        out[n] = windowed
    return out


def _bit_reverse(x: int, log2n: int) -> int:
    r = 0
    for _ in range(log2n):
        r = (r << 1) | (x & 1)
        x >>= 1
    return r


def fft_q15_radix2(re: np.ndarray, im: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Mirror of fft_q15_radix2() in lib/mfcc/src/fft.cpp"""
    N = 256
    LOG2_N = 8
    re = re.copy().astype(np.int16)
    im = im.copy().astype(np.int16)

    # Bit-reversal
    for i in range(N):
        j = _bit_reverse(i, LOG2_N)
        if j > i:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]

    # Butterflies
    for s in range(1, LOG2_N + 1):
        m = 1 << s
        m_half = m >> 1
        tw_step = N // m
        for k in range(0, N, m):
            for j in range(m_half):
                idx_w = j * tw_step
                wr = int(tables.TWIDDLE_COS[idx_w])
                wi = -int(tables.TWIDDLE_SIN[idx_w])

                t_idx = k + j + m_half
                u_idx = k + j

                tr = (wr * int(re[t_idx]) - wi * int(im[t_idx])) >> 15
                ti = (wr * int(im[t_idx]) + wi * int(re[t_idx])) >> 15

                ur = int(re[u_idx])
                ui = int(im[u_idx])

                re[u_idx] = (ur + tr) >> 1
                im[u_idx] = (ui + ti) >> 1
                re[t_idx] = (ur - tr) >> 1
                im[t_idx] = (ui - ti) >> 1

    return re, im


def magnitude_squared(re: np.ndarray, im: np.ndarray) -> np.ndarray:
    """Mirror of magnitude_squared() in lib/mfcc/src/magnitude.cpp"""
    r = re[:128].astype(np.int32)
    i = im[:128].astype(np.int32)
    return r * r + i * i


def mel_filter_bank(power: np.ndarray) -> np.ndarray:
    """Mirror of mel_filter_bank() in lib/mfcc/src/mel_bank.cpp"""
    out = np.zeros(26, dtype=np.int32)
    for m in range(26):
        flt = tables.MEL_FILTERS[m]
        energy = 0
        coef_i = 0
        for k in range(flt["start"], flt["end"]):
            scaled = int(power[k]) >> 15
            energy += scaled * int(flt["coefs_q15"][coef_i])
            coef_i += 1
        out[m] = energy
    return out


def log2_q15(mel_energies: np.ndarray) -> np.ndarray:
    """Mirror of log2_q15() in lib/mfcc/src/log_lut.cpp.

    Output is in Q11 format (5 integer bits + 11 fractional bits, range ~[-16, +16)).
    """
    out = np.zeros(26, dtype=np.int16)
    for m in range(26):
        x = int(mel_energies[m])
        if x == 0:
            out[m] = -32768
            continue

        # Position of highest set bit
        e = x.bit_length() - 1                # equivalent to 31 - clz(x) for 32-bit

        # Normalized mantissa as 10-bit index
        if e >= 10:
            mantissa = (x >> (e - 10)) & 0x3FF
        else:
            mantissa = (x << (10 - e)) & 0x3FF

        frac_log = int(tables.LOG2_LUT[mantissa])
        result = (e << 11) + (frac_log >> 4)
        result = max(-32767, min(32767, result))
        out[m] = result
    return out
