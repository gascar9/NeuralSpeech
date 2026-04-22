# Guide de test bench — FP2 (Filtrage + sous-échantillonnage)

**Objectif** : valider **ET2** (atténuation ≥ 30 dB au-dessus de 4 kHz) et **ET3** (filtrage < 31 µs par échantillon) sur le banc de TP.

**Temps estimé** : 45 min pour les mesures minimales, 1h30 pour la version complète (avec Nyquist + photos propres).

**Matériel requis**
- Arduino Due + câble USB **Programming Port** (pas Native)
- MAX9814 débranché ou non — on injecte le signal directement via GBF
- Générateur de fonctions Siglent SDG 805
- Oscilloscope Siglent SDS 1102CML+ (2 voies)
- Multimètre (pour contrôle Vpp en DC-coupling au besoin)
- PC avec monitor série (PlatformIO, PuTTY, ou `pio device monitor`)

---

## Préparation (5 min)

1. **Upload le firmware** : dans le terminal de VS Code avec l'extension PlatformIO, ou directement :
   ```bash
   pio run --target upload
   ```
   Vérifier la sortie : `[SUCCESS]` et taille RAM ~8.5 %, Flash ~5.4 %.

2. **Câblage test bench** (pas le MAX9814 pour l'instant, on teste le filtre avec un signal propre) :
   - GBF sortie BNC → **un Y BNC** pour avoir le signal sur **A0 de la Due** ET sur **CH2 de l'oscillo** (trace d'entrée de référence)
   - **DAC0** (pin 66 de la Due) → **CH1 de l'oscillo** (trace filtrée en sortie)
   - Masse commune GBF ↔ Due ↔ oscillo
   - Alim Due via USB

3. **Règles GBF importantes** :
   - Amplitude : **1 Vpp centré sur 1.65 V** (offset DC de 1.65 V) pour rester dans la plage ADC 0-3.3 V. Sur le SDG 805 : `AMPL = 1.00 Vpp`, `OFFSET = 1.65 V DC`.
   - Forme : **sinusoïde**
   - Charge de sortie : **HI-Z** (HI-Z) sur le GBF pour afficher la bonne tension

4. **Monitor série** : ouvre-le à **250000 bauds**. Tu dois voir alternativement :
   ```
   [FP1] Fe_reelle=32012 Hz | samples=32012 | buf_used=X/512
   [FP2] filter_us_avg=X.XX max=Y.YY | buf8k_used=Z/2048 | taps=97
   ```

---

## Test 1 — Bande passante préservée (ET2 côté passant) (10 min)

**But** : vérifier que les fréquences vocales utiles (300 Hz à 3 kHz) passent sans atténuation notable.

**Procédure**
1. Régler GBF sur **500 Hz sinus, 1 Vpp, offset 1.65 V**
2. Sur l'oscillo : lire le Vpp de CH2 (entrée) et CH1 (DAC0 filtré)
3. Reporter dans le tableau ci-dessous
4. Répéter pour 1000 Hz, 2000 Hz, 3000 Hz

| f (Hz) | Vpp entrée (CH2) | Vpp DAC0 (CH1) | Atténuation (dB) |
|-------:|-----------------:|---------------:|-----------------:|
| 500    |                  |                |                  |
| 1000   |                  |                |                  |
| 2000   |                  |                |                  |
| 3000   |                  |                |                  |

**Formule atténuation** : `20 × log10(Vpp_CH1 / Vpp_CH2)` — doit être **proche de 0 dB** (entre 0 et -1 dB) dans toute cette plage.

**📸 Screenshot à prendre** : oscillo à 1 kHz avec CH1 et CH2 superposés, curseurs activés sur Vpp de chaque voie. Nommer `assets/FP2/fp2_bandpass_1kHz.jpeg`.

---

## Test 2 — Coupure à 4 kHz (ET2 cœur — critère noté) (10 min)

**But** : prouver que l'atténuation à 4 kHz est **≥ 30 dB**.

**Procédure**
1. Régler GBF à **3500 Hz** (début de transition) — mesure
2. Passer à **4000 Hz** — mesure critique
3. Passer à **5000 Hz**, **6000 Hz**, **8000 Hz** — mesures de validation au-delà

| f (Hz) | Vpp entrée | Vpp DAC0 | Atténuation (dB) | ET2 respecté ? |
|-------:|-----------:|---------:|-----------------:|:--------------:|
| 3500   |            |          |                  | N/A (transition) |
| **4000** |          |          |                  | **≥ 30 dB attendu** |
| 5000   |            |          |                  | ≥ 40 dB attendu |
| 6000   |            |          |                  | ≥ 55 dB attendu |
| 8000   |            |          |                  | ≥ 70 dB attendu |

**Astuce mesure à 4 kHz+** : quand l'atténuation est forte, le signal CH1 devient tout petit. **Augmenter l'amplitude GBF temporairement** (par exemple 2 Vpp) et **zoomer la voie CH1** (calibre mV/div petit, typiquement 10 mV/div ou 20 mV/div). Bien repartir avec 1 Vpp GBF pour la suite.

**📸 Screenshots** :
- `assets/FP2/fp2_cutoff_4kHz.jpeg` : 4 kHz, mesure nette de l'atténuation
- `assets/FP2/fp2_cutoff_8kHz.jpeg` : 8 kHz, CH1 quasi-nul

---

## Test 3 — Réponse complète pour le rapport (15 min)

**But** : tracer la vraie courbe mesurée de réponse en fréquence et la superposer à la courbe théorique (figure `rapport/figures/FP2/filter_response.png`).

