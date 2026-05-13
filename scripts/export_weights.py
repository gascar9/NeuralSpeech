#!/usr/bin/env python3
"""
FP6 — Export des poids Q15/Q11 du CNN entraîné vers un header C.

Lit :
  models/cnn_vrai_faux.keras       (modèle Keras entraîné par train_cnn.py)
  models/normalization_params.npz  (mean + std par coef MFCC)

Écrit :
  include/model_weights.h          (tableaux int16/int32 en Q11/Q24)

Conventions :
  - MFCC en entrée : int16 (scale MFCC brut, dynamique ±32767)
  - Normalisation embarquée :
      x_norm_q11 = ((mfcc_raw - mean_int16) * inv_std_q24) >> 13
      → résultat en Q11 dans la dynamique typique z-score [-3, +3]
  - Poids CNN : Q11 (1 signe + 4 entier + 11 fractionnaire), range ±15.999
  - Inférence : accumulateur int32, sortie de couche réajustée en Q11
                par >> 11 (multiplication de deux Q11 donne Q22).

Pourquoi Q11 et pas Q15 ?
  Q15 ne couvre que ±0.9999 → on saturerait dès qu'un poids dépasse 1, ou
  qu'un accumulateur retombe dans une valeur > 1 après ReLU. Q11 donne
  un confort de ±16 sans perdre trop de précision (résolution 1/2048 ≈ 5e-4).
"""
from pathlib import Path
import numpy as np
import tensorflow as tf


Q_BITS         = 11        # Q11 : 11 bits fractionnaires
Q_SCALE        = 1 << Q_BITS  # 2048
INV_STD_Q_BITS = 24        # Q24 pour 1/std (qui est très petit)

PROJECT_ROOT = Path(__file__).parent.parent
MODEL_PATH   = PROJECT_ROOT / "models" / "cnn_vrai_faux.keras"
NORM_PATH    = PROJECT_ROOT / "models" / "normalization_params.npz"
OUT_PATH     = PROJECT_ROOT / "lib" / "cnn" / "src" / "model_weights.h"


def to_q11(x_float):
    """Quantize a float array to Q11 int16.

    Sature à ±32767 (donc ±~16.0). Tout poids hors de cette plage est clipé,
    on imprime un warning si ça arrive.
    """
    x_q = np.round(np.asarray(x_float, dtype=np.float64) * Q_SCALE)
    saturated = int(np.sum((x_q > 32767) | (x_q < -32768)))
    if saturated > 0:
        print(f"      ⚠ {saturated} valeurs saturées en Q11 (max abs original = "
              f"{np.abs(x_float).max():.3f}, max Q11 = ±15.999)")
    x_q = np.clip(x_q, -32768, 32767).astype(np.int16)
    return x_q


def write_array_int16(fp, name, arr, comment=""):
    fp.write(f"// {name}: shape={list(arr.shape)}, total={arr.size}\n")
    if comment:
        fp.write(f"//   {comment}\n")
    fp.write(f"static const int16_t {name}[{arr.size}] = {{\n")
    flat = arr.flatten()
    for i in range(0, len(flat), 12):
        chunk = flat[i:i + 12]
        fp.write("    " + ", ".join(f"{int(x):>6d}" for x in chunk) + ",\n")
    fp.write("};\n\n")


def write_array_int32(fp, name, arr, comment=""):
    fp.write(f"// {name}: shape={list(arr.shape)}, total={arr.size}\n")
    if comment:
        fp.write(f"//   {comment}\n")
    fp.write(f"static const int32_t {name}[{arr.size}] = {{\n")
    flat = arr.flatten()
    for i in range(0, len(flat), 6):
        chunk = flat[i:i + 6]
        fp.write("    " + ", ".join(f"{int(x):>12d}" for x in chunk) + ",\n")
    fp.write("};\n\n")


