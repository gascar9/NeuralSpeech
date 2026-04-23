# Guide de test bench — FP3 (Validation Audacity)

**Objectif** : valider **ET4** — un enregistrement de 3 secondes du mot « Électronique » doit être restitué de manière claire dans Audacity.

**Temps estimé** : 30 min pour le premier enregistrement propre, 15 min par enregistrement suivant.

**Matériel requis**
- Arduino Due + câble USB **Programming Port**
- **Bouton poussoir** (n'importe lequel — 2 broches suffisent)
- 2 fils jumper mâle-femelle (ou pinces croco)
- **Microphone MAX9814** (cette fois on branche vraiment le micro — exit le GBF)
- 3 fils jumper pour le micro (VCC / GND / OUT)
- PC avec Python 3 + pip + **Audacity** installé

---

## Étape 1 — Câblage bouton + microphone (10 min)

Le GBF n'est plus utile en FP3 (on enregistre la voix, pas un signal test). Débranche-le pour libérer A0.

### Bouton sur **D2**

- Une broche du bouton → **D2** (colonne "digital", coin bas-gauche de la Due — 3e pin en partant de GND)
- Autre broche du bouton → **GND** (n'importe lequel des 2-3 GND dispo)
- **Pas besoin de résistance** : le firmware active le pull-up interne (`INPUT_PULLUP`)
- Appui = LOW (bouton ferme le circuit vers GND), relâché = HIGH (pull-up)

### Microphone MAX9814 sur A0

- MAX9814 **VCC** → Due **3V3** (⚠️ **pas 5V**, la Due est 3.3V)
- MAX9814 **GND** → Due **GND**
- MAX9814 **OUT** → Due **A0** (même broche que le GBF avant)
- Laisse **GAIN** et **AR** flottants (→ gain 60 dB par défaut, AGC actif — parfait pour la voix)

### Schéma récap

```
Arduino Due            MAX9814               Bouton
─────────────          ───────               ──────
3V3    ─────────────── VCC
GND    ─────────────── GND ──┐
GND    ────────────────────────┘────────── broche B
A0     ─────────────── OUT
D2     ──────────────────────────────────── broche A

USB Programming → PC (alim + série)
```

**Laisse DAC0/DAC1 débranchés** pour FP3 — on ne se sert pas de l'oscilloscope ici.

---

## Étape 2 — Upload firmware + sanity check (3 min)

```bash
pio run -e due --target upload
```

Le monitor série à 250000 baud doit afficher au démarrage :
```
=== NeuralSpeech FP1+FP2+FP3 — ADC 32 kHz + filtre RIF + buf 8 kHz + bouton D2 ===
*** DAC0 = signal BRUT (avant filtre)  --> CH1 oscillo ***
*** DAC1 = signal FILTRE (apres FP2)   --> CH2 oscillo ***
FE             : 32000 Hz
...
FP3 button pin : D2
FP3 capture    : 24000 samples @ 8 kHz (3 s)
FP3 pret — appuyez D2 pour enregistrer.
...
[FP1] Fe_reelle=32012 Hz | samples=32012 | buf_used=0/512
[FP2] filter_us_avg=11.29 max=14.5 | buf8k_used=X/2048 | taps=40 | fp3=IDLE
```

**Le critère de succès** : la ligne `fp3=IDLE` à la fin de la ligne `[FP2]`. Ça confirme que la state machine FP3 est active et en attente du bouton.

Test du bouton : appuie une fois sur D2. Tu dois voir :
```
[FP3] Capture armee -- parlez maintenant (3 s)
[FP2] ... | fp3=ARMING (8000/24000)
[FP2] ... | fp3=ARMING (16000/24000)
[FP3] --- DEBUT CAPTURE WAV ---
<caractères bizarres = les 16008 octets binaires>
[FP3] --- FIN CAPTURE WAV ---
[FP3] Pret pour la prochaine capture (appuyer D2).
[FP2] ... | fp3=IDLE
```

Les caractères bizarres après `DEBUT CAPTURE` sont **normaux** — c'est le bloc binaire int16 LE que le monitor affiche comme il peut. **Ne t'affole pas**, c'est juste que le monitor texte ne sait pas afficher du binaire proprement.

---

## Étape 3 — Enregistrement via script Python (recommandé, 5 min)

### Installer pyserial (une seule fois)
```bash
pip install pyserial
```

### Fermer le monitor série PlatformIO

**Important** : un seul programme à la fois peut ouvrir le port série. Si le monitor PlatformIO est ouvert, ferme-le (Ctrl+C ou clic sur la poubelle à côté du terminal).

### Lancer le récepteur
```bash
python3 scripts/fp3_recv.py
```

Sortie attendue :
```
Port série : /dev/tty.usbmodem1101 @ 250000 baud
En attente du header magic (appuyez sur D2 de l'Arduino Due) ...
Délai max : 120 s
```

### Enregistrer le mot
1. Approche le micro à 10-20 cm de ta bouche
2. **Appuie sur D2** (un court appui suffit)
3. **Prononce "Électronique"** clairement (tu as **3 secondes** à partir de l'appui)
4. Le script détecte le header, lit les 48 Kio de PCM, écrit le WAV

Sortie finale :
```
Header OK. Annonce : 24000 samples.
Payload PCM reçu : 48000 octets en ~1.9 s (~24 Kio/s)

✓ WAV écrit : /Users/.../recording_20260422_193015.wav  (47 Kio)
  Mono, 16 bits, 8000 Hz, 3.00 s

Ouvre-le dans Audacity :  File → Open → recording_20260422_193015.wav
```

Le `.wav` est écrit dans le **répertoire courant** (sauf si tu passes `--out`). Typiquement à la racine du projet.

### Options utiles

```bash
# Nommer le fichier
python3 scripts/fp3_recv.py --out electronique_essai1.wav

# Forcer un port spécifique (si auto-detect rate)
python3 scripts/fp3_recv.py --port /dev/tty.usbmodem1101

# Allonger le délai d'attente (défaut 120 s)
python3 scripts/fp3_recv.py --timeout 300
```

---

## Étape 4 — Écoute et validation dans Audacity ⭐

### Ouverture du fichier

1. Lancer **Audacity**
2. **File → Open** → sélectionner le `.wav` produit
3. Tu vois apparaître **une seule piste** (mono), **3 secondes** de durée

### Ce que tu dois voir/entendre

**Onde temporelle** :
- La forme d'onde doit avoir des "zones d'énergie" visibles correspondant aux syllabes du mot
- « É-lec-tro-nique » → 4 syllabes distinctes (tu as 3 s, donc tu peux parler sans te presser ou même dire le mot 2-3 fois)
- Amplitude typique : ±10 à ±20 % de la pleine échelle (±2048 sur int16 → visible dans Audacity)
- Pas de **clipping** (pas de carré plat en haut ou en bas de l'onde)

**Lecture audio** :
- Bouton **Play** (espace) → tu dois **reconnaître distinctement** le mot « Électronique »
- Si tu entends juste du bruit granuleux ou un bourdonnement : problème de câblage micro ou gain
- Si tu entends le mot mais très faible : augmente le GAIN du MAX9814 (broche GAIN à GND pour +10 dB)

### Captures à prendre pour le rapport

1. **Vue complète de l'onde temporelle**
   - Zoom : `Ctrl+F` (Fit in Window) pour voir les 3 secondes
   - Capture écran de la fenêtre Audacity entière
   - Nommer : `assets/FP3/fp3_audacity_waveform.png`
   - **Doit montrer** : les 4 syllabes distinctes, l'amplitude cohérente, pas de clipping

2. **Spectrogramme (très valorisé pour la soutenance)**
   - Clique sur le nom de la piste (petit menu à gauche) → **Spectrogram**
   - L'affichage passe en spectrogramme coloré (temps en x, fréquence en y, énergie en couleur)
   - Tu vois les **formants vocaux** comme des bandes horizontales lumineuses
   - Capture écran
   - Nommer : `assets/FP3/fp3_audacity_spectrogram.png`
   - **Doit montrer** : bandes horizontales bien contrastées dans la plage 0–4 kHz, peu d'énergie au-dessus de 4 kHz (grâce au filtre FP2 !)

3. **Plot du spectre (bonus)**
   - Sélectionne toute la piste (`Ctrl+A`)
   - Menu **Analyze → Plot Spectrum...**
   - Algorithm: Spectrum, Function: Hann, Size: 2048
   - Tu vois la densité spectrale de puissance
   - Capture écran
   - Nommer : `assets/FP3/fp3_audacity_spectrum.png`
   - **Doit montrer** : énergie concentrée 200 Hz – 3 kHz (formants voix), chute nette après 4 kHz

### Vérifications auditives

- **Play normal** : « Électronique » clair et intelligible
- **Play au ralenti** (`Effect → Change Speed` → 50%) : on doit encore reconnaître le mot
- **Pas de clics ni de saccades** : preuve que le buffer 8 kHz n'a pas été overflow

---

## Étape 5 — Fallback sans Python (Audacity import RAW direct)

Si pour une raison X tu ne peux pas faire tourner Python côté PC, Audacity sait importer directement des bytes bruts. C'est plus bricolé mais ça dépanne.

### Capturer le flux série dans un fichier binaire

```bash
# Fermer le monitor PlatformIO d'abord
cat /dev/tty.usbmodem1101 > capture.bin &
CAT_PID=$!
# appuie sur le bouton, attends 2 secondes
kill $CAT_PID
```

### Import dans Audacity

1. **File → Import → Raw Data...**
2. Sélectionner `capture.bin`
3. Dialog qui apparaît :
   - **Encoding** : `Signed 16-bit PCM`
   - **Byte order** : `Little-endian`
   - **Channels** : `1 Channel (Mono)`
   - **Sample rate** : `8000` Hz
   - **Start offset** : **à trouver manuellement** (voir ci-dessous)
   - **Amount to import** : `100%`

### Trouver le Start offset

Le fichier `capture.bin` contient **tout** ce qui est sorti de la série, incluant les messages texte `[FP1]`, `[FP2]`, `[FP3]`. Les octets audio sont encadrés par les magic bytes `AA 55 AA 55` (début) et `DE AD BE EF` (fin).

Méthode rapide avec `xxd` (macOS / Linux) :
```bash
xxd capture.bin | grep -n "aa 55 aa 55"
```

Ça te dit la ligne où commencent les magic bytes. Calcule l'offset en octets (chaque ligne = 16 octets). Le vrai audio commence **8 octets après** le début du magic (4 header + 4 length).

**C'est pénible.** Utilise le script Python plutôt, sauf urgence.

---

## Résumé — ce qu'il faut livrer pour valider ET4

| Élément | Fichier | Statut |
|---------|---------|--------|
| Firmware FP3 fonctionnel | `src/main.cpp` | ✅ déjà dans le commit |
| Script Python récepteur | `scripts/fp3_recv.py` | ✅ déjà dans le commit |
| Fichier audio Électronique | `assets/FP3/electronique.wav` | **à faire par Gaspard** |
| Capture Audacity waveform | `assets/FP3/fp3_audacity_waveform.png` | **à faire** |
| Capture Audacity spectrogramme | `assets/FP3/fp3_audacity_spectrogram.png` | **à faire (recommandé)** |

**Critère minimal ET4** : le `.wav` s'ouvre dans Audacity et on entend « Électronique » distinctement → **ET4 validé**.

---

## Pièges courants

- ❌ **GBF encore branché sur A0** : enlève-le, il bouffe le signal du micro
- ❌ **5V sur VCC du MAX9814** : VCC de la Due = 3V3 max, branche-le là
- ❌ **Pas de masse commune** : le micro captera du bruit 50 Hz — vérifie que GND micro = GND Due
- ❌ **Monitor PlatformIO ouvert en même temps que Python** : conflit d'ouverture de port, un seul à la fois
- ❌ **Bouton tenu appuyé trop longtemps** : anti-rebond 50 ms mais la state machine ne re-déclenche qu'après un relâche. Appui court suffisant.
- ❌ **Mot dit trop tôt** : l'enregistrement commence à l'appui du bouton, pas avant. Appuie **puis** parle dans les ~3.2 s qui suivent.
- ❌ **Distance micro trop grande** : reste à 10-20 cm pour un bon signal / bruit

## Si le WAV est silencieux (amplitude quasi-nulle)

Le MAX9814 a un AGC (contrôle automatique de gain) qui peut mettre quelques secondes à monter. Fais 2-3 essais en parlant fort. Si c'est toujours silencieux :
1. Vérifie à l'oscilloscope sur DAC0 (signal brut) que la parole fait vraiment bouger la trace
2. Si oui, le problème est dans le filtre ou la chaîne serial → contacte-moi avec le WAV
3. Si non, problème micro (câblage, alim, ou micro mort)

## Si le WAV clip (écrêté, saturation)

Inverser le MAX9814 : mettre le broche GAIN à **Vdd** pour forcer le gain à 40 dB au lieu de 60 dB. Ça divise la sensibilité par ~3.

---

**Quand tu as ton premier WAV propre + les 2-3 captures Audacity** → ET4 validé, FP3 close, on peut attaquer FP4 (MFCC).
