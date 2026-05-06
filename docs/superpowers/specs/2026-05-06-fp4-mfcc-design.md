# FP4 — MFCC en Q15 strict — Design

**Date** : 2026-05-06
**Auteur** : Gaspard BOUCHARLAT (équipe NeuralSpeech S2)
**Statut** : approuvé pour implémentation
**Contexte** : Soutenance 2 ECE Paris ING3 S6 Calcul Embarqué — extension du firmware Arduino Due existant (FP1+FP2+FP3 validés en S1)

## 1. Objectif

Calculer la matrice **MFCC 62×13** sur 1 seconde d'audio numérisé à 8 kHz, **entièrement en arithmétique entière Q15**, sur le SAM3X8E (Cortex-M3 84 MHz, sans FPU).

Cette matrice est l'entrée du réseau de neurones convolutionnel (FP6) qui classifiera les mots prononcés.

**Exigences techniques couvertes** :
- **ET5** : framing 256 samples / hop 128 (50 % overlap)
- **ET6** : 13 MFCCs sur 1 s → matrice 62×13

## 2. Choix structurants validés

| Choix | Valeur | Justification |
|---|---|---|
| Niveau de finalisation | **C** — Optimisé Q15 dès maintenant | Anticiper le budget CPU/RAM pour FP6 (CNN inférence également gourmand) |
| Format numérique | **C1** — Q15 strict bout en bout | Argument fort rapport S2 : « pipeline 100 % entier, pas une seule instruction flottante » |
| Stratégie validation | **V2** — Mirror Python Q15 bit-exact | Oracle exact pour debug, plus fiable que librosa (qui fait du float) |
| Trigger flow | **α** — All-in-one auto après dump WAV | Un appui = tout (audio + WAV + MFCC). Aligné avec le besoin FP5 « 1 sample = 1 paire (audio, MFCC) » |
| Code organization | **`lib/mfcc/`** séparée | Lib testable indépendamment via PlatformIO test framework |

## 3. Architecture

### 3.1 Extension de la FSM FP3 existante

```
IDLE
  │ bouton D2
  ▼
ARMING ─────► (capture 8000 samples dans captureBuffer)
  │ buffer plein
  ▼
DUMPING_WAV ─► (dump binaire WAV série, magic 0xAA55AA55)
  │ footer envoyé
  ▼
COMPUTING_MFCC ─► (compute_mfcc() bloquant, ~17 ms)
  │ matrice prête
  ▼
DUMPING_MFCC ──► (dump binaire MFCC série, magic 0xCAFEBABE)
  │ footer envoyé
  ▼
IDLE
```

**Pendant `COMPUTING_MFCC`** : calcul bloquant dans la `loop()`. Logs `[FP1]/[FP2]` suspendus comme pendant `DUMPING_WAV`. ISR + filtrage continuent à tourner. **Avant compute** : flush `bufTail = bufHead` pour éviter l'overflow d'`adcBuffer[512]` pendant les 17 ms de calcul (~544 samples poussés > 512 cases).

**Pendant `DUMPING_MFCC`** : service non-bloquant calqué sur `fp3_service_dump()`.

### 3.2 Organisation du code : `lib/mfcc/`

```
lib/mfcc/
├── library.json
├── include/
│   └── mfcc.h           ← API publique unique
└── src/
    ├── mfcc.cpp         ← orchestration : preemphase + frame loop
    ├── fft.h/.cpp       ← FFT 256 pts radix-2 in-place Q15
    ├── mel_bank.h/.cpp  ← 26 filtres triangulaires + |·|²
    ├── dct.h/.cpp       ← DCT-II 26→13 Q15
    ├── log_lut.h/.cpp   ← log₂ Q15 via LUT 1024 + interpolation linéaire
    └── tables.h         ← Hamming LUT + twiddles + mel filters + DCT cosines
```

### 3.3 API publique

