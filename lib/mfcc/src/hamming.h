#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr size_t HAMMING_SIZE = 256;

/**
 * Apply Hamming window to a frame in-place using HAMMING_LUT.
 * out[n] = (frame[n] * HAMMING_LUT[n]) >> 15
 */
void hamming_q15(const int16_t* frame_in, int16_t* frame_out);
