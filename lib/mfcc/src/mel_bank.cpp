#include "mel_bank.h"
#include "tables.h"

void mel_filter_bank(const int32_t* power, int32_t* mel_out) {
    for (size_t m = 0; m < 26; m++) {
        const MelFilter& flt = MEL_FILTERS[m];
        int32_t energy = 0;
        size_t coef_i = 0;
        for (size_t k = flt.start; k < flt.end; k++, coef_i++) {
            // power[k] is up to ~134M; coef_q15 up to 32767.
            // To keep within int32: scale power down by >>15 first.
            int32_t scaled_power = power[k] >> 15;       // ~4k max
            energy += scaled_power * (int32_t)flt.coefs_q15[coef_i];
        }
        mel_out[m] = energy;
    }
}