```cpp
// lib/mfcc/include/mfcc.h
constexpr size_t MFCC_FRAMES = 62;
constexpr size_t MFCC_COEFS  = 13;
constexpr size_t MFCC_AUDIO_SAMPLES = 8000;  // 1 s @ 8 kHz

void compute_mfcc(const int16_t* audio_8khz_8000_samples,
                  int16_t mfcc_out[MFCC_FRAMES][MFCC_COEFS]);
```

Une seule fonction publique. FFT, DCT, log, mel sont internes (déclarés dans les `.h` sous `lib/mfcc/src/` mais pas exposés via `include/`).

### 3.4 Modifications dans `src/main.cpp`

- **Allocation statique** : `static int16_t mfccMatrix[62][13];` (1 612 octets, persistant pour FP6)
- **Enum FSM étendu** : ajout de `FP3_COMPUTING_MFCC` et `FP3_DUMPING_MFCC`
- **State machine étendue** dans la `loop()` pour gérer les nouveaux états
- **Service** : nouveau `fp3_service_mfcc_dump()`
- **Hook** : transition `FP3_DUMPING` (footer envoyé) → `FP3_COMPUTING_MFCC`

## 4. Pipeline data flow Q15

```
captureBuffer[8000] int16  (1 s @ 8 kHz, post-FP2)
        │
        ▼
[1] Préemphase Q15           y[n] = x[n] - 0.97·x[n-1]   (in-place)
        │
        ▼
[2] Frame extraction         62 frames de 256, hop 128, padding 0 sur dernier
        │
        ▼ (boucle par frame)
[3] Hamming Q15              x_w[n] = x[n] · hamming[n]
        │
        ▼
[4] FFT 256-pts radix-2 Q15  X[k] = FFT(x_w), in-place, scaling >>1 par étage
        │
        ▼
[5] Magnitude²               P[k] = re² + im²  (int32, k=0..127)
        │
        ▼
[6] Banc MEL (26 filtres)    E[m] = Σ P[k]·H_m[k]
        │
        ▼
[7] log₂ Q15 via LUT         L[m] = log₂(E[m]) (CLZ + LUT 1024 + interp)
        │
        ▼
[8] DCT-II 26→13 Q15         c[k] = Σ L[m]·cos((m+0.5)kπ/26)
        │
        ▼
mfccMatrix[frame][0..12] int16 Q15
```

### 4.1 Détails Q15 par étape

#### [1] Préemphase
- Constante : `ALPHA_Q15 = 31785` (= round(0.97 × 32768))
- Calcul : `y = x - (int16_t)((ALPHA_Q15 * prev) >> 15)`
- Clamp défensif int16 (overflow théorique +64544 si `x = +32767, prev = -32768`)
- Coût : 100 µs total

#### [2] Frame extraction
- 62 frames, hop 128. Frame 0 : samples [0..255]. Frame 61 : samples [7808..8063]
- Le dernier frame dépasse le buffer 8000 → **64 zéros de padding**
- Pas de copie : pointeurs sur `preemph[]`

#### [3] Hamming
- LUT 256 valeurs Q15 précalculée en flash
- `windowed = (frame[n] * hamming_lut[n]) >> 15`
- Coût : 200 µs total

#### [4] FFT 256-pts radix-2 — étape dominante (15 ms / 17 ms)
- 8 étages (log₂256), 128 butterflies par étage
- **Scaling unconditionnel `>> 1` à chaque étage** pour éviter overflow int16
- Précision finale ~8 bits sur le spectre (perte 8 bits sur 8 étages)
- Suffisant pour MFCC : dynamique vocale ~40 dB = 7 bits utiles
- Twiddle factors : 128 cos + 128 sin Q15 en flash (512 octets)

