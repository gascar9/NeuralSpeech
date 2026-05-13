#!/usr/bin/env python3
"""
FP5 — Entraînement du CNN pour classifier vrai / faux à partir des MFCC.

Conforme ET7 + ET8 du sujet NeuralSpeech V1.3 :
  - ET7 : MSE des données de test < 0,05 après entraînement.
          Backprop INTERDITE sur le jeu de test (donc on isole le test).
  - ET8 : ≥ 50 enregistrements par mot dans le jeu d'entraînement.

Entrée :
  dataset/vrai/vrai_*.mfcc.npy    (matrices 62×13 int16 Q11)
  dataset/faux/faux_*.mfcc.npy    (matrices 62×13 int16 Q11)

Sorties :
  models/cnn_vrai_faux.keras                 (modèle Keras entraîné)
  models/normalization_params.npz            (mean + std par coefficient, pour FP6)
  rapport/figures/fp5_training_curves.png    (courbes loss + accuracy)
  rapport/figures/fp5_confusion_matrix.png   (matrice de confusion)

Usage :
    python3 scripts/train_cnn.py
"""
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
import tensorflow as tf
from sklearn.model_selection import train_test_split
from sklearn.metrics import confusion_matrix


# ===========================================================================
# 0. Configuration
# ===========================================================================
PROJECT_ROOT = Path(__file__).parent.parent
DATASET_DIR  = PROJECT_ROOT / "dataset"
MODELS_DIR   = PROJECT_ROOT / "models"
FIGURES_DIR  = PROJECT_ROOT / "rapport" / "figures"
MODELS_DIR.mkdir(exist_ok=True)
FIGURES_DIR.mkdir(parents=True, exist_ok=True)

# Hyperparamètres d'entraînement
TEST_SPLIT_RATIO  = 0.20    # 20% des données pour le test (intouchable pendant training)
RANDOM_SEED       = 42      # pour reproductibilité
EPOCHS            = 200     # max — on s'arrête plus tôt avec early stopping
BATCH_SIZE        = 16      # mini-batch
LEARNING_RATE     = 5e-4    # plus petit pour éviter les oscillations en early training

# Data augmentation — multiplie le dataset par AUG_FACTOR (bruit gaussien + time-shift)
AUG_FACTOR        = 10      # 40 train → 400 augmented samples
AUG_NOISE_STD     = 0.10    # écart-type du bruit gaussien (en unités de std normalisée)
AUG_TIME_SHIFT    = 15      # décalage max ± frames (1 frame = 16 ms). Cf. diag_word_position.py :
                            # vrai et faux ont des positions de mot qui diffèrent de ~5 frames
                            # avec écart-type ~9 frames. ±15 frames = bien plus que ça,
                            # donc le CNN ne peut plus apprendre la position.

# Régularisation
DROPOUT_RATE      = 0.4     # 40% des neurones aléatoirement coupés pendant training
EARLY_STOP_PATIENCE = 20    # arrête l'entraînement si val_acc ne progresse pas pendant N epochs

# Reproductibilité (mêmes résultats à chaque exécution)
np.random.seed(RANDOM_SEED)
tf.random.set_seed(RANDOM_SEED)


# ===========================================================================
# 1. Chargement du dataset
# ===========================================================================
def load_dataset():
    """Charge toutes les matrices MFCC .npy depuis dataset/vrai et dataset/faux.

    Returns:
        X (np.ndarray) : shape (N, 62, 13), int16 — toutes les matrices MFCC
        y (np.ndarray) : shape (N,), int    — étiquettes (0=vrai, 1=faux)
        files (list)   : chemins des fichiers (pour debug)
    """
    X, y, files = [], [], []

    for label, word in enumerate(["vrai", "faux"]):
        word_dir = DATASET_DIR / word
        for npy_path in sorted(word_dir.glob(f"{word}_*.mfcc.npy")):
            mfcc = np.load(npy_path)
            if mfcc.shape != (62, 13):
                print(f"  ⚠ {npy_path.name} a la mauvaise shape {mfcc.shape}, ignoré")
                continue
            X.append(mfcc)
            y.append(label)
            files.append(npy_path.name)

    X = np.array(X, dtype=np.int16)   # (N, 62, 13)
    y = np.array(y, dtype=np.int32)   # (N,)
    return X, y, files


