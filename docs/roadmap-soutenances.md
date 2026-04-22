# NeuralSpeech — Roadmap Soutenances

**Date création** : 2026-04-22
**Notation** : 50 % rapport · 25 % soutenance 1 (10 pts, présentation 8' + démo 4' + questions 6') · 25 % soutenance 2 (démo + questions)
**Malus** : -12 si IA non fonctionnelle sur Arduino Due
**Dépôt BC** : OBLIGATOIRE avant chaque soutenance (sinon équipe non acceptée)

---

## Découpage stratégique

- **Soutenance 1** (mi-semestre) : démontrer FP1 + FP2 + FP3 fonctionnels sur l'Arduino Due. FP4 bien démarrée en bonus.
- **Soutenance 2** (fin) : chaîne complète FP1→FP6 en démo, CNN entraîné, reconnaissance ≥2 mots avec <5 % d'erreur.
- **Rapport** (50 %) : rédaction en continu, une section par FP avec conception + validation + résultats + schémas.

---

## Phase 0 — Setup bench & documentation (J0 → J2)

### Objectifs
- Montage physique opérationnel, documentation repo propre.

### À faire (coté hardware)
- [ ] Kit inventorié : Arduino Due, MAX9814, bouton poussoir, 3-4 LEDs, résistances 330 Ω, breadboard, jumpers
- [ ] Oscilloscope + générateur de fonctions réservés sur le créneau TP
- [ ] Multimètre pour vérifs alim/continuité

### À faire (côté repo)
- [ ] `src/main.cpp` minimal compile + téléverse → vérifie PlatformIO + driver USB Due (bouton ERASE/RESET connu)
- [ ] Créer `docs/hardware/` pour les schémas de câblage
- [ ] Ajouter script Python dans `scripts/` pour conversion série → .wav (utile FP3)

### Photos / captures rapport
- [ ] Photo du bench complet (avec Arduino, micro, PC, oscillo)
- [ ] Photo du kit déballé avec label des composants

### Livrable
- [ ] Compile & upload OK → commit initial "chore: bootstrap bench"

---

## Phase 1 — FP1 Acquisition ADC 32 kHz (J2 → J7) — ET1

### Objectifs techniques
- ET1 : ADC 32 kHz via interruption timer, échantillonnage fixe et précis
- Code propre avec `#define FE 32000` (pas de magic numbers)

### À faire (délégation à `signal-chain`)
- [ ] Configurer TC (Timer Counter) pour déclencher ADC à 32 kHz
- [ ] ISR ultra-courte qui pousse dans buffer circulaire
- [ ] Activer DAC0 pour reconstruire le signal (validation oscillo)

### Tests bench à faire nous-mêmes
- [ ] Injecter une sinusoïde 1 kHz (générateur) sur A0
- [ ] Sonde oscillo sur DAC0 : forme reconnaissable ?
- [ ] Mesurer l'écart entre 2 samples DAC → **doit être 31.25 µs**
- [ ] Test Nyquist : monter la fréquence d'entrée jusqu'à 16 kHz, vérifier absence de repliement
- [ ] Note dans carnet : fréquence mesurée, stabilité, écart-type si possible

### Photos / captures rapport
- [ ] Photo montage MAX9814 câblé sur A0 + alim
- [ ] Capture oscilloscope : sinusoïde entrée vs DAC reconstruit (cursors sur 2 samples → 31.25 µs visible)
- [ ] Capture oscilloscope : signal 16 kHz (limite Nyquist)

### Livrable Soutenance 1
- [ ] Schéma fonctionnel FP1 (diagramme blocs)
- [ ] Extrait de code ISR + init TC dans le rapport
- [ ] Slide "FP1 validée" avec capture oscillo

---

## Phase 2 — FP2 Filtrage + subsampling (J7 → J12) — ET2, ET3

### Objectifs techniques
- ET2 : atténuation ≥ 30 dB au-dessus de 4 kHz
- ET3 : filtrage < 31 µs par échantillon
- Subsampling /4 : 32 kHz → 8 kHz

### À faire (délégation à `signal-chain`)
- [ ] Choisir RIF vs RII (recommandation : RIF Hamming ordre ~48 pour phase linéaire)
- [ ] Calculer coefficients (script Python scipy.signal.firwin, commit dans `scripts/`)
- [ ] Implémenter filtre en C avec buffer circulaire power-of-2
- [ ] Ajouter mesure `micros()` autour du filtrage → log série

### Tests bench à faire nous-mêmes
- [ ] Balayer générateur 100 Hz → 16 kHz en notant le niveau DAC
- [ ] Tracer la réponse en fréquence manuellement → vérifier -30 dB à 4 kHz
- [ ] Vérifier temps de filtrage affiché console < 31 µs (idéalement < 20 µs)
- [ ] Test avec signal voix parlée → écouter la sortie filtrée via DAC sur haut-parleur amplifié

### Photos / captures rapport
- [ ] Plot Python de la réponse théorique du filtre (bien annoter -30 dB @ 4 kHz)
- [ ] Capture console série avec temps de filtrage mesuré
- [ ] Spectre FFT avant/après filtre (Audacity "Plot Spectrum" sur un enregistrement)

### Livrable Soutenance 1
- [ ] Slide "Choix du filtre" avec justification (RIF vs RII, ordre retenu)
- [ ] Graphe réponse fréquentielle
- [ ] Capture "temps de traitement < 31 µs"

---

## Phase 3 — FP3 Validation Audacity (J12 → J14) — ET4

### Objectifs techniques
- Transférer 1 s d'audio via série → reconstruire .wav → écouter "Électronique" clairement

### À faire (délégation à `signal-chain`)
- [ ] Routine qui dump 8000 échantillons int16 après appui bouton
- [ ] Script Python `scripts/serial_to_wav.py` qui lit la série et génère un .wav

### Tests bench à faire nous-mêmes
- [ ] Enregistrer "Électronique" 5 fois à différentes distances du micro
- [ ] Ouvrir dans Audacity → écouter → confirmer intelligibilité
- [ ] Comparer spectrogramme avant filtrage (si on a gardé le brut 32 kHz quelque part) et après (8 kHz)

### Photos / captures rapport
- [ ] Screenshot Audacity avec le mot "Électronique" visible (waveform + spectrogramme)
- [ ] Diagramme du protocole série (header, payload, footer, baudrate)
- [ ] Si possible : courte vidéo du transfert (juste pour la démo, pas pour le rapport)

### Livrable Soutenance 1
- [ ] **Démo live** : bouton → 1 s → .wav → lecture Audacity
- [ ] Fichier .wav committé dans `docs/audio-samples/` pour archive

---

## === SOUTENANCE 1 === (mi-semestre, date à confirmer)

### Prépa présentation (8 min)
- [ ] Diaporama 10-12 slides max (pas un résumé du rapport)
- [ ] Slide 1 : titre + équipe
- [ ] Slide 2 : diagramme fonctionnel complet (FP1→FP6, zones faites en vert)
- [ ] Slide 3 : schéma câblage hardware (MAX9814 → A0)
- [ ] Slides 4-5 : FP1 — ADC (algo, captures oscillo)
- [ ] Slides 6-7 : FP2 — filtrage (choix, réponse, temps mesuré)
- [ ] Slide 8 : FP3 — Audacity (screenshot + extrait son)
- [ ] Slide 9 : prochaines étapes (FP4-FP6, plan Soutenance 2)
- [ ] Slide 10 : questions

### Prépa démo (4 min)
- [ ] Scénario démo écrit (qui fait quoi, dans quel ordre)
- [ ] PC + Arduino + câbles prêts, Audacity ouvert
- [ ] Adaptateur USB-C → HDMI dans le sac
- [ ] Test projecteur la veille

### Prépa questions (6 min)
- [ ] Slides back-up : code ISR, formule filtre, calculs Nyquist
- [ ] Anticiper "pourquoi RIF et pas RII ?", "comment vérifier Fe ?", "capacité SRAM ?"

### Dépôt BC
- [ ] Push final du code sur BC **avant** l'heure limite
- [ ] Vérifier présence : rapport intermédiaire, code source, slides

### Checklist pré-soutenance
- [ ] Répéter la présentation en conditions (chrono)
- [ ] PC chargé, Arduino fonctionnel testé 1 h avant
- [ ] Chaque membre du groupe sait parler d'au moins 1 FP

---

## Phase 4 — FP4 MFCC 13 coefficients (J15 → J21) — ET5, ET6

### Objectifs techniques
- Frames 256 avec hop 128 (ET5)
- 13 MFCCs par frame sur 1 s d'audio → matrice 62×13 (ET6)

### À faire (délégation à `mfcc-expert`)
- [ ] Préemphase α=0.97
- [ ] Lookup table Hamming
- [ ] FFT 256 points (`arduinoFFT` ou manuel)
- [ ] Banc MEL 26 filtres triangulaires
- [ ] DCT-II → garder 13 premiers coefs
- [ ] Fonction qui produit 62×13 floats à partir de 8000 samples

### Tests bench à faire nous-mêmes
- [ ] Injecter sinusoïde 1 kHz pure → vérifier pic FFT au bin 32 (freq bin = F × N / Fs = 1000 × 256 / 8000 = 32) ✓
- [ ] Dump matrice MFCC via série, comparer à un calcul Python de référence (librosa ou script fourni Pôle Électronique)
- [ ] Écart entre C et Python < 5 % acceptable

### Photos / captures rapport
- [ ] Plot matplotlib : signal brut → préemphase → fenêtré Hamming → FFT → MEL → MFCC (pipeline étape par étape)
- [ ] Heatmap de la matrice 62×13 pour "bleu" vs "rouge" (on doit voir une différence visuelle)
- [ ] Tableau comparatif MFCC C vs Python

### Livrable
- [ ] Fonction `compute_mfcc(samples, out_matrix)` documentée
- [ ] Commit `scripts/mfcc_reference.py` pour la validation

---

## Phase 5 — FP5 CNN training Python (J21 → J28) — ET7, ET8

### Objectifs techniques
- Dataset ≥ 100 éléments (50 × 2 mots min)
- MSE < 0.05 sur test
- Pas de back-prop sur test (interdit)

### À faire (délégation à `ml-expert`)
- [ ] Script `scripts/train.py` : charge dataset, normalise, split, entraîne
- [ ] Architecture CNN light (~10k params, voir spec)
- [ ] Augmentation : bruit gaussien, décalage temporel
- [ ] Sauvegarder `.h5` + dump poids en `include/model_weights.h`

### Tests à faire nous-mêmes
- [ ] Loop d'entraînement jusqu'à convergence (courbes loss)
- [ ] Vérifier MSE sur test < 0.05
- [ ] Matrice de confusion sur test
- [ ] Test avec enregistrements fait par nous (pas seulement dataset fourni)

### Photos / captures rapport
- [ ] Courbes loss/accuracy train + val
- [ ] Architecture CNN en schéma (netron.app)
- [ ] Matrice de confusion
- [ ] Tableau "epochs, lr, batch size, MSE train, MSE test"

### Livrable
- [ ] `model_weights.h` committé
- [ ] Script training reproductible (seed fixée)

---

## Phase 6 — FP6 Inférence embarquée (J28 → J35) — ET9

### Objectifs techniques
- Inference en C sur Arduino Due
- < 5 % erreur sur ≥ 10 mots prononcés
- Robustesse bruit de fond

### À faire (délégation à `ml-expert`)
- [ ] Implémenter couches en C : Conv2D, ReLU, MaxPool, Flatten, Dense, Softmax
- [ ] Pipeline complet : bouton → 1 s → MFCC → inference → LED
- [ ] Sanity check : 1 sample connu donne la même prédiction que Python (écart < 1e-5)

### Tests bench à faire nous-mêmes
- [ ] Enregistrer 50 fois chaque mot dans conditions calmes
- [ ] Enregistrer 20 fois avec bruit fond (musique, conversations)
- [ ] Calculer taux d'erreur → cible < 5 %
- [ ] Tester avec locuteurs différents (les 4 membres du groupe)
- [ ] Tester à différentes distances du micro (10 cm, 30 cm, 1 m)
- [ ] Tester avec mots non appris → système doit idéalement ne rien allumer (ou allumer une LED "inconnu")

### Photos / captures rapport
- [ ] Photo démo : bouton pressé → LED allumée correspondante
- [ ] Vidéo (8-15 s) de reconnaissance en conditions réelles
- [ ] Tableau taux de reconnaissance par mot / par locuteur / par condition (calme/bruit)

### Livrable
- [ ] Code inference commité
- [ ] Vidéo pour rapport et démo

---

## === SOUTENANCE 2 === (fin semestre)

### Prépa (similaire à Soutenance 1 mais focus démo)
- [ ] Démo live en conditions réelles (bruit de la salle inclus)
- [ ] Scénario : chaque membre prononce un mot → système répond correctement
- [ ] Plan B si système échoue : vidéo backup + discussion honnête sur les limites
- [ ] Slides back-up avec architecture CNN, mesures, taux d'erreur

### Dépôt BC
- [ ] Code final propre (pas de dead code, commentaires clairs)
- [ ] Rapport final
- [ ] Slides

---

## Rédaction du rapport (EN CONTINU — 50 % de la note)

### Template Moodle "La Toolbox" — OBLIGATOIRE

### Structure type
- [ ] Page de garde + équipe
- [ ] Résumé / abstract
- [ ] Introduction (sujet, équipe, découpage)
- [ ] Diagramme fonctionnel (FP1→FP6)
- [ ] Section par FP : conception (choix, justification) + validation (mesures, captures, comparaisons)
- [ ] Gestion projet : Gantt, répartition des tâches, difficultés rencontrées
- [ ] Conclusion + perspectives (ex : ajout OLED, Bluetooth, multi-langues)
- [ ] Bibliographie / références
- [ ] Annexes : code source clé, datasheets MAX9814/SAM3X8E

### Rythme de rédaction conseillé
- [ ] Après chaque phase : rédiger la section correspondante pendant que c'est frais
- [ ] Chaque membre responsable d'1 FP pour le rapport
- [ ] Relecture croisée avant dépôt

### Pièges à éviter
- Plagiat (vérif `Le pôle électronique` doc)
- Slides = résumé du rapport (interdit par le barème)
- Retards de dépôt BC → ne pas être accepté en soutenance

---

## Photos/captures à collectionner — checklist globale

Un dossier `docs/assets/` pour centraliser. À prendre au fil de l'eau :

### Hardware
- [ ] Bench complet
- [ ] Câblage MAX9814 zoomé
- [ ] Câblage bouton + LEDs

### Oscilloscope
- [ ] Signal analogique entrée vs DAC reconstruit (ET1)
- [ ] Mesure période 31.25 µs (cursors)
- [ ] Signal filtré vs non filtré (démo ET2)

### Console / IDE
- [ ] Temps filtrage mesuré via micros() (ET3)
- [ ] Matrice MFCC dumpée (ET6)

### Audacity
- [ ] Waveform "Électronique" (ET4)
- [ ] Spectrogramme filtré 0-4 kHz clean
- [ ] Spectrum avant/après filtre (Plot Spectrum)

### Python / matplotlib
- [ ] Réponse fréquentielle filtre (avec -30 dB annoté)
- [ ] Pipeline MFCC step-by-step (6 subplots)
- [ ] Heatmap MFCC bleu vs rouge
- [ ] Courbes training CNN
- [ ] Matrice de confusion
- [ ] Architecture CNN

### Démo finale
- [ ] Photo système en fonctionnement avec LED allumée
- [ ] Vidéo 10-15 s reconnaissance live

---

## Meta — gestion d'équipe

- [ ] Répartition par FP dans un Gantt (Notion/Excel/Trello au choix)
- [ ] Point hebdo 15 min pour unblock
- [ ] Chaque membre push régulièrement (pas de gros dump en fin de semaine)
- [ ] Relecture croisée du rapport avant chaque dépôt

---

## Raccourcis utiles pendant le projet

- Tâche mono-FP → appelle directement l'agent (ex : "configure l'ADC 32 kHz" → `signal-chain`)
- Tâche multi-FP → `ns-orchestrator`
- Nouvelle brique (OLED, BT, autre mot) → `agent-smith`
- Question rapport/méthode → moi directement
