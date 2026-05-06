#include "hamming.h"
#include "tables.h"

void hamming_q15(const int16_t* frame_in, int16_t* frame_out) {
    for (size_t n = 0; n < HAMMING_SIZE; n++) {
        int32_t windowed = ((int32_t)frame_in[n] * (int32_t)HAMMING_LUT[n]) >> 15;
        frame_out[n] = (int16_t)windowed;
    }
}
