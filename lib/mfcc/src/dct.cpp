#include "dct.h"
#include "tables.h"
#include <climits>

void dct_q15(const int16_t* log_mel_q11, int16_t* mfcc_out) {
    for (size_t k = 0; k < 13; k++) {
        // int64 accumulator to avoid overflow when log_mel has INT16_MIN values
        // (sentinel for log2(0)) and DCT[0] has all positive coefficients.
        // Worst case: 26 × 32768 × 32767 ≈ 2.79e10, exceeds INT32_MAX = 2.15e9.
        int64_t sum = 0;
        for (size_t m = 0; m < 26; m++) {
            sum += (int64_t)log_mel_q11[m] * (int64_t)DCT_Q15[k][m];
        }
        int64_t result = sum >> 15;
        // Saturate to int16 range to match the Python mirror (and produce
        // well-defined DSP behavior on extreme inputs).
        if (result >  INT16_MAX) result = INT16_MAX;
        if (result < -INT16_MAX) result = -INT16_MAX;
        mfcc_out[k] = (int16_t)result;
    }
}
