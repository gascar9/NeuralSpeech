// ============================================================
// cnn.h — Inférence CNN Q11 embarquée pour Arduino Due
//
// API publique unique : cnn_infer().
// Tout le détail (couches Conv/Pool/Dense, ReLU, argmax) est interne.
// ============================================================
#pragma once
#include <stdint.h>

// IDs de classes (cohérents avec train_cnn.py et model_weights.h)
#ifndef CLASS_VRAI
#define CLASS_VRAI 0
#endif
#ifndef CLASS_FAUX
#define CLASS_FAUX 1
#endif

/**
 * Inférence du réseau sur une matrice MFCC.
 *
 * @param mfcc_raw  pointeur vers MFCC_FRAMES*MFCC_COEFS = 806 valeurs int16
 *                  (sortie brute de FP4, scale MFCC d'origine, PAS Q11)
 * @param logits    si non-NULL, écrit les 2 logits Q11 finaux (avant argmax).
 *                  Pratique pour estimer une "confiance" sur chip.
 * @return          CLASS_VRAI ou CLASS_FAUX
 *
 * Coût mémoire :
 *   buffers internes statiques  ~17.5 Ko BSS
 *   header poids (flash)        ~17 Ko
 * Coût temps :
 *   ~10 ms à 84 MHz (mesurer in-situ avec micros()).
 */
int cnn_infer(const int16_t* mfcc_raw, int16_t* logits);