Butterfly Q15 :
```cpp
int32_t tr = ((int32_t)wr * fft_real[j] - (int32_t)wi * fft_imag[j]) >> 15;
int32_t ti = ((int32_t)wr * fft_imag[j] + (int32_t)wi * fft_real[j]) >> 15;
fft_real[j] = (int16_t)((fft_real[i] - tr) >> 1);
fft_imag[j] = (int16_t)((fft_imag[i] - ti) >> 1);
fft_real[i] = (int16_t)((fft_real[i] + tr) >> 1);
fft_imag[i] = (int16_t)((fft_imag[i] + ti) >> 1);
```

#### [5] Magnitude²
- Symétrie hermitienne : on ne calcule que les bins [0..127]
- `power[k] = (int32_t)fft_real[k]² + (int32_t)fft_imag[k]²` (toujours ≥ 0)
- Coût : 5 µs / frame

#### [6] Banc MEL 26 filtres triangulaires
- Espacement MEL entre 0 et 4 kHz (Nyquist)
- Chaque filtre couvre ~10 bins FFT
- Stockage compact : `struct { uint8_t start, peak, end; int16_t coefs_q15[16]; }` × 26 = ~512 octets en flash
- Coût : 5 µs / frame

#### [7] log₂ Q15 via LUT — la subtilité C1
1. `clz = __builtin_clz(mel_energies[m])` (1 instruction Cortex-M3)
2. Exposant `e = 31 - clz`
3. Mantisse normalisée : `m_norm = (mel_energies[m] >> (e - 15)) & 0x7FFF` (Q15 dans [0, 1))
4. Lookup : `lut_idx = m_norm >> 5` (1024 entrées couvrant log₂([1, 2)))
5. Interpolation linéaire : `frac = m_norm & 0x1F`, `result = log2_lut[lut_idx] + ((slope_lut[lut_idx] * frac) >> 5)`
6. Recombinaison : `log2(x) = e + result` (Q15)
7. **Edge case** : `mel_energies[m] == 0` → retour `INT16_MIN`

Précision finale ~12 bits sur log₂. Coût : 1 ms total (62 × 26 logs).

LUT en flash : 1024 × 2 octets (table) + 1024 × 2 octets (slopes) = 4 Ko.

#### [8] DCT-II 26→13
- Matrice cosinus précalculée Q15 : `dct_q15[13][26]` = 676 octets en flash
- Coefficient : `dct_q15[k][m] = round(cos((m + 0.5) * k * π / 26) * 32767)`

```cpp
for (k = 0..12) {
    int32_t sum = 0;
    for (m = 0..25) sum += (int32_t)log_mel[m] * dct_q15[k][m];
    mfcc[frame][k] = (int16_t)(sum >> 15);
}
```

Coût : 300 µs total.

### 4.2 Récap timing total

| Étape | Coût total (62 frames) |
|---|---|
| Préemphase | 100 µs |
| Hamming | 200 µs |
| **FFT** | **15 ms** ← dominant |
| Magnitude² | 300 µs |
| Banc MEL | 300 µs |
| log₂ LUT | 1 ms |
| DCT-II | 300 µs |
| **Total** | **~17 ms** |

## 5. Memory & Flash budget

### 5.1 RAM (96 Ko SRAM total)

| Buffer | Taille | Persistance |
|---|---|---|
| `mfccMatrix[62][13]` int16 | 1 612 octets | Persistant (consommé par FP6 inférence) |
| `preemphBuffer[8000]` int16 | 16 Ko | **Réutilise `captureBuffer` in-place** |
| `fftReal[256]`, `fftImag[256]` int16 | 1 Ko | Scope frame |
| `magnitudeSquared[128]` int32 | 512 octets | Scope frame |
| `melEnergies[26]` int32 | 104 octets | Scope frame |
| `logMelEnergies[26]` int16 Q15 | 52 octets | Scope frame |
| **Total nouveau** | **~3.3 Ko** | |

**Bilan** : 24 084 octets (avant) → ~27 400 octets (après) = **27.9 % SRAM**. Marge ~70 Ko pour FP6.

