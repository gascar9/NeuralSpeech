#pragma once
#include <stdint.h>

struct MelFilter {
    uint8_t start;
    uint8_t peak;
    uint8_t end;       // exclusive
    int16_t coefs_q15[20];  // padded with 0 if filter shorter (max actual = 20)
};

extern const int16_t HAMMING_LUT[256];
extern const int16_t TWIDDLE_COS[128];
extern const int16_t TWIDDLE_SIN[128];
extern const int16_t LOG2_LUT[1024];
extern const int16_t LOG2_SLOPES[1024];
extern const int16_t DCT_Q15[13][26];
extern const uint8_t MEL_MAX_COEFS;
extern const MelFilter MEL_FILTERS[26];
