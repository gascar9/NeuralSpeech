#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Apply 26 triangular MEL filters to the magnitude squared spectrum.
 * Output: 26 mel-band energies (int32, non-negative).
 *
 * Each filter uses the precomputed MelFilter struct from tables.h.
 */
void mel_filter_bank(const int32_t* power, int32_t* mel_energies_out);
