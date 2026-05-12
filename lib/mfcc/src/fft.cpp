#include "fft.h"
#include "tables.h"

static inline int bit_reverse(int x, int log2n) {
    int r = 0;
    for (int i = 0; i < log2n; i++) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}

void fft_q15_radix2(int16_t* re, int16_t* im) {
    constexpr int LOG2_N = 8;  // log2(256)

    // 1. Bit-reversal permutation
    for (int i = 0; i < (int)FFT_N; i++) {
        int j = bit_reverse(i, LOG2_N);
        if (j > i) {
            int16_t tr = re[i]; re[i] = re[j]; re[j] = tr;
            int16_t ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    // 2. Butterfly stages
    for (int s = 1; s <= LOG2_N; s++) {
        int m       = 1 << s;          // length of DFT at this stage
        int m_half  = m >> 1;
        int tw_step = FFT_N / m;       // step within twiddle table

        for (int k = 0; k < (int)FFT_N; k += m) {
            for (int j = 0; j < m_half; j++) {
                int idx_w = j * tw_step;
                int16_t wr = TWIDDLE_COS[idx_w];
                int16_t wi = -TWIDDLE_SIN[idx_w];   // FFT uses negative sin

                int t_idx = k + j + m_half;
                int u_idx = k + j;

                int32_t tr = ((int32_t)wr * re[t_idx] - (int32_t)wi * im[t_idx]) >> 15;
                int32_t ti = ((int32_t)wr * im[t_idx] + (int32_t)wi * re[t_idx]) >> 15;

                int32_t ur = re[u_idx];
                int32_t ui = im[u_idx];

                // Scale by 1/2 each stage to avoid overflow
                re[u_idx]  = (int16_t)((ur + tr) >> 1);
                im[u_idx]  = (int16_t)((ui + ti) >> 1);
                re[t_idx]  = (int16_t)((ur - tr) >> 1);
                im[t_idx]  = (int16_t)((ui - ti) >> 1);
            }
        }
    }
}
