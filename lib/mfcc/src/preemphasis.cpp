#include "preemphasis.h"

void preemphasis_q15(int16_t* audio, size_t n_samples) {
    int16_t prev = 0;
    for (size_t i = 0; i < n_samples; i++) {
        int32_t scaled = ((int32_t)PREEMPHASIS_ALPHA_Q15 * (int32_t)prev) >> 15;
        int32_t y = (int32_t)audio[i] - scaled;
        if (y >  32767) y =  32767;
        if (y < -32768) y = -32768;
        prev = audio[i];          // save BEFORE overwriting
        audio[i] = (int16_t)y;
    }
}
