#include "log_lut.h"
#include "tables.h"
#include <climits>

void log2_q15(const int32_t* mel_energies, int16_t* log_out) {
    for (size_t m = 0; m < 26; m++) {
        uint32_t x = (uint32_t)mel_energies[m];
        if (x == 0) {
            log_out[m] = INT16_MIN;
            continue;
        }

        // Exponent: position of highest set bit
        int clz = __builtin_clz(x);
        int e = 31 - clz;                      // 0..30 typically

        // Normalize mantissa to [1, 2): shift to put MSB at bit 30, then mask top 11 bits
        // We want a 10-bit fractional index into LOG2_LUT[1024].
        uint32_t mantissa;
        if (e >= 10) {
            mantissa = (x >> (e - 10)) & 0x3FF;
        } else {
            mantissa = (x << (10 - e)) & 0x3FF;
        }

        // Interpolate between LOG2_LUT[mantissa] and the next entry using slope
        // (slope is precomputed delta to next entry in Q15)
        // Here we just use the LUT value directly; the LUT is dense enough.
        int16_t frac_log = LOG2_LUT[mantissa];      // log2 of normalized fractional, Q15

        // Final: log2(x) = e + frac_log_normalized
        // The result needs to fit in int16 (Q5.10 effectively, e is up to 30).
        // To keep room: store e in upper bits, fractional in lower.
        // But a simpler representation: result_q15 = (e * 32768) + frac_log
        // With e up to 30, that's > int16. We must scale.
        // Convention: log2(x) is in [0, 31). We use Q11 (5 integer + 11 fractional, range ±16).
        // result = (e << 11) + (frac_log >> 4)
        // Saturate at int16 range
        int32_t result_q11 = ((int32_t)e << 11) + ((int32_t)frac_log >> 4);
        if (result_q11 >  INT16_MAX) result_q11 = INT16_MAX;
        if (result_q11 < -INT16_MAX) result_q11 = -INT16_MAX;
        log_out[m] = (int16_t)result_q11;
    }
}
