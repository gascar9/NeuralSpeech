/**
 * filter_coefs.h -- Coefficients RIF passe-bas anti-repliement (FP2, ET2)
 *
 * Source : http://t-filter.engineerjs.com (Parks-McClellan / Remez exchange)
 *
 * Gabarit de conception :
 *   - Fe            : 32000 Hz
 *   - Bande passante: 0 - 3200 Hz     gain = 1, ripple desire 0.5 dB
 *   - Bande coupee  : 4000 - 16000 Hz gain = 0, attenuation desiree >= 30 dB (ET2)
 *   - Precision     : 16 bits fixed-point (Q15 implicite, somme ~= 32768)
 *
 * Le filtre Parks-McClellan est "equiripple optimal" : pour un gabarit donne,
 * il donne le nombre de taps minimum. Ici 59 taps (contre 97 pour notre premier
 * design methode des fenetres Hamming) -- reduction de ~40% du cout CPU.
 *
 * Coefficients symetriques (phase lineaire garantie), centre a l'indice 29.
 *
 * NE PAS MODIFIER MANUELLEMENT -- regenerer via http://t-filter.engineerjs.com
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

/** Nombre total de coefficients (taps) du filtre RIF */
constexpr size_t FILTER_TAPS = 59U;

/**
 * Coefficients en Q15 (int16 signes).
 * Convolution : acc (int32) = sum( FILTER_COEFS_Q15[k] * x[n-k] )
 * Sortie signal : y = acc >> 15
 *
 * Dynamique : somme des |coefs| ~= 50000, acc_max ~= 50000 * 2048 = 1.02e8,
 * largement sous INT32_MAX = 2.15e9 (marge x20).
 */
constexpr int16_t FILTER_COEFS_Q15[FILTER_TAPS] = {
      189,    509,   -107,    -45,   -226,   -239,   -160,     30,
      238,    360,    314,     90,   -227,   -484,   -534,   -306,
      136,    603,    851,    694,    112,   -702,  -1381,  -1514,
     -807,    767,   2926,   5138,   6790,   7402,   6790,   5138,
     2926,    767,   -807,  -1514,  -1381,   -702,    112,    694,
      851,    603,    136,   -306,   -534,   -484,   -227,     90,
      314,    360,    238,     30,   -160,   -239,   -226,    -45,
     -107,    509,    189
};