### 5.2 Flash (512 Ko total)

| LUT / Code | Taille |
|---|---|
| `hamming_lut[256]` int16 | 512 octets |
| `twiddle_factors_q15[128]` (cos+sin) | 512 octets |
| `mel_filter_bank[26]` (struct compacte) | ~512 octets |
| **`log2_lut[1024]` + `log2_slopes[1024]`** | **4 Ko** |
| `dct_cosines_q15[13][26]` | 676 octets |
| Code MFCC compilé (estimation) | ~3-4 Ko |
| **Total nouveau** | **~9-10 Ko** |

**Bilan** : 28 668 octets (avant) → ~38 000 octets (après) = **~7.3 % Flash**. Marge >92 %.

### 5.3 Timing utilisateur

| Phase | Avant FP4 | Après FP4 |
|---|---|---|
| ARMING (capture 1 s) | 1000 ms | 1000 ms |
| DUMPING_WAV | ~640 ms | ~640 ms |
| COMPUTING_MFCC | — | **+17 ms** |
| DUMPING_MFCC | — | **+65 ms** |
| **Total par appui** | 1.64 s | **1.72 s** |

Différence imperceptible côté UX.

## 6. Protocole binaire MFCC

Calqué sur le protocole FP3 (WAV) :

```
[0xCA 0xFE 0xBA 0xBE]    ← magic header (4 octets)
[uint32 LE = 62]          ← nombre de frames (4 octets)
[uint32 LE = 13]          ← coefs par frame (4 octets)
[62 × 13 × int16 LE]      ← matrice MFCC (1612 octets)
[0xC0 0xDE 0xBA 0xBE]    ← magic footer (4 octets)
                          ───────────
Total : 1628 octets
```

Côté Python : `scripts/fp3_recv.py` étendu pour parser les 2 blocs successifs (WAV + MFCC) après chaque appui bouton. Output :
- `recording_YYYYMMDD_HHMMSS.wav` (existant)
- `recording_YYYYMMDD_HHMMSS.mfcc.npy` (nouveau, numpy array int16 (62, 13))

## 7. Error handling

| # | Risque | Protection |
|---|---|---|
| 1 | Préemphase overflow int16 | Clamp `[INT16_MIN, INT16_MAX]` |
| 2 | log₂(0) sur silence pur | Plancher `INT16_MIN` |
| 3 | `adcBuffer` overflow pendant 17 ms compute | Flush `bufTail = bufHead` avant compute |
| 4 | Échec écriture série pendant `DUMPING_MFCC` | Service non-bloquant via `Serial.availableForWrite()` |

**Cas safe par construction** (overflow numérique) :
- Magnitude² : avec scaling FFT par étage, max post-FFT ~±8192 → max power ~134 M, bien dans int32
- Banc MEL : sum de ~10 termes int32 bornés
- DCT-II : 26 × Q15 × Q15 = ~26 M, bien dans int32

## 8. Stratégie de tests

### 8.1 Niveau 1 — Unit tests C++ (`lib/mfcc/test/`)

Lancement via `pio test -e due`. Un fichier par étape critique.

| Test | Vérifie | Méthode |
|---|---|---|
| `test_preemphasis.cpp` | clamp + filtre HPF | input pas de poles HF, output diff fini |
| `test_hamming.cpp` | LUT correcte | symétrie, peak = 0.54+0.46 |
| `test_fft.cpp` | FFT correcte | sin(2π·k·n/N) → pic au bin k |
| `test_mel_bank.cpp` | filtres triangulaires bien placés | énergie 1 kHz → max sur le bon filtre mel |
| `test_log_lut.cpp` | log Q15 vs log() float | écart < 0.5 % sur 1000 valeurs aléatoires |
| `test_dct.cpp` | DCT-II inversible | round-trip DCT⁻¹∘DCT ≈ identité (tolérance Q15) |

