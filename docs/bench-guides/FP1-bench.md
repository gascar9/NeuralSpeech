# Guide de test bench — FP1 (ADC 32 kHz)

**Objectif** : valider **ET1** (ADC à 32 kHz fixe et précise via interruption timer).

**Temps estimé** : 20 min pour le minimum, 45 min pour la version complète avec Nyquist.

**Matériel requis**
- Arduino Due + câble USB **Programming Port**
- Générateur de fonctions Siglent SDG 805
- Oscilloscope Siglent SDS 1102CML+ (2 voies)
- PC avec monitor série (250000 bauds)

---

## Préparation (5 min)

1. **Upload firmware** : `pio run --target upload`
2. **Câblage** :
   - GBF → Y BNC → **A0 (Due)** + **CH2 (oscillo)**
   - **DAC0 (pin 66 Due)** → **CH1 (oscillo)**
   - Masses communes
3. **GBF** : sinusoïde 1 Vpp, offset 1.65 V, HI-Z
4. **Monitor série** 250000 bauds

---

## Test 1 — Reconstruction propre (5 min)

**But** : vérifier que le DAC reconstruit bien le signal injecté (preuve que la chaîne ADC → buffer → DAC fonctionne).

1. Injection **1 kHz** sur A0
2. Observer CH1 (DAC0) : doit montrer une sinusoïde 1 kHz reconstruite
3. Mesurer Vpp, Mean, Freq sur CH1
4. Mesurer Vpp, Freq sur CH2 pour comparer

**Attendu** : CH1 et CH2 à la même fréquence, Vpp cohérent.

**📸 Screenshot** : `assets/FP1/fp1_reconstruction_1kHz.jpeg` → **fait ✓** (`FP1Oscilloscope2.jpeg`).

---

## Test 2 — Mesure directe de Fe = 32 kHz (ET1 cœur — critère noté) (10 min)

**But** : prouver que $F_e$ vaut **31.25 µs ± 0.5 µs** entre deux échantillons consécutifs du DAC.

**Procédure**
1. Débrancher la voie d'entrée CH2 (on veut juste CH1 = DAC0 visible)
2. **Zoom fort en temps** : `M = 5 µs/div` ou `M = 10 µs/div`
3. Le signal DAC0 apparaît en **escalier** (sample-and-hold)
4. Activer les **curseurs temps** (bouton Cursors) et placer un curseur sur deux fronts successifs des marches
5. La mesure doit donner **ΔT = 31.25 µs ± 0.5 µs** (= 1 / 32 kHz)

**Validation croisée via console série** : lire la ligne `[FP1] Fe_reelle=XXXXX Hz` — doit donner **32012 ± 30 Hz**.

**📸 Screenshot** : `assets/FP1/fp1_te_mesure.jpeg` — **à faire** (zoom avec curseurs sur marches).

---

## Test 3 — Critère de Nyquist (bonus, 10 min)

**But** : démontrer le repliement spectral et motiver la nécessité de FP2.

1. **Injection 15 kHz** (juste sous Nyquist = 16 kHz) :
   - CH1 (DAC0) montre une sinusoïde 15 kHz reconstituée, bruitée mais clairement visible
   - **📸 Screenshot** : `assets/FP1/fp1_nyquist_15kHz.jpeg` — **à faire**

2. **Injection 17 kHz** (au-dessus de Nyquist) :
   - CH1 montre une sinusoïde à **15 kHz** (= 32 − 17) au lieu de 17 kHz
   - C'est le **repliement spectral** (aliasing) : la composante hors-bande est repliée dans la bande utile
   - Ce test montre pourquoi le filtre FP2 est indispensable
   - **📸 Screenshot** : `assets/FP1/fp1_nyquist_17khz_alias.jpeg` — **à faire**

---

## Résumé des critères de validation

| Critère | Valeur attendue | Test | État |
|---------|-----------------|------|------|
| **ET1** — reconstruction | CH1 = CH2 en forme et fréquence | Test 1 | ✓ fait |
| **ET1** — $T_e = 31{,}25$ µs | 31,25 µs ± 0,5 µs entre marches | Test 2 | **à faire** |
| Fe console série | 32012 ± 30 Hz | Test 2 | **à faire** (lecture ligne `[FP1]`) |
| Nyquist OK | 15 kHz propre | Test 3 | à faire |
| Repliement démo | 17 kHz → alias 15 kHz | Test 3 | à faire |

## Pièges

- ❌ Port USB Native au lieu de Programming → gigue ISR
- ❌ Oublier de zoomer fortement en temps : à 500 µs/div on ne voit pas les marches
- ❌ GBF sans HI-Z → amplitude réelle / 2

---

**Quand Tests 2 et 3 sont validés** → ET1 est complètement documenté pour la soutenance 1. Passer à FP2 avec le guide `FP2-bench.md`.
