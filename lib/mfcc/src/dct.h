#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Apply DCT-II from 26 mel log energies to 13 MFCC coefficients.
 * Output: 13 int16 Q11 MFCCs (same scale as log2_q15 input).
 */
void dct_q15(const int16_t* log_mel_q11, int16_t* mfcc_out);
