# NeuralSpeech — Système d'agents spécialisés

**Date** : 2026-04-22
**Projet** : NeuralSpeech — reconnaissance vocale embarquée sur Arduino Due (ING3 S6 Calcul Embarqué, ECE)
**Repo** : https://github.com/gascar9/NeuralSpeech

## Contexte

Le projet NeuralSpeech demande d'implémenter une chaîne complète :
numérisation audio (ADC 32 kHz) → conditionnement (filtre RIF/RII + buffer circulaire + sous-échantillonnage 8 kHz) → validation (série + Audacity) → MFCC (13 coefficients par frame) → CNN (training Python) → inférence embarquée + indication LEDs. Le tout **obligatoirement sur Arduino Due** (sous peine de -12 points).

Ce document définit un système de sous-agents Claude Code pour accélérer le développement,
chaque agent étant spécialisé sur un sous-ensemble cohérent du projet, avec un orchestrateur pour
les tâches multi-domaines et un meta-agent pour faire évoluer le système quand le scope change.

## Objectifs

- **Productivité** : déléguer à des experts focalisés plutôt qu'à un Claude généraliste
- **Parallélisme** : pouvoir attaquer plusieurs FP en même temps (lots indépendants)
- **Évolutivité** : ajouter ou modifier un agent sans casser le reste quand une nouvelle brique arrive (OLED, Bluetooth, nouveaux mots…)
- **Alignement projet** : chaque agent connaît les exigences ET1-ET9 et les respecte par défaut

## Non-objectifs