**Procédure**
1. Compléter les mesures ci-dessus par **100 Hz**, **200 Hz**, **3200 Hz** (fin bande passante), **3577 Hz** (fc -6 dB), et quelques points dans la transition (3700, 3800, 3900).
2. Ouvrir un fichier LibreOffice/Excel/Python et reporter les mesures.
3. Calcul atténuation = `20 * log10(Vpp_sortie / Vpp_entree)`.
4. Plot la courbe mesurée (points) superposée à la courbe théorique lissée.

Exemple rapide en Python :
```python
import numpy as np
import matplotlib.pyplot as plt

freqs_hz   = np.array([100, 500, 1000, 2000, 3000, 3200, 3500, 3577, 3700, 4000, 5000, 6000, 8000])
att_db     = np.array([ 0,   0,    0,    0,   -0.2, -0.5, -2,   -6,   -15,  -30.2, -44, -60, -72])  # À REMPLACER avec tes mesures

plt.figure(figsize=(10,4))
plt.plot(freqs_hz, att_db, 'ro-', label='Mesuré')
# superposer la courbe théorique ici si on la charge depuis un .npz ou .csv
plt.axhline(-30, color='r', linestyle='--', label='ET2 : -30 dB')
plt.axvline(4000, color='orange', linestyle=':', label='f_stop = 4 kHz')
plt.xlabel('Fréquence (Hz)')
plt.ylabel('Atténuation (dB)')
plt.grid(True, which='both')
plt.legend()
plt.savefig('rapport/figures/FP2/fp2-freq-response-measured.png', dpi=150)
```

**📸 Figure à produire** : `rapport/figures/FP2/fp2-freq-response-measured.png` (à sauvegarder et commiter).

---

## Test 4 — Temps de filtrage (ET3) (5 min)

**But** : confirmer que `filter_us_max < 31.00`.

**Procédure**
1. Observer la ligne `[FP2] filter_us_avg=... max=...` sur le monitor série pendant au moins 30 secondes.
2. Noter la valeur max la plus haute vue.
3. Injection d'un signal fort (2 Vpp, sinus 2 kHz) pour stresser un peu.

**Résultat attendu** : `avg ≈ 3–5 µs`, `max < 15 µs`.

**📸 Capture** : capture écran du monitor série avec plusieurs lignes `[FP2]` visibles. `assets/FP2/fp2_timing_serial.png`.

**⚠️ Si `max` dépasse 20 µs** : un autre code a été ajouté dans la loop. Regarder si quelqu'un a ajouté un `Serial.print` quelque part dans le chemin critique.

---

## Test 5 — Démo de l'efficacité du filtre (BONUS, 10 min)

**But** : prouver visuellement que le filtre supprime l'aliasing (comparaison FP1 sans filtre ↔ FP2 avec filtre).

**Procédure**
1. Garder le firmware FP2 actuel
2. Injection **17 kHz** (au-delà de Nyquist) avec 1 Vpp sur A0
3. Observer CH1 (DAC0) : le signal doit être **quasi-nul** (< 20 mV) — le filtre a supprimé la fréquence image
4. Pour comparaison : sans le filtre (version FP1 pure), on aurait vu un signal fantôme à 15 kHz (32 − 17 = 15 kHz)

**📸 Screenshot** : `assets/FP2/fp2_nyquist_17khz_filtered.jpeg` — la comparaison avec `assets/FP1/` (à prendre pendant les tests FP1) est un argument fort pour la soutenance.

---

## Après le bench

1. **Mettre toutes les captures dans `assets/FP2/`** et committer
2. **Remplir les `\TODO{}` du rapport** `rapport/chapters/04-fp2.tex` — sections "Validation" :
   - Tableau `tab:fp2-measures` avec les vraies valeurs
   - Figure mesurée + insertion en LaTeX
   - Capture timing série
3. **Ping-moi** (Claude) avec les résultats et j'update le rapport automatiquement
4. **Push git** après intégration

## Pièges classiques à éviter

- ❌ **Câble GBF sans Hi-Z** : double amplitude apparente, mesures faussées. Toujours mettre HI-Z sur le SDG.
- ❌ **DC-coupling oscillo sur CH2** : on perd la trace de l'offset 1.65 V. Préfère AC-coupling si on veut voir l'amplitude seulement.
- ❌ **Saturation ADC** : si `Vpp > 1.65 V` le signal dépasse la plage ADC et on voit un écrêtage en haut ou en bas. Garder `Vpp ≤ 3 V` et offset = Vpp/2 + 0.15 V minimum.
- ❌ **Port USB Native au lieu de Programming** : l'USB natif introduit de la gigue sur l'ISR TC0. Utiliser le **Programming Port** pendant toute la mesure.
- ❌ **Oublier d'attendre la stabilisation** : après changement de fréquence GBF, attendre 1–2 secondes avant de lire Vpp (le filtre a une réponse transitoire).

## Résumé des critères de validation

| Critère | Mesure attendue | Où le voir |
|---------|-----------------|-----------|
| **ET2** | Atténuation ≥ 30 dB à 4 kHz | Test 2, ligne "4000 Hz" |
| **ET3** | `filter_us_max < 31.00` | Test 4, monitor série |
| Bande passante utile | 0–3 kHz à < 1 dB d'atténuation | Test 1 |
| Buffer 8 kHz opérationnel | `buf8k_used` oscille entre 0 et une valeur stable | Monitor série, ligne `[FP2]` |
| Démo anti-repliement | Signal 17 kHz atténué > 60 dB | Test 5 |

---

**Quand tout est validé** → on peut passer à FP3 (transfert Audacity) en toute confiance.
