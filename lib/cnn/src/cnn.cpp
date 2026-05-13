// ============================================================
// cnn.cpp — Inférence Q11 (miroir bit-conforme de scripts/cnn_inference_q11.py)
// ============================================================
#include "cnn.h"
#include "model_weights.h"

#include <stdint.h>

// --- Constantes locales -----------------------------------------------------
#define Q11_BITS_LOC   11
#define INT16_MAX_C    32767
#define INT16_MIN_C   (-32768)

#define SAT_I16(v)  ((v) > INT16_MAX_C ? (int16_t)INT16_MAX_C \
                   : ((v) < INT16_MIN_C ? (int16_t)INT16_MIN_C : (int16_t)(v)))

#define RELU_I32(v) ((v) < 0 ? 0 : (v))

// --- Indexation des tenseurs 3D contigus -----------------------------------
// Activations : layout [H][W][C] (Keras channels_last)
static inline int idx_hwc(int h, int w, int c, int W, int C) {
    return h * W * C + w * C + c;
}

// Conv kernels : layout [C_out][kh][kw][C_in] (réorganisé par export_weights.py)
static inline int idx_kernel(int co, int kh, int kw, int ci,
                              int KH, int KW, int C_in) {
    return co * (KH * KW * C_in) + kh * (KW * C_in) + kw * C_in + ci;
}

// Dense weights : layout [in][out] (Keras row-major)
static inline int idx_dense(int i, int o, int D_out) {
    return i * D_out + o;
}

// --- Buffers statiques (BSS, alloués une fois pour toutes) -----------------
// Total : 1612 + 10560 + 2400 + 2688 + 448 + 64 = 17 772 octets
static int16_t buf_norm[MFCC_FRAMES * MFCC_COEFS];                          // 1612
static int16_t buf_conv1[CONV1_OUT_H * CONV1_OUT_W * CONV1_FILTERS];        // 10560
static int16_t buf_pool1[POOL1_OUT_H * POOL1_OUT_W * CONV1_FILTERS];        // 2400
static int16_t buf_conv2[CONV2_OUT_H * CONV2_OUT_W * CONV2_FILTERS];        // 2688
static int16_t buf_pool2[POOL2_OUT_H * POOL2_OUT_W * CONV2_FILTERS];        // 448
static int16_t buf_dense1[DENSE1_UNITS];                                    // 64

// ============================================================================
// 1. Normalisation z-score Q11
//    out[f][c] = saturate( ((mfcc_raw - MEAN[c]) * INV_STD_Q24[c]) >> 13 )
// ============================================================================
static void normalize_q11(const int16_t* mfcc_raw, int16_t* out) {
    for (int f = 0; f < MFCC_FRAMES; f++) {
        for (int c = 0; c < MFCC_COEFS; c++) {
            int32_t diff = (int32_t)mfcc_raw[f * MFCC_COEFS + c] - (int32_t)NORM_MEAN[c];
            // INV_STD_Q24 peut atteindre ~1200, diff peut atteindre ±60000
            // Le produit dépasse int32 → on passe par int64.
            int64_t scaled = (int64_t)diff * (int64_t)INV_STD_Q24[c];
            int32_t r = (int32_t)(scaled >> 13);  // Q24 -> Q11
            out[f * MFCC_COEFS + c] = SAT_I16(r);
        }
    }
}

// ============================================================================
// 2. Conv2D 3×3 + ReLU + saturation int16  (générique, paramétré)
//    in  : (H_in, W_in, C_in)        Q11
//    w   : (C_out, 3, 3, C_in)       Q11
//    b   : (C_out)                   Q11
//    out : (H_in-2, W_in-2, C_out)   Q11
// ============================================================================
static void conv2d_3x3_relu(const int16_t* in,  int H_in, int W_in, int C_in,
                            int16_t* out,
                            const int16_t* w, const int16_t* b, int C_out) {
    const int H_out = H_in - 2;
    const int W_out = W_in - 2;
    for (int h = 0; h < H_out; h++) {
        for (int wcol = 0; wcol < W_out; wcol++) {
            for (int co = 0; co < C_out; co++) {
                // bias Q11 décalé à Q22 pour matcher l'échelle des produits MAC
                int32_t acc = (int32_t)b[co] << Q11_BITS_LOC;
                for (int kh = 0; kh < 3; kh++) {
                    for (int kw = 0; kw < 3; kw++) {
                        for (int ci = 0; ci < C_in; ci++) {
                            int32_t xv = (int32_t)in[idx_hwc(h + kh, wcol + kw, ci, W_in, C_in)];
                            int32_t kv = (int32_t)w[idx_kernel(co, kh, kw, ci, 3, 3, C_in)];
                            acc += kv * xv;
                        }
                    }
                }
                acc >>= Q11_BITS_LOC;          // Q22 → Q11
                acc = RELU_I32(acc);
                out[idx_hwc(h, wcol, co, W_out, C_out)] = SAT_I16(acc);
            }
        }
    }
}

