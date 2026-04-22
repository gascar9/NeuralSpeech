# Guide de test bench — FP1 (ADC 32 kHz)

**Objectif** : valider **ET1** (échantillonnage ADC à 32 kHz fixe et précis, déclenché par timer) avec toutes les preuves nécessaires pour la soutenance et le rapport.

**Temps estimé** : 45 min pour faire tout bien, photos comprises.

**Matériel requis**
- Arduino Due + câble USB **Programming Port** (le port bas, côté alim — PAS le Native du haut)
- Générateur de fonctions Siglent SDG 805 (GBF)
- Oscilloscope Siglent SDS 1102CML+ (2 voies)
- 2 câbles BNC + Y BNC + pinces crocodile / fils pour breadboard
- Smartphone ou appareil photo pour les clichés

---

## Vue d'ensemble des 4 tests

| # | Test | Critère | Dur. |
|---|------|---------|------|
| 1 | Upload + sanity check console | `Fe_reelle ≈ 32012 Hz` | 5 min |
| 2 | **Reconstruction 1 kHz** | DAC0 = entrée en forme, Vpp, Freq | 5 min |
| 3 | **Te = 31.25 µs** (cœur ET1) | Zoom marches DAC → période entre échantillons | 10 min |
| 4 | **Démo Nyquist** (très valorisé) | 15 kHz OK, 17 kHz → alias à 15 kHz | 15 min |

**⚠️ Important** : pour les tests 3 et 4, il faut le firmware **FP1 PURE** (sans filtre). On va upload un env dédié, puis revenir en pipeline complet après.

---

## Préparation : 2 firmwares différents

Le repo contient **2 environnements PlatformIO** dans `platformio.ini` :

