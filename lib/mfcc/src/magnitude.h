#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr size_t MAGNITUDE_BINS = 128;  // hermitian symmetry: only bins 0..127

/**
 * Compute |X[k]|^2 = re[k]^2 + im[k]^2 for k = 0..127.
 * Output is int32 (always non-negative).
 */
void magnitude_squared(const int16_t* re, const int16_t* im, int32_t* power_out);
