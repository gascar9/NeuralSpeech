/**
 * filter_coefs.h -- Coefficients RIF passe-bas anti-repliement (FP2, ET2)
 *
 * Source : http://t-filter.engineerjs.com (Parks-McClellan / Remez exchange)
 *
 * Gabarit de conception :
 *   - Fe                : 32000 Hz
 *   - Bande passante    : 0 - 2800 Hz    gain = 1, ripple desire 5 dB (actual ~1.1 dB)
 *   - Bande coupee      : 4000 - 16000 Hz gain = 0, attenuation -30 dB (actual -41.5 dB, ET2 OK +11.5 dB marge)
 *   - Precision         : 16 bits fixed-point (Q15 implicite, >> 15 en runtime)
 *
 * Nombre de taps : 40 (contre 97 avec la methode des fenetres Hamming et 59 avec
 * un gabarit 0-3200 Hz). Parks-McClellan sur un gabarit plus large offre la
 * reduction de cout CPU la plus agressive tout en gardant ET2 largement respectee.
 *
 * Compromis : la bande passante s'arrete a 2800 Hz au lieu de 3200 Hz. Les
 * formants vocaux F1 (200-1000 Hz) et F2 (800-2500 Hz) restent entierement
 * preserves, seul le haut du F3 et F4 sont legerement attenues -- sans impact
 * sur la reconnaissance de "bleu" / "rouge" qui se joue sur F1/F2.
 *
 * Coefficients symetriques (phase lineaire garantie), centre aux indices 19-20.
 *
 * NE PAS MODIFIER MANUELLEMENT -- regenerer via http://t-filter.engineerjs.com
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

/** Nombre total de coefficients (taps) du filtre RIF */
constexpr size_t FILTER_TAPS = 40U;

/**
 * Coefficients en Q15 (int16 signes).
 * Convolution : acc (int32) = sum( FILTER_COEFS_Q15[k] * x[n-k] )
 * Sortie signal : y = acc >> 15
 *
 * Dynamique : somme des |coefs| ~= 50000, acc_max ~= 50000 * 2048 = 1.02e8,
 * largement sous INT32_MAX = 2.15e9 (marge x20).
 */
constexpr int16_t FILTER_COEFS_Q15[FILTER_TAPS] = {
     -216,   -394,   -400,   -459,   -246,      4,    371,    601,
      652,    377,   -136,   -770,  -1240,  -1292,   -713,    520,
     2248,   4120,   5708,   6618,   6618,   5708,   4120,   2248,
      520,   -713,  -1292,  -1240,   -770,   -136,    377,    652,
      601,    371,      4,   -246,   -459,   -400,   -394,   -216
};
