#include "magnitude.h"

void magnitude_squared(const int16_t* re, const int16_t* im, int32_t* power_out) {
    for (size_t k = 0; k < MAGNITUDE_BINS; k++) {
        int32_t r = (int32_t)re[k];
        int32_t i = (int32_t)im[k];
        power_out[k] = r * r + i * i;
    }
}
