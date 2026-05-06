#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * MFCC computation in Q15 fixed-point.
 * Spec: docs/superpowers/specs/2026-05-06-fp4-mfcc-design.md
 */

constexpr size_t MFCC_FRAMES        = 62;
constexpr size_t MFCC_COEFS         = 13;
constexpr size_t MFCC_AUDIO_SAMPLES = 8000;  // 1 s @ 8 kHz
constexpr size_t MFCC_FFT_SIZE      = 256;
constexpr size_t MFCC_HOP_SIZE      = 128;
constexpr size_t MFCC_N_MEL         = 26;

/**
 * Compute the 62x13 MFCC matrix from 1 s of audio at 8 kHz.
 *
 * @param audio_8khz   Pointer to MFCC_AUDIO_SAMPLES int16 samples (mutable: in-place preemphasis).
 * @param mfcc_out     Output matrix [62][13] of int16 Q15 MFCCs.
 *
 * Cost: ~17 ms on SAM3X8E @ 84 MHz (FFT dominant at ~15 ms).
 */
void compute_mfcc(int16_t* audio_8khz, int16_t mfcc_out[MFCC_FRAMES][MFCC_COEFS]);
