#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Compute log2 of 26 mel energies using LUT + CLZ + linear interpolation.
 * Input  : 26 int32 mel energies (non-negative)
 * Output : 26 int16 log2 values in Q11 format (5 integer bits + 11 fractional). INT16_MIN if input is 0.
 */
void log2_q15(const int32_t* mel_energies, int16_t* log_mel_out);