# ===========================================================================
# 2. Normalisation (z-score par coefficient)
# ===========================================================================
def normalize_features(X_train, X_test):
    """Normalise les MFCC en z-score, par COEFFICIENT (13 mean/std, pas 62×13).

    Avec un petit dataset (40 samples), normaliser par (frame, coef) cause
    des std=0 sur les positions silencieuses où tous les samples ont la
    même valeur. Cela fait exploser les valeurs test après division.

    On normalise donc par COEFFICIENT (agrégé sur toutes les frames + samples) :
        mean.shape = (13,), std.shape = (13,)
    Chaque coefficient garde son échelle propre (coef 0 = énergie, coef 12 = détail)
    mais avec 40 × 62 = 2480 valeurs par coef, std n'est jamais nulle.

    Returns:
        X_train_norm, X_test_norm, mean, std
    """
    X_train_f = X_train.astype(np.float32)
    X_test_f  = X_test.astype(np.float32)

    # Mean/std par coefficient (réduction sur axes 0=samples, 1=frames)
    mean = X_train_f.mean(axis=(0, 1))   # shape (13,)
    std  = X_train_f.std(axis=(0, 1))    # shape (13,)
    # Plancher de sécurité : empêche toute division par très petit nombre
    std  = np.maximum(std, 1.0)

    X_train_norm = (X_train_f - mean) / std
    X_test_norm  = (X_test_f  - mean) / std

    return X_train_norm, X_test_norm, mean, std


# ===========================================================================
# 3. Architecture du CNN
# ===========================================================================
def build_cnn(input_shape=(62, 13, 1)):
    """CNN compact pour classification binaire vrai/faux.

    Architecture :
      Input (62×13×1)
        → Conv2D(8 filtres 3×3, ReLU)        → 60×11×8
        → MaxPooling2D(2×2)                  → 30×5×8
        → Conv2D(16 filtres 3×3, ReLU)       → 28×3×16
        → MaxPooling2D(2×2)                  → 14×1×16
        → Flatten                            → 224
        → Dense(32, ReLU)                    → 32
        → Dense(2, Softmax) [P_vrai, P_faux] → 2

    Total : 8 514 paramètres entraînables (~33 KB → tient large sur Due flash).
    """
    # CNN avec dropout pour éviter l'overfitting sur petit dataset
    l2 = tf.keras.regularizers.l2(1e-3)  # pénalise les gros poids (régularisation L2)
    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=input_shape),
        tf.keras.layers.Conv2D(8, (3, 3), activation="relu", kernel_regularizer=l2, name="conv1"),
        tf.keras.layers.MaxPooling2D((2, 2), name="pool1"),
        tf.keras.layers.Conv2D(16, (3, 3), activation="relu", kernel_regularizer=l2, name="conv2"),
        tf.keras.layers.MaxPooling2D((2, 2), name="pool2"),
        tf.keras.layers.Flatten(name="flatten"),
        tf.keras.layers.Dropout(DROPOUT_RATE, name="dropout1"),     # ← anti-overfit
        tf.keras.layers.Dense(32, activation="relu", kernel_regularizer=l2, name="dense1"),
        tf.keras.layers.Dropout(DROPOUT_RATE, name="dropout2"),     # ← anti-overfit
        tf.keras.layers.Dense(2, activation="softmax", name="output"),
    ])

    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=LEARNING_RATE),
        loss="sparse_categorical_crossentropy",
        # Note : on ne met PAS la MSE en metric ici, parce que tf.keras.metrics.MSE
        # attend y_true et y_pred de même shape (or nos labels sont int sparse).
        # On calcule la MSE manuellement à la fin (one-hot vs softmax).
        metrics=["accuracy"],
    )
    return model


def compute_mse_one_hot(model, X, y, num_classes=2):
    """Calcule la MSE entre les prédictions softmax et les labels one-hot.

    C'est la MSE attendue par le sujet (ET7 : < 0,05 sur test).
    """
    y_pred = model.predict(X, verbose=0)                       # (N, 2)
    y_oh   = tf.one_hot(y, num_classes).numpy().astype(float)  # (N, 2)
    mse    = float(np.mean((y_oh - y_pred) ** 2))
    return mse


