#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr size_t FFT_N = 256;

/**
 * In-place 256-point radix-2 FFT in Q15.
 * Scaling: unconditional >> 1 at each of the 8 stages to avoid int16 overflow.
 * Net gain: 1 / 2^8 = 1/256 (precision loss ~8 bits, acceptable for MFCC).
 *
 * Input:  real[256], imag[256] — initially imag is all zeros (real input)
 * Output: real[256], imag[256] — frequency domain, bins 0..127 are unique (hermitian)
 */
void fft_q15_radix2(int16_t* real, int16_t* imag);