def main():
    print("=" * 70)
    print("  FP6 — Export poids CNN Q11 vers C header")
    print("=" * 70)

    # === 1. Charge le modèle ===
    print(f"\n[1/4] Charge modèle Keras : {MODEL_PATH.name}")
    model = tf.keras.models.load_model(MODEL_PATH)
    norm = np.load(NORM_PATH)
    mean = norm["mean"].astype(np.float32)   # shape (13,)
    std  = norm["std"].astype(np.float32)    # shape (13,)

    print(f"      Mean MFCC (par coef) : min={mean.min():.1f}, max={mean.max():.1f}")
    print(f"      Std  MFCC (par coef) : min={std.min():.1f}, max={std.max():.1f}")

    # === 2. Inspecte couches ===
    print(f"\n[2/4] Layers :")
    conv1_w, conv1_b = model.get_layer("conv1").get_weights()
    conv2_w, conv2_b = model.get_layer("conv2").get_weights()
    dense1_w, dense1_b = model.get_layer("dense1").get_weights()
    dense2_w, dense2_b = model.get_layer("output").get_weights()

    layers = [
        ("conv1_w",  conv1_w),  ("conv1_b",  conv1_b),
        ("conv2_w",  conv2_w),  ("conv2_b",  conv2_b),
        ("dense1_w", dense1_w), ("dense1_b", dense1_b),
        ("dense2_w", dense2_w), ("dense2_b", dense2_b),
    ]
    total_params = 0
    for name, w in layers:
        total_params += w.size
        print(f"      {name:>10s}  shape={str(list(w.shape)):>18s}  "
              f"min={w.min():+7.3f}  max={w.max():+7.3f}  size={w.size}")
    print(f"      Total params : {total_params}")

    # === 3. Quantification ===
    print(f"\n[3/4] Quantification Q11 (scale={Q_SCALE}) :")

    # Mean : valeur brute en scale MFCC d'origine — pas Q11
    # Inv std : 1/std stocké en Q24 (parce que std grand → inv_std petit)
    mean_int16 = np.clip(np.round(mean), -32768, 32767).astype(np.int16)
    inv_std    = 1.0 / std
    inv_std_q24 = np.round(inv_std * (1 << INV_STD_Q_BITS)).astype(np.int64)
    inv_std_q24 = np.clip(inv_std_q24, -2_147_483_648, 2_147_483_647).astype(np.int32)
    print(f"      mean stocké en int16 (raw MFCC scale)")
    print(f"      inv_std stocké en Q24 (max = {inv_std_q24.max()})")

    print(f"      Conversion Q11 des poids :")
    for name, w in [("conv1_w", conv1_w), ("conv1_b", conv1_b),
                    ("conv2_w", conv2_w), ("conv2_b", conv2_b),
                    ("dense1_w", dense1_w), ("dense1_b", dense1_b),
                    ("dense2_w", dense2_w), ("dense2_b", dense2_b)]:
        print(f"      • {name:>10s}", end="  ")
        w_q = to_q11(w)
        w_recovered = w_q.astype(np.float32) / Q_SCALE
        err_max = float(np.abs(w - w_recovered).max())
        err_mean = float(np.abs(w - w_recovered).mean())
        print(f"err_max={err_max:.5f}  err_mean={err_mean:.6f}")

    # === 4. Écriture du header ===
    print(f"\n[4/4] Écriture {OUT_PATH}")
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)

    # Re-organisation des kernels Keras (kh, kw, C_in, C_out) → (C_out, kh, kw, C_in)
    # pour l'inférence : on itère C_out en boucle externe, c'est plus efficace.
    conv1_w_reorg = np.transpose(conv1_w, (3, 0, 1, 2))   # (8, 3, 3, 1)
    conv2_w_reorg = np.transpose(conv2_w, (3, 0, 1, 2))   # (16, 3, 3, 8)

    with open(OUT_PATH, "w") as fp:
        fp.write("// ============================================================\n")
        fp.write("// model_weights.h — CNN vrai/faux quantifié Q11 pour Arduino Due\n")
        fp.write("// AUTO-GÉNÉRÉ par scripts/export_weights.py — NE PAS MODIFIER\n")
        fp.write("//\n")
        fp.write("// Source : models/cnn_vrai_faux.keras\n")
        fp.write("// Format poids : Q11 (1 signe + 4 entier + 11 fractionnaire)\n")
        fp.write("// Range  : ±15.9995, résolution 1/2048 ≈ 4.88e-4\n")
        fp.write("//\n")
        fp.write("// Topologie :\n")
        fp.write("//   Input  62×13×1  (MFCC normalisé z-score, Q11)\n")
        fp.write("//   Conv1  3×3, 8 filtres,  ReLU            -> 60×11×8\n")
        fp.write("//   MaxPool 2×2                              -> 30×5×8\n")
        fp.write("//   Conv2  3×3, 16 filtres, ReLU            -> 28×3×16\n")
        fp.write("//   MaxPool 2×2                              -> 14×1×16\n")
        fp.write("//   Flatten                                  -> 224\n")
        fp.write("//   Dense1 ReLU                              -> 32\n")
        fp.write("//   Dense2 (output)                          -> 2 (logits)\n")
        fp.write("//   argmax (pas de softmax sur chip)\n")
        fp.write("// ============================================================\n\n")
        fp.write("#pragma once\n")
        fp.write("#include <stdint.h>\n\n")

        fp.write("// --- Constantes pipeline ---\n")
        fp.write("#define MFCC_FRAMES   62\n")
        fp.write("#define MFCC_COEFS    13\n")
        fp.write("#define Q11_BITS      11\n")
        fp.write("#define Q11_SCALE     2048\n")
        fp.write("#define Q24_BITS      24\n")
        fp.write("#define Q24_SCALE     16777216\n\n")

        fp.write("// --- Topologie ---\n")
        fp.write("#define CONV1_FILTERS    8\n")
        fp.write("#define CONV1_KH         3\n")
        fp.write("#define CONV1_KW         3\n")
        fp.write("#define CONV1_C_IN       1\n")
        fp.write("#define CONV1_OUT_H     60\n")
        fp.write("#define CONV1_OUT_W     11\n\n")
        fp.write("#define POOL1_OUT_H     30\n")
        fp.write("#define POOL1_OUT_W      5\n\n")
        fp.write("#define CONV2_FILTERS   16\n")
        fp.write("#define CONV2_KH         3\n")
        fp.write("#define CONV2_KW         3\n")
        fp.write("#define CONV2_C_IN       8\n")
        fp.write("#define CONV2_OUT_H     28\n")
        fp.write("#define CONV2_OUT_W      3\n\n")
        fp.write("#define POOL2_OUT_H     14\n")
        fp.write("#define POOL2_OUT_W      1\n\n")
        fp.write("#define FLATTEN_SIZE   224\n")
        fp.write("#define DENSE1_UNITS    32\n")
        fp.write("#define DENSE2_UNITS     2\n\n")

        fp.write("// --- Normalisation z-score (par coefficient MFCC) ---\n")
        fp.write("// x_norm_q11 = ((mfcc_raw - NORM_MEAN[c]) * INV_STD_Q24[c]) >> 13\n")
        write_array_int16(fp, "NORM_MEAN", mean_int16,
                          "moyenne par coef (scale MFCC brut, PAS Q11)")
        write_array_int32(fp, "INV_STD_Q24", inv_std_q24,
                          "1/std en Q24 → multiplier puis >> 13 pour obtenir Q11")

        fp.write("// --- Poids CNN (Q11 int16) ---\n\n")

        fp.write("// CONV1 — 8 filtres 3×3 sur 1 canal d'entrée\n")
        fp.write("// Layout : [C_out=8][kh=3][kw=3][C_in=1] = 72 valeurs\n")
        write_array_int16(fp, "CONV1_W", to_q11(conv1_w_reorg))
        write_array_int16(fp, "CONV1_B", to_q11(conv1_b))

        fp.write("// CONV2 — 16 filtres 3×3 sur 8 canaux d'entrée\n")
        fp.write("// Layout : [C_out=16][kh=3][kw=3][C_in=8] = 1152 valeurs\n")
        write_array_int16(fp, "CONV2_W", to_q11(conv2_w_reorg))
        write_array_int16(fp, "CONV2_B", to_q11(conv2_b))

        fp.write("// DENSE1 — fully connected 224 → 32\n")
        fp.write("// Layout : [in=224][out=32] = 7168 valeurs (Keras row-major)\n")
        write_array_int16(fp, "DENSE1_W", to_q11(dense1_w))
        write_array_int16(fp, "DENSE1_B", to_q11(dense1_b))

        fp.write("// DENSE2 — fully connected 32 → 2 (logits vrai/faux)\n")
        fp.write("// Layout : [in=32][out=2] = 64 valeurs\n")
        write_array_int16(fp, "DENSE2_W", to_q11(dense2_w))
        write_array_int16(fp, "DENSE2_B", to_q11(dense2_b))

        fp.write("// Class IDs (cohérent avec train_cnn.py)\n")
        fp.write("#define CLASS_VRAI    0\n")
        fp.write("#define CLASS_FAUX    1\n")
        fp.write("\n// EOF\n")

    size_kb = OUT_PATH.stat().st_size / 1024
    print(f"\n      ✓ {OUT_PATH.relative_to(PROJECT_ROOT)}  ({size_kb:.1f} KB)")

    # Récapitulatif mémoire flash attendu
    total_int16 = 13 + sum(a.size for a in (conv1_w, conv1_b, conv2_w, conv2_b,
                                            dense1_w, dense1_b, dense2_w, dense2_b))
    total_int32 = 13
    flash_kb = (total_int16 * 2 + total_int32 * 4) / 1024
    print(f"\n  Flash consommée (poids+norm) : "
          f"{total_int16} int16 + {total_int32} int32 = {flash_kb:.1f} KB")
    print(f"  Sur 512 KB flash Due : {flash_kb / 512 * 100:.1f} % utilisée")

    print("\n" + "=" * 70)
    print("  Export terminé.")
    print("=" * 70)


if __name__ == "__main__":
    main()