- **`due`** (défaut) : firmware FP1+FP2 → le DAC sort le signal **filtré** (utile pour FP2 mais masque l'alias)
- **`due_fp1`** : firmware FP1 seul → le DAC sort le signal **ADC brut** (requis pour Te et Nyquist)

**Upload pour FP1 pur** (pour les tests 3 et 4) :
```bash
pio run -e due_fp1 --target upload
```

**Retour au pipeline complet** (après les tests FP1, avant de tester FP2) :
```bash
pio run -e due --target upload
```

Dans VS Code avec l'extension PlatformIO : en bas à gauche tu as un sélecteur d'env (`Default (ProjetCE)`), clique dessus → choisi `env:due_fp1`. Ensuite les boutons Build/Upload utilisent cet env.

---

## Câblage (commun aux 4 tests)

```
    GBF (Siglent SDG 805)
        BNC sortie
           │
        ┌──┴──┐
        │  Y  │  (T BNC ou câble en Y)
        └┬───┬┘
         │   │
    ┌────┘   └────┐
    │             │
  A0 (Due)    CH2 (oscillo)   ← trace du signal d'entrée
    │
  (Arduino Due)
    │
  DAC0 (pin 66)   ── CH1 (oscillo)   ← trace du signal reconstruit
    │
  GND  ── GND GBF  ── GND oscillo       ← masse commune OBLIGATOIRE
```

**Règles GBF Siglent SDG 805** :
- Forme : **sinusoïde**
- Amplitude : **1 Vpp** sauf précisé
- Offset DC : **+1.65 V** (pour que le signal balaie 0.15 V → 3.15 V dans la plage ADC 0-3.3 V)
- Charge : **HI-Z** (sinon l'amplitude affichée est fausse d'un facteur 2)

**Réglages oscilloscope initiaux** :
- CH1 : 500 mV/div, couplage DC
- CH2 : 500 mV/div, couplage DC
- Timebase : 500 µs/div (adapter par test)
- Trigger : CH2, front montant, niveau ≈ 1.65 V

---

## TEST 1 — Sanity check console série (5 min)

**But** : vérifier que la Due démarre bien et que l'ISR tourne à la bonne fréquence.

1. **Upload env `due_fp1`** :
   ```bash
   pio run -e due_fp1 --target upload
   ```
2. **Ouvre le monitor série** (250000 bauds). Tu dois voir :
   ```
   === NeuralSpeech FP1 PURE — ADC 32 kHz + DAC brut (pas de filtre) ===
   *** Mode validation ET1 : DAC0 restitue le signal ADC brut ***
   FE             : 32000 Hz
   ...
   Demarrage acquisition + filtrage...
   [FP1] Fe_reelle=32012 Hz | samples=XXXXX | buf_used=0/512
   [FP1] Fe_reelle=32012 Hz | samples=XXXXX | buf_used=0/512
   ...
   ```
3. **Critères de succès** :
   - `Fe_reelle` oscille entre **32008 et 32015 Hz** (cible théorique : 32012 Hz)
   - `buf_used` reste **≤ 5** (loop rapide, pas de saturation)
   - **Aucune ligne `[FP2]`** n'apparaît (mode FP1 pur — attendu)

**📸 Capture à faire** : screenshot du monitor série avec au moins 5 lignes `[FP1]` visibles
→ `assets/FP1/fp1_console_pure.png`

---

## TEST 2 — Reconstruction 1 kHz (5 min)

**But** : montrer que la chaîne ADC → buffer → DAC fonctionne (le signal reconstruit a la même forme et fréquence que l'entrée).

1. GBF : **sinus 1 kHz, 1 Vpp, offset 1.65 V**
2. Oscillo :
   - CH1 (DAC0) : 500 mV/div, DC
   - CH2 (A0) : 500 mV/div, DC
   - Timebase : 500 µs/div (on voit 2-3 périodes)
3. Active les mesures auto sur CH1 (`Measure` → `Vpp`, `Mean`, `Freq`, `Period`)

**Critères de succès** :
- `Freq` CH1 ≈ 1.00 kHz (identique à l'entrée)
- `Period` CH1 ≈ 1.00 ms
- `Vpp` CH1 ≈ `Vpp` CH2 (à ±10 % près, car le DAC a une résolution 12 bits)
- Les deux traces **parfaitement superposables**, même forme, même phase

**📸 Captures** :
- `assets/FP1/fp1_reconstruction_1kHz.jpeg` (tu as **déjà** celle-ci : `FP1Oscilloscope2.jpeg` OK si tu veux la garder)
- Bonus : `assets/FP1/fp1_reconstruction_100Hz.jpeg` (même test à 100 Hz — prouve que le système marche en basses fréquences aussi)

---

## TEST 3 — Mesure directe de Te = 31.25 µs (⭐ cœur ET1, 10 min)

**But** : **prouver que Te = 1/32000 = 31.25 µs** avec une mesure directe entre deux échantillons successifs. C'est LA validation ET1 officielle.

### Procédure

1. **GBF** : signal **DC pur** ou **triangle très lent** (10 Hz, 1 Vpp, offset 1.65 V)
   - Le but c'est de voir les **marches** du DAC, pas une sinusoïde. En DC ou signal très lent, les marches sont bien visibles
   - En pratique, garder **sinus 1 kHz** marche aussi — on zoome sur une petite portion de la sinus qu'on approxime par des marches
2. **Oscillo — zoom temporel fort** :
   - Timebase : **5 µs/div** (ou 10 µs/div)
   - CH1 : 50 mV/div ou 100 mV/div pour voir les marches finement
   - Trigger sur CH1, AUTO
3. **Activation curseurs** : bouton `Cursors` → mode `Time` → place les 2 curseurs sur **le front d'une marche** et **le front de la marche suivante**
4. **Lecture** : `ΔT` entre curseurs devrait afficher **≈ 31.25 µs** (tolérance ±0.5 µs)

### Vérification croisée par console

En parallèle, lis la valeur `Fe_reelle` sur la console :
```
[FP1] Fe_reelle=32012 Hz  →  Te = 1/32012 = 31.238 µs
```
Les deux mesures (oscillo et console) doivent converger vers **31.25 ± 0.5 µs**.

### 📸 Captures

- `assets/FP1/fp1_te_mesure_31us.jpeg` : oscillo zoomé avec curseurs sur marches, ΔT lisible
- Bonus : `assets/FP1/fp1_fe_console.png` : ligne console avec `Fe_reelle=32012`

---

## TEST 4 — Démo Nyquist (⭐⭐ critère discriminant soutenance, 15 min)

**But** : montrer concrètement ce qu'est le **repliement spectral** (aliasing) — ça justifie l'existence du filtre FP2.

### Partie 4A — Nyquist respecté (5 min)

1. GBF : **sinus 15 kHz** (juste sous Nyquist = Fe/2 = 16 kHz), 1 Vpp, offset 1.65 V
2. Oscillo :
   - Timebase : 20 µs/div (on voit 3-4 périodes de 15 kHz)
   - CH1 et CH2 à 500 mV/div
3. **Observation attendue** : CH1 (DAC0) montre une sinusoïde à **15 kHz**, bruitée (seulement ~2 échantillons par période) mais **reconstituable**

**📸** `assets/FP1/fp1_nyquist_15kHz_ok.jpeg`

### Partie 4B — Nyquist violé → repliement (10 min)

1. GBF : monte à **17 kHz** (au-dessus de Nyquist), 1 Vpp
2. **Observation attendue** : CH1 montre une sinusoïde **à 15 kHz** — ce n'est PAS le signal réel d'entrée, c'est un **alias** créé par le sous-échantillonnage virtuel du théorème de Shannon :
   $$f_{\text{alias}} = F_e - f_{\text{entree}} = 32\,000 - 17\,000 = 15\,000 \text{ Hz}$$
3. Pour confirmer : mesure la fréquence de CH1 via `Measure → Freq` → doit donner **15 kHz ± 200 Hz** (alors que l'entrée CH2 est à 17 kHz)
4. Test complémentaire : monte à **20 kHz** → alias à 32 − 20 = 12 kHz
5. Test final : **31 kHz** → alias à 32 − 31 = 1 kHz (quasi invisible, preuve que l'alias est bien dû au sampling)

**📸 Captures impératives** :
- `assets/FP1/fp1_nyquist_17kHz_alias.jpeg` : entrée 17 kHz, CH1 à 15 kHz (avec mesure fréq visible)
- Bonus : `assets/FP1/fp1_nyquist_20kHz_alias.jpeg` : entrée 20 kHz, alias à 12 kHz

### Pourquoi ce test est ⭐⭐

En soutenance, ce test démontre visuellement que :
1. Tu as compris le théorème de Shannon
2. Tu sais prouver la nécessité du filtre FP2 par une expérience concrète
3. Tu peux motiver la conception du filtre anti-repliement

C'est **le** test qui fait la différence entre une note "Acquis" et "Expert" dans la grille d'évaluation.

---

## Après les tests FP1

### 1. Repasser en firmware complet
```bash
pio run -e due --target upload
```
Le DAC ressortira le signal filtré (mode normal).

### 2. Captures dans `assets/FP1/`
Vérifie que tu as bien :
- ✅ `FP1Oscilloscope2.jpeg` (reconstruction 1 kHz, déjà là)
- 🆕 `fp1_console_pure.png` (Test 1)
- 🆕 `fp1_te_mesure_31us.jpeg` (Test 3 — le critique)
- 🆕 `fp1_nyquist_15kHz_ok.jpeg` (Test 4A)
- 🆕 `fp1_nyquist_17kHz_alias.jpeg` (Test 4B — l'impressionnant)

### 3. Tu me pings
Envoie-moi tes captures + les valeurs numériques suivantes :
- `Fe_reelle` lu en console (ex : `32012 Hz`)
- `ΔT` mesuré à l'oscillo au Test 3 (ex : `31.28 µs`)
- Fréquence de l'alias observé à l'entrée 17 kHz (doit être ~15 kHz)

Je remplis les `\TODO` restants du rapport `rapport/chapters/03-fp1.tex` avec tes vrais chiffres + j'insère tes nouvelles figures.

---

## Pièges à éviter (liste concentrée)

- ❌ **Port USB Native** au lieu de Programming → l'USB natif introduit de la gigue sur l'ISR TC0, Fe pas stable
- ❌ **GBF sans HI-Z** → amplitude réelle divisée par 2, Vpp qui ne correspond à rien
- ❌ **Timebase oscillo à 500 µs/div pour le test 3** → les marches font 31 µs, invisibles à cette échelle, il FAUT zoomer à 5-10 µs/div
- ❌ **Masses pas reliées** entre GBF / Due / oscillo → bruit 50 Hz, pics parasites
- ❌ **Confondre `Te` et `période du signal`** : Te = 31.25 µs est l'intervalle entre 2 **échantillons**, pas entre 2 périodes de sinusoïde
- ❌ **Uploader en env `due` et vouloir voir Nyquist** → le filtre FP2 kill l'alias à 17 kHz, il faut env `due_fp1`

---

## Résumé des critères de validation ET1

| Critère | Valeur attendue | Où le voir | Status |
|---------|-----------------|-----------|--------|
| ADC déclenché par timer (conception) | Code TC0_Handler() | `src/main.cpp` | ✅ déjà fait |
| `Fe_reelle` console | 32012 ± 3 Hz | Monitor série | à noter |
| Reconstruction signal 1 kHz | DAC = entrée en forme | Oscillo test 2 | ✅ déjà (FP1Oscilloscope2.jpeg) |
| **Te = 31.25 µs** (mesure oscillo) | 31.25 ± 0.5 µs | Test 3 curseurs | **à faire** |
| Nyquist respecté 15 kHz | Signal propre à 15 kHz | Test 4A | **à faire** |
| Repliement 17 kHz → 15 kHz | Alias visible | Test 4B | **à faire** |

Quand les 3 "à faire" sont faits → ET1 complètement documenté pour la soutenance.
