#include "mfcc.h"
#include "preemphasis.h"
#include "hamming.h"
#include "fft.h"
#include "magnitude.h"
#include "mel_bank.h"
#include "log_lut.h"
#include "dct.h"
#include <string.h>

// Per-frame scratch buffers (~4 KB on stack — OK for SAM3X8E with 96 KB SRAM)
static int16_t fft_real[256];
static int16_t fft_imag[256];
static int32_t power[128];
static int32_t mel_energies[26];
static int16_t log_mel[26];

void compute_mfcc(int16_t* audio, int16_t mfcc_out[MFCC_FRAMES][MFCC_COEFS]) {
    // Stage 1: in-place preemphasis on the whole audio buffer
    preemphasis_q15(audio, MFCC_AUDIO_SAMPLES);

    // Stage 2-8: per-frame loop
    for (size_t f = 0; f < MFCC_FRAMES; f++) {
        size_t start = f * MFCC_HOP_SIZE;

        // Stage 2: extract frame with zero-padding for last frame
        int16_t frame_in[256];
        for (size_t n = 0; n < 256; n++) {
            size_t idx = start + n;
            frame_in[n] = (idx < MFCC_AUDIO_SAMPLES) ? audio[idx] : 0;
        }

        // Stage 3: Hamming
        int16_t windowed[256];
        hamming_q15(frame_in, windowed);

        // Stage 4: FFT (in-place on real/imag)
        memcpy(fft_real, windowed, sizeof(windowed));
        memset(fft_imag, 0, sizeof(fft_imag));
        fft_q15_radix2(fft_real, fft_imag);

        // Stage 5: |X[k]|^2
        magnitude_squared(fft_real, fft_imag, power);

        // Stage 6: MEL bank
        mel_filter_bank(power, mel_energies);

        // Stage 7: log2
        log2_q15(mel_energies, log_mel);

        // Stage 8: DCT-II
        dct_q15(log_mel, mfcc_out[f]);
    }
}
