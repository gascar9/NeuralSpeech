#include "dct.h"
#include "tables.h"

void dct_q15(const int16_t* log_mel_q11, int16_t* mfcc_out) {
    for (size_t k = 0; k < 13; k++) {
        int32_t sum = 0;
        for (size_t m = 0; m < 26; m++) {
            sum += (int32_t)log_mel_q11[m] * (int32_t)DCT_Q15[k][m];
        }
        mfcc_out[k] = (int16_t)(sum >> 15);
    }
}