**Critère** : tous les tests passent sur Due.

### 8.2 Niveau 2 — Mirror Python (`scripts/mfcc_reference.py`)

Réimplémentation exacte du pipeline C++ en Python avec `numpy.int16` strict. Sert d'oracle pour le test bit-exact.

**Tables partagées** : `tables.py` contient les mêmes LUTs Q15 que `lib/mfcc/src/tables.h`. Pour éviter la divergence, génération depuis un script unique `scripts/gen_tables.py` qui produit les deux formats.

### 8.3 Niveau 3 — Integration test bit-exact

Le test critique :

1. Audio test généré côté Python : sinusoïde 1 kHz pure, 1 s @ 8 kHz, amplitude ±20000 → `test_audio.bin` (8000 × int16 LE)
2. Audio chargé sur Due via série → posé dans `captureBuffer`
3. Firmware calcule `mfccMatrix[62][13]`, dump via série
4. Python : `firmware_mfcc = parse_serial_dump()`
5. Python : `reference_mfcc = compute_mfcc_q15(test_audio)`
6. Assertion : `np.array_equal(firmware_mfcc, reference_mfcc)` → **bit-exact**

**Critère** : `assert` passe sur 3 audios test :
- `test_sin_1khz.bin` (pic FFT bin 32)
- `test_silence.bin` (zeros → MFCC tous à `INT16_MIN`)
- `test_voice_real.bin` (un de nos vrais `recording_*.wav`)

### 8.4 Bonus rapport — Heatmaps

Pour le rapport S2, une fois les tests verts, génération matplotlib :

```python
fig, ax = plt.subplots(1, 2, figsize=(10, 4))
ax[0].imshow(mfcc_bleu.T, aspect='auto');  ax[0].set_title('« bleu »')
ax[1].imshow(mfcc_rouge.T, aspect='auto'); ax[1].set_title('« rouge »')
plt.savefig('rapport/figures/mfcc_heatmap_bleu_vs_rouge.png')
```

Critère qualitatif : les deux heatmaps doivent être **visiblement différentes** à l'œil nu (sinon FP6 aura du mal à les classer).

## 9. Hors-périmètre (FP4 ne fait PAS)

- ✗ Entraînement du CNN (c'est FP5, en Python)
- ✗ Inférence sur le Due (c'est FP6, conv2D / dense / softmax en C)
- ✗ Allumage des LEDs (c'est FP6)
- ✗ Reconnaissance multi-mots dynamique sans appui bouton (hors spec ET)
- ✗ Robustesse au bruit de fond (c'est ET9, à valider en S2 sur l'ensemble pipeline)

## 10. Livrables FP4

- ✅ `lib/mfcc/` complet, compile et passe les tests unitaires
- ✅ `src/main.cpp` étendu avec FSM `COMPUTING_MFCC` + `DUMPING_MFCC`
- ✅ `scripts/mfcc_reference.py` (mirror Python Q15)
- ✅ `scripts/gen_tables.py` (génération tables partagées C/Python)
- ✅ `scripts/fp3_recv.py` étendu pour parser le bloc MFCC
- ✅ `scripts/test_mfcc_integration.py` (test bit-exact firmware ↔ Python)
- ✅ Section dédiée dans `rapport/chapters/06-fp4.tex` (conception, validation, heatmap, tableau timing/mémoire)
- ✅ 1 figure heatmap MFCC dans `rapport/figures/`

## 11. Exigences S2 couvertes

| ET | Critère | Validation |
|---|---|---|
| **ET5** | framing 256 / hop 128 | code `lib/mfcc/src/mfcc.cpp` boucle `for (frame=0..61)` |
| **ET6** | matrice 62×13 sur 1 s | `mfccMatrix[62][13]` dumpée via série, parsée par Python |

ET7-ET9 (CNN training, inférence, robustesse) sont couverts par FP5/FP6, hors-périmètre de cette spec.