// ============================================================================
// 3. MaxPool 2×2 stride 2 (paramétré)
//    in  : (H_in, W_in, C)
//    out : (H_in/2, W_in/2, C)
// ============================================================================
static void maxpool_2x2(const int16_t* in, int H_in, int W_in, int C,
                        int16_t* out) {
    const int H_out = H_in / 2;
    const int W_out = W_in / 2;
    for (int h = 0; h < H_out; h++) {
        for (int wcol = 0; wcol < W_out; wcol++) {
            for (int c = 0; c < C; c++) {
                int16_t a = in[idx_hwc(2*h,     2*wcol,     c, W_in, C)];
                int16_t b = in[idx_hwc(2*h + 1, 2*wcol,     c, W_in, C)];
                int16_t cc = in[idx_hwc(2*h,    2*wcol + 1, c, W_in, C)];
                int16_t d = in[idx_hwc(2*h + 1, 2*wcol + 1, c, W_in, C)];
                int16_t m = a;
                if (b > m) m = b;
                if (cc > m) m = cc;
                if (d > m) m = d;
                out[idx_hwc(h, wcol, c, W_out, C)] = m;
            }
        }
    }
}

// ============================================================================
// 4. Dense + ReLU optionnel
// ============================================================================
static void dense(const int16_t* in, int D_in,
                  int16_t* out, int D_out,
                  const int16_t* w, const int16_t* b,
                  bool with_relu) {
    for (int o = 0; o < D_out; o++) {
        // int64 par sécurité : Dense1 a 224 inputs, pas de marge int32
        int64_t acc = (int64_t)b[o] << Q11_BITS_LOC;
        for (int i = 0; i < D_in; i++) {
            acc += (int64_t)w[idx_dense(i, o, D_out)] * (int64_t)in[i];
        }
        acc >>= Q11_BITS_LOC;
        if (with_relu && acc < 0) acc = 0;
        if (acc > INT16_MAX_C) acc = INT16_MAX_C;
        else if (acc < INT16_MIN_C) acc = INT16_MIN_C;
        out[o] = (int16_t)acc;
    }
}

// ============================================================================
// 5. Orchestrateur — séquence complète des couches
// ============================================================================
int cnn_infer(const int16_t* mfcc_raw, int16_t* logits) {
    // 1. Normalisation
    normalize_q11(mfcc_raw, buf_norm);

    // 2. Conv1 3×3, 1 → 8 canaux  : (62,13,1) → (60,11,8)
    conv2d_3x3_relu(buf_norm, MFCC_FRAMES, MFCC_COEFS, 1,
                    buf_conv1,
                    CONV1_W, CONV1_B, CONV1_FILTERS);

    // 3. MaxPool 2×2                : (60,11,8) → (30,5,8)
    maxpool_2x2(buf_conv1, CONV1_OUT_H, CONV1_OUT_W, CONV1_FILTERS,
                buf_pool1);

    // 4. Conv2 3×3, 8 → 16 canaux  : (30,5,8)  → (28,3,16)
    conv2d_3x3_relu(buf_pool1, POOL1_OUT_H, POOL1_OUT_W, CONV1_FILTERS,
                    buf_conv2,
                    CONV2_W, CONV2_B, CONV2_FILTERS);

    // 5. MaxPool 2×2                : (28,3,16) → (14,1,16)
    maxpool_2x2(buf_conv2, CONV2_OUT_H, CONV2_OUT_W, CONV2_FILTERS,
                buf_pool2);

    // 6. Flatten implicite : buf_pool2 est déjà contigü en mémoire (14*1*16=224)

    // 7. Dense1 224 → 32 + ReLU
    dense(buf_pool2, FLATTEN_SIZE,
          buf_dense1, DENSE1_UNITS,
          DENSE1_W, DENSE1_B, /*with_relu=*/true);

    // 8. Dense2 32 → 2 (logits, pas de softmax)
    int16_t logits_local[DENSE2_UNITS];
    dense(buf_dense1, DENSE1_UNITS,
          logits_local, DENSE2_UNITS,
          DENSE2_W, DENSE2_B, /*with_relu=*/false);

    if (logits) {
        logits[0] = logits_local[0];
        logits[1] = logits_local[1];
    }

    // 9. argmax (équivalent argmax(softmax(.)))
    return (logits_local[CLASS_VRAI] >= logits_local[CLASS_FAUX])
         ? CLASS_VRAI : CLASS_FAUX;
}
