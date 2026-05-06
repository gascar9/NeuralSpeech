#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr int16_t PREEMPHASIS_ALPHA_Q15 = 31785;  // round(0.97 * 32768)

/**
 * Apply preemphasis HPF in-place: y[n] = x[n] - 0.97 * x[n-1]
 * Output is clamped to [INT16_MIN, INT16_MAX].
 */
void preemphasis_q15(int16_t* audio, size_t n_samples);