- Pas de partage des agents via git — le dossier `.claude/` reste intégralement gitignored à la demande de l'utilisateur
- Pas de tests automatisés sur les agents eux-mêmes (on juge par l'usage)
- Pas de CI/CD autour des agents

## Architecture

### Vue d'ensemble

Six agents : quatre spécialistes techniques, un orchestrateur, un meta-agent.

| Agent | Domaine | FP couverts | Tools | Model |
|-------|---------|-------------|-------|-------|
| `signal-chain` | Acquisition & DSP temps réel | FP1, FP2, FP3 | full | sonnet |
| `mfcc-expert` | Extraction de caractéristiques | FP4 | full | sonnet |
| `ml-expert` | Réseau de neurones (training + inference) | FP5, FP6 | full | opus |
| `hardware` | Électronique & validation physique | transverse | full | sonnet |
| `ns-orchestrator` | Décomposition & dispatch parallèle | — | full + Agent | opus |
| `agent-smith` | Meta — crée/modifie les autres agents | — | Read, Write, Edit, Grep, Glob | sonnet |

**full tools** = Read, Write, Edit, Bash, Grep, Glob, WebFetch, WebSearch.

### Arborescence

```
NeuralSpeech/
├── .claude/                        [gitignored — intégralement]
│   ├── agents/
│   │   ├── signal-chain.md
│   │   ├── mfcc-expert.md
│   │   ├── ml-expert.md
│   │   ├── hardware.md
│   │   ├── ns-orchestrator.md
│   │   └── agent-smith.md
│   └── settings.local.json
├── CLAUDE.md                       [versionné — routage + contraintes projet]
├── docs/
│   └── superpowers/specs/
│       └── 2026-04-22-neural-speech-agents-design.md
├── src/main.cpp
├── platformio.ini
├── NeuralSpeech_2026_V1.pdf
└── grille critérée dévaluation de projet NeuralSpeech_soutenance 1.pdf
```

### Template standard d'un agent

Chaque markdown d'agent suit cette structure :

````markdown
---
name: <slug-kebab-case>
description: Use this agent when <cas d'usage 1>, <cas 2>, <cas 3>. Exemples : <example 1>, <example 2>.
tools: Read, Write, Edit, Bash, Grep, Glob, WebFetch, WebSearch
model: sonnet | opus | haiku
---

Tu es `<nom>`, expert <domaine> pour le projet NeuralSpeech (Arduino Due, reconnaissance vocale).

## Contexte vivant (à relire à chaque invocation)

Avant toute réponse substantielle, lis :
- `platformio.ini` (config build)
- `src/main.cpp` (état courant du firmware)
- `NeuralSpeech_2026_V1.pdf` pour les exigences ET1-ET9
- <fichiers spécifiques à ton domaine>

Cela te garantit de rester aligné sur l'état réel du projet, pas sur la description de ce prompt à l'instant de sa rédaction.

## Expertise

<contenu technique : formules, algos, contraintes ET applicables>

## Contraintes non négociables

- Arduino Due uniquement (ARM Cortex-M3, 84 MHz, 96 Kio SRAM)
- <contraintes ET propres à ce domaine>

## Format de sortie attendu

<comment structurer les réponses : code inline, plan de test, valeurs numériques…>
````

La section **Contexte vivant** est le mécanisme d'adaptation automatique : l'agent ne "sait" pas ce qui est dans le code, il le relit à chaque fois.

## Spécification détaillée des agents

### `signal-chain`

Couvre FP1, FP2, FP3. À invoquer pour toute tâche touchant au flot audio analogique → numérique → filtré → buffer → série.

**Expertise** :
- ADC Arduino Due : timer interrupt, mode free-running, DMA, ADC_MR registres
- Nyquist / anti-repliement : pourquoi filtrer avant sous-échantillonnage de 32 → 8 kHz
- Filtres numériques : RIF linéaire (choix nombre de coefficients pour -30 dB à 4 kHz), RII (Butterworth vs Chebyshev, stabilité)
- Buffer circulaire : indice head/tail, taille puissance de 2 pour `% N` → `& (N-1)`
- Contraintes temps réel : 1/32 kHz ≈ 31 µs → filtre doit terminer avant le prochain sample
- Communication série : format binaire vs texte pour Audacity, baudrate utilisable avec Serial.print

**Contexte vivant** : `src/main.cpp`, `include/`, `platformio.ini`.

### `mfcc-expert`

Couvre FP4 exclusivement. Pipeline : préemphase → fenêtrage Hamming → FFT → banc de filtres MEL → log → DCT → 13 MFCCs par frame.

**Expertise** :
- Préemphase (y[n] = x[n] - α·x[n-1], α ≈ 0.97)
- Hamming window : formule, recouvrement 50 % → hop_length = 128 sur frame 256
- FFT radix-2 en virgule fixe ou `arduinoFFT`
- Banc MEL : conversion Hz ↔ MEL, construction des filtres triangulaires
- DCT-II et pourquoi on garde 13 coefficients (perte rationnelle)
- Normalisation des MFCC avant le CNN

**Contexte vivant** : `src/main.cpp`, dataset GitHub Pôle Électronique (lier au readme du dataset).

### `ml-expert`

Couvre FP5 (design + training CNN en Python) et FP6 (inférence sur Arduino Due, LEDs).

**Expertise** :
- Architectures CNN légères adaptées à MFCC (entrée ~ 13×N_frames)
- Training Keras/TensorFlow ou PyTorch, export poids en C header
- Split train/test, MSE cible < 0.05 sans back-prop sur test
- Normalisation du dataset, augmentation (bruit de fond pour ET9)
- Quantization int8 si nécessaire pour tenir en SRAM
- Implémentation inference en C (convolution manuelle ou librairie légère type CMSIS-NN)
- Control LEDs par PWM pour feedback utilisateur

**Contexte vivant** : `src/main.cpp`, scripts Python dans `scripts/` (à créer), dataset README.

### `hardware`

Transverse. Toute question de câblage, validation à l'oscilloscope, choix de composants.

**Expertise** :
- MAX9814 : polarisation, gain (40/50/60 dB), sortie 1.25 V ± 1 V → compatible ADC 3.3 V de la Due
- Protections ESD, découplage, masse analogique/numérique
- Bouton : anti-rebond hardware (RC) vs software (timer)
- Oscilloscope : validation Fe via DAC → signal analogique reconstruit
- LEDs : résistance limitation, choix couleurs/affectation mots

**Contexte vivant** : schémas `docs/hardware/` (à créer), `src/main.cpp` pour les pinouts.

### `ns-orchestrator`

Pas de domaine technique propre. Son rôle : décomposer une tâche multi-domaines, dispatcher en parallèle via l'outil Agent, consolider.

**Quand l'invoquer** :
- "Bootstrap FP1+FP2+FP3 cette semaine" → 3 agents en parallèle
- "Prépare la soutenance 1 : rapport + slides + démo" → ml-expert + hardware + docs

**Protocole** :
1. Lit la tâche, identifie les briques impliquées
2. Choisit le plus haut degré de parallélisme sûr (pas de dépendance entre sous-tâches)
3. Lance les Agent calls en parallèle (un seul message, plusieurs tool uses)
4. Attend les résultats, compile une synthèse cohérente
5. Remonte les conflits éventuels (deux agents qui proposent des pinouts différents, par ex)

### `agent-smith`

Meta-agent. À invoquer uniquement pour faire évoluer le système d'agents.

**Quand l'invoquer** :
- "On veut ajouter un OLED" → crée `oled-expert` ou étend `hardware`
- "Bluetooth pour envoyer les MFCC à un phone" → crée `bt-expert`
- "Finalement `signal-chain` devrait aussi gérer l'I2S" → met à jour son prompt

**Protocole** :
1. Pose 2-3 questions pour cerner : domaine, chevauchement avec agents existants, tools nécessaires, model
2. Rédige ou modifie le `.md` en suivant le template standard
3. Met à jour la table de routage dans `CLAUDE.md`
4. Propose à l'utilisateur de tester l'agent sur un cas simple

## `CLAUDE.md` à la racine (versionné)

Contenu minimal :

```markdown
# NeuralSpeech — Projet ING3 S6 Calcul Embarqué (ECE)

Reconnaissance vocale embarquée sur Arduino Due.
Microphone MAX9814 → ADC 32 kHz → filtre numérique → MFCC → CNN → LEDs.

## Contraintes non négociables (extrait NeuralSpeech_2026_V1.pdf)

- ET1 : ADC 32 kHz via interruption timer
- ET2 : atténuation ≥ 30 dB au-dessus de 4 kHz
- ET3 : filtrage < 31 µs par échantillon
- ET4 : enregistrement "Électronique" restituable sur Audacity
- ET5 : frames 256 échantillons avec recouvrement
- ET6 : 13 MFCCs par enregistrement d'1 s
- ET7 : MSE < 0.05 sur données de test après entraînement
- ET8 : dataset ≥ 100 éléments (50 par mot, ≥ 2 mots)
- ET9 : < 5 % d'erreur sur ≥ 10 mots prononcés, robustesse bruit de fond
- Arduino Due obligatoire → sinon malus -12

## Agents spécialisés (`.claude/agents/`, non versionnés)

| Agent | Utiliser quand |
|-------|----------------|
| `signal-chain` | FP1/FP2/FP3 — ADC, filtre numérique, buffer circulaire, série |
| `mfcc-expert` | FP4 — préemphase, Hamming, FFT, MEL, DCT |
| `ml-expert` | FP5/FP6 — CNN design, training Python, inference embarquée |
| `hardware` | Câblage MAX9814, bouton, oscilloscope, LEDs |
| `ns-orchestrator` | Tâche multi-domaine, lots parallèles |
| `agent-smith` | Créer ou modifier un agent |

Les agents sont gitignored : seuls les membres du groupe qui les régénèrent localement les ont.
```

## Flux de travail type

### Cas 1 : tâche mono-domaine

Tu demandes "implémente l'ADC 32 kHz sur timer". Main Claude lit `CLAUDE.md` → route vers `signal-chain` via Agent tool. L'agent lit le code courant, écrit le code, propose un test oscilloscope.

### Cas 2 : tâche multi-domaine

Tu demandes "mets en place FP1, FP2 et FP3 cette semaine". Main Claude délègue à `ns-orchestrator`. Celui-ci identifie 3 sous-tâches quasi indépendantes (FP1→FP2 ont une dépendance séquentielle mais l'API est claire, FP3 est une lecture). Il dispatche `signal-chain` sur FP1 d'abord, puis en parallèle FP2 et FP3.

### Cas 3 : extension

Tu ajoutes un OLED. Tu invoques `agent-smith`. Il pose les questions, crée `oled-expert.md`, met à jour la table de routage dans `CLAUDE.md`.

## Risques & mitigations

| Risque | Mitigation |
|--------|-----------|
| Agents qui se marchent sur les pieds (signal-chain + mfcc-expert touchent au même `main.cpp`) | L'orchestrateur les sérialise sur les tâches qui éditent le même fichier |
| Contexte vivant coûte des tokens à chaque invocation | Acceptable — c'est le prix de la non-staleness. Les fichiers critiques restent petits (src/main.cpp) |
| Agent-smith peut créer des doublons d'agents | Son protocole impose une question explicite sur le chevauchement avant création |
| Les agents sont gitignored → les coéquipiers doivent les régénérer | Assumé par l'utilisateur. `CLAUDE.md` documente le système pour qu'ils sachent quoi recréer |
| ml-expert tourne en opus → coûts | Assumé : c'est le cerveau technique le plus dense |

## Critères de succès

- Les 6 agents existent et sont invocables (`Skill` tool peut les lister indirectement via `Agent`)
- Chaque agent produit, sur son domaine, du code/raisonnement aligné avec les ET applicables
- `ns-orchestrator` sait dispatcher ≥ 2 agents en parallèle sur une tâche multi-FP
- `agent-smith` peut créer un 7e agent (OLED par exemple) sans intervention manuelle sur les fichiers
- `CLAUDE.md` permet à un nouveau Claude Code d'arriver dans le repo et comprendre l'architecture

## Évolution prévue

Quand FP1-FP6 seront implémentés, les agents deviendront plutôt des "maintenance/optim" plutôt que "création". On pourrait à ce moment-là :
- Consolider `ml-expert` en deux (un pour training, un pour inference embarquée)
- Ajouter un `report-writer` pour la rédaction du rapport (50 % de la note)
- Ajouter un `soutenance-coach` pour la prépa des démos (25 % + 25 %)

Ces extensions passent par `agent-smith`.
