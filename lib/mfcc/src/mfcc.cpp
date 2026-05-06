#include "mfcc.h"

void compute_mfcc(int16_t* audio, int16_t mfcc_out[MFCC_FRAMES][MFCC_COEFS]) {
    // Stub — filled progressively from Task 4 onwards
    for (size_t f = 0; f < MFCC_FRAMES; f++)
        for (size_t k = 0; k < MFCC_COEFS; k++)
            mfcc_out[f][k] = 0;
}