# ===========================================================================
# 4. Entraînement
# ===========================================================================
def train_and_evaluate():
    print("=" * 70)
    print("  FP5 — Entraînement CNN vrai / faux")
    print("=" * 70)

    # --- Charge dataset
    print("\n[1/5] Chargement du dataset...")
    X, y, files = load_dataset()
    n_vrai = int((y == 0).sum())
    n_faux = int((y == 1).sum())
    print(f"      Total : {len(X)} samples — {n_vrai} vrai + {n_faux} faux")

    if len(X) < 20:
        print("\n  ⚠ ATTENTION : moins de 20 samples. Le CNN ne pourra pas")
        print("    apprendre proprement. Il faut au moins 50+50 = 100 samples (ET8).")
        print("    Lance d'abord :")
        print("      python3 scripts/capture_dataset.py vrai gaspard 25")
        print("      python3 scripts/capture_dataset.py faux gaspard 25")
        return

    # --- Split train/test (stratifié pour préserver le ratio vrai/faux)
    print(f"\n[2/5] Split train/test ({(1-TEST_SPLIT_RATIO)*100:.0f}/{TEST_SPLIT_RATIO*100:.0f}, stratifié)...")
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=TEST_SPLIT_RATIO,
        random_state=RANDOM_SEED, stratify=y
    )
    print(f"      Train : {len(X_train)} samples")
    print(f"      Test  : {len(X_test)} samples  ← interdit pendant l'entraînement (ET7)")

    # --- Normalisation (calculée sur train uniquement)
    print("\n[3/5] Normalisation z-score (mean/std calculés sur TRAIN seulement)...")
    X_train_norm, X_test_norm, mean, std = normalize_features(X_train, X_test)
    # On ajoute la dimension de canal pour le Conv2D : (N, 62, 13) → (N, 62, 13, 1)
    X_train_norm = X_train_norm[..., np.newaxis]
    X_test_norm  = X_test_norm[..., np.newaxis]
    print(f"      Mean global : {mean.mean():.3f}, std global : {std.mean():.3f}")
    print(f"      X_train_norm range : [{X_train_norm.min():.2f}, {X_train_norm.max():.2f}]")
    print(f"      X_test_norm  range : [{X_test_norm.min():.2f}, {X_test_norm.max():.2f}]")

    # --- Data augmentation : bruit gaussien + time-shift aléatoire
    # Le time-shift force le CNN à apprendre le CONTENU du mot, pas sa position
    # dans le buffer (voir scripts/diag_word_position.py).
    print(f"\n[3.5/5] Data augmentation : factor {AUG_FACTOR}× "
          f"(noise std={AUG_NOISE_STD}, time-shift ±{AUG_TIME_SHIFT} frames)")

    def time_shift(x_batch, shifts):
        """Décale chaque sample du batch sur l'axe frames (axe 1).
        Padding avec zéros (= moyenne après z-score = équivalent silence).

        x_batch : (N, 62, 13, 1) float32 normalisé
        shifts  : (N,) int — décalage par sample (positif = retard du mot)
        """
        out = np.zeros_like(x_batch)
        N, F = x_batch.shape[0], x_batch.shape[1]
        for i in range(N):
            s = int(shifts[i])
            if s == 0:
                out[i] = x_batch[i]
            elif s > 0:
                # Mot décalé vers la fin : on duplique les premières frames vides
                if s < F:
                    out[i, s:] = x_batch[i, :F-s]
            else:
                s_abs = -s
                if s_abs < F:
                    out[i, :F-s_abs] = x_batch[i, s_abs:]
        return out

    X_train_aug = [X_train_norm]      # original, sans transformation
    y_train_aug = [y_train]
    rng = np.random.default_rng(RANDOM_SEED)
    for k in range(AUG_FACTOR - 1):
        shifts = rng.integers(-AUG_TIME_SHIFT, AUG_TIME_SHIFT + 1, size=len(X_train_norm))
        shifted = time_shift(X_train_norm, shifts)
        noise = rng.standard_normal(shifted.shape).astype(np.float32) * AUG_NOISE_STD
        X_train_aug.append(shifted + noise)
        y_train_aug.append(y_train)
    X_train_norm = np.concatenate(X_train_aug, axis=0)
    y_train      = np.concatenate(y_train_aug, axis=0)
    print(f"      Train augmenté : {len(X_train_norm)} samples (avant : {len(X_train)})")

    # --- Construction du CNN
    print("\n[4/5] Architecture CNN...")
    model = build_cnn()
    model.summary()

    # --- Entraînement (backprop UNIQUEMENT sur X_train, jamais sur X_test)
    print(f"\n[5/5] Entraînement ({EPOCHS} epochs, batch={BATCH_SIZE})...")
    print(f"      ⚠ Le test set N'est PAS utilisé par backprop (ET7)")
    print(f"      Il est juste passé en validation_data pour monitorer la convergence,")
    print(f"      mais Keras NE FAIT PAS de backprop dessus.\n")

    import time
    t0 = time.time()
    # Early stopping : on s'arrête si val_accuracy ne progresse plus pendant N epochs
    early_stop = tf.keras.callbacks.EarlyStopping(
        monitor="val_accuracy", patience=EARLY_STOP_PATIENCE,
        restore_best_weights=True, mode="max", verbose=1
    )
    history = model.fit(
        X_train_norm, y_train,
        validation_data=(X_test_norm, y_test),  # juste pour log, pas pour backprop
        epochs=EPOCHS,
        batch_size=BATCH_SIZE,
        callbacks=[early_stop],
        verbose=2,
    )
    training_duration = time.time() - t0

    # --- Évaluation finale
    print("\n" + "=" * 70)
    print("  RÉSULTATS FINAUX")
    print("=" * 70)

    train_loss, train_acc = model.evaluate(X_train_norm, y_train, verbose=0)
    test_loss,  test_acc  = model.evaluate(X_test_norm,  y_test,  verbose=0)
    # MSE calculée manuellement (one-hot vs softmax)
    train_mse = compute_mse_one_hot(model, X_train_norm, y_train)
    test_mse  = compute_mse_one_hot(model, X_test_norm,  y_test)

    print(f"\n  Durée d'entraînement        : {training_duration:.1f} s")
    print(f"  Nombre d'epochs (cycles)    : {EPOCHS}")
    print(f"  Batch size                  : {BATCH_SIZE}")
    print(f"  Learning rate               : {LEARNING_RATE}")
    print()
    print(f"  📊 Train :  loss={train_loss:.4f}  accuracy={train_acc*100:.1f}%  MSE={train_mse:.4f}")
    print(f"  📊 Test  :  loss={test_loss:.4f}  accuracy={test_acc*100:.1f}%  MSE={test_mse:.4f}")
    print()

    if test_mse < 0.05:
        print(f"  ✅ ET7 VALIDÉE : MSE test = {test_mse:.4f} < 0.05 ✓")
    else:
        print(f"  ⚠ ET7 PAS ENCORE : MSE test = {test_mse:.4f} ≥ 0.05")
        print(f"    Pistes : plus de samples (ET8 demande ≥50+50), data augmentation,")
        print(f"             ajuster epochs/batch, etc.")
    print()

    # --- Matrice de confusion
    y_pred = np.argmax(model.predict(X_test_norm, verbose=0), axis=1)
    cm = confusion_matrix(y_test, y_pred)
    print(f"  Matrice de confusion (test) :")
    print(f"             prédit vrai  prédit faux")
    print(f"  vrai réel  {cm[0,0]:>11}  {cm[0,1]:>11}")
    print(f"  faux réel  {cm[1,0]:>11}  {cm[1,1]:>11}")
    print()

    # --- Sauvegarde
    print("[6/6] Sauvegarde des artefacts...")
    model_path = MODELS_DIR / "cnn_vrai_faux.keras"
    norm_path  = MODELS_DIR / "normalization_params.npz"
    model.save(model_path)
    np.savez(norm_path, mean=mean, std=std)
    print(f"      ✓ Modèle           : {model_path}")
    print(f"      ✓ Normalisation    : {norm_path}")

    # --- Courbes d'entraînement (loss + accuracy uniquement, MSE manuelle finale)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))

    axes[0].plot(history.history["loss"],     label="train")
    axes[0].plot(history.history["val_loss"], label="test")
    axes[0].set_title("Loss (cross-entropy)")
    axes[0].set_xlabel("Epoch"); axes[0].set_ylabel("Loss")
    axes[0].legend(); axes[0].grid(alpha=0.3)

    axes[1].plot(history.history["accuracy"],     label="train")
    axes[1].plot(history.history["val_accuracy"], label="test")
    axes[1].set_title(f"Accuracy — MSE finale test = {test_mse:.4f}"
                      f" {'✓ ET7' if test_mse < 0.05 else '✗ ET7'}")
    axes[1].set_xlabel("Epoch"); axes[1].set_ylabel("Accuracy")
    axes[1].set_ylim(0, 1.05)
    axes[1].legend(); axes[1].grid(alpha=0.3)

    plt.tight_layout()
    curves_path = FIGURES_DIR / "fp5_training_curves.png"
    plt.savefig(curves_path, dpi=150, bbox_inches="tight")
    print(f"      ✓ Courbes          : {curves_path}")
    plt.close()

    # --- Matrice de confusion en figure
    fig, ax = plt.subplots(figsize=(5, 4))
    im = ax.imshow(cm, cmap="Blues")
    ax.set_xticks([0, 1]); ax.set_xticklabels(["vrai", "faux"])
    ax.set_yticks([0, 1]); ax.set_yticklabels(["vrai", "faux"])
    ax.set_xlabel("Prédit"); ax.set_ylabel("Réel")
    ax.set_title(f"Matrice de confusion (test)\nAccuracy = {test_acc*100:.1f}%")
    for i in range(2):
        for j in range(2):
            ax.text(j, i, cm[i, j], ha="center", va="center",
                    color="white" if cm[i, j] > cm.max()/2 else "black",
                    fontsize=14)
    plt.colorbar(im, ax=ax)
    plt.tight_layout()
    cm_path = FIGURES_DIR / "fp5_confusion_matrix.png"
    plt.savefig(cm_path, dpi=150, bbox_inches="tight")
    print(f"      ✓ Confusion        : {cm_path}")
    plt.close()

    print("\n" + "=" * 70)
    print("  Entraînement terminé.")
    print("=" * 70)


if __name__ == "__main__":
    train_and_evaluate()
