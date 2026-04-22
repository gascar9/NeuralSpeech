# NeuralSpeech — Projet ING3 S6 Calcul Embarqué (ECE)

Reconnaissance vocale embarquée sur **Arduino Due** (SAM3X8E, 84 MHz, 96 Kio SRAM).
Chaîne : microphone MAX9814 → ADC 32 kHz → filtre numérique → sous-échantillonnage 8 kHz → MFCC (13 coefs/frame) → CNN → LEDs.

## Documentation projet

- Sujet : `NeuralSpeech_2026_V1.pdf`
- Grille d'évaluation soutenance 1 : `grille critérée dévaluation de projet NeuralSpeech_soutenance 1.pdf`
- Spec agents : `docs/superpowers/specs/2026-04-22-neural-speech-agents-design.md`

## Contraintes non négociables (ET du sujet)

- **ET1** : ADC 32 kHz via interruption timer, échantillonnage fixe et précis
- **ET2** : atténuation ≥ 30 dB au-dessus de 4 kHz (filtre anti-repliement)
- **ET3** : filtrage < 31 µs par échantillon (1 / 32 kHz)
- **ET4** : enregistrement "Électronique" restituable clairement sur Audacity
- **ET5** : frames de 256 échantillons avec recouvrement (hop 128)
- **ET6** : 13 MFCCs générés à partir d'un enregistrement d'1 seconde
- **ET7** : MSE < 0.05 sur données de test après entraînement CNN
- **ET8** : dataset ≥ 100 éléments (50 par mot × ≥ 2 mots)
- **ET9** : < 5 % d'erreur sur ≥ 10 mots prononcés, robustesse au bruit de fond
- **Arduino Due obligatoire** : un projet fonctionnel sur PC mais pas sur Arduino = malus -12

## Agents spécialisés (`.claude/agents/`, non versionnés)

| Agent | Rôle | Utiliser quand |
|-------|------|----------------|
| `signal-chain` | FP1+FP2+FP3 : ADC, filtre numérique, buffer circulaire, série | ADC config, filtrage temps réel, transfert Audacity |
| `mfcc-expert` | FP4 : préemphase, Hamming, FFT, MEL, DCT | calcul des MFCC, FFT, banc MEL |
| `ml-expert` | FP5+FP6 : CNN, training Python, inference embarquée | design réseau, training Keras/PyTorch, export poids, inference C |
| `hardware` | Électronique & validation physique | câblage MAX9814, bouton, LEDs, oscilloscope |
| `ns-orchestrator` | Décompose & dispatche en parallèle | tâche multi-domaine, attaque parallèle de plusieurs FP |
| `agent-smith` | Meta-agent : crée/modifie les autres agents | nouvelle brique (OLED, Bluetooth…), évolution du scope |

## Règles de routage pour le main Claude

- Tâche mono-domaine identifiable → délègue directement au spécialiste correspondant via l'outil Agent
- Tâche qui touche plusieurs FP simultanément → délègue à `ns-orchestrator`
- Demande de modification du système d'agents lui-même → `agent-smith`
- Question générale de projet ou de rapport → traite directement (pas de spécialiste rapport-writer pour l'instant)

## Projet

- Framework : PlatformIO + Arduino
- Board : Arduino Due (`platformio.ini` → `env:due`)
- Source : `src/main.cpp`
- Dataset (fourni par l'école) : GitHub Pôle Électronique — mots "bleu" / "rouge" pour le bootstrap

## Note sur le versioning

Le dossier `.claude/` est intégralement gitignored — les agents eux-mêmes ne sont pas partagés via git. Ce `CLAUDE.md` sert de documentation pour que n'importe quel membre du groupe (ou une session Claude Code future) puisse comprendre l'architecture et recréer les agents si besoin.

## Rapport LaTeX (50 % de la note)

Le rapport est dans `rapport/` en LaTeX modulaire (un `.tex` par FP dans `chapters/`). **Compilation PDF uniquement en fin de projet** ; entre-temps on édite les sources.

**Règle pour les sessions Claude Code** : à chaque résultat substantiel d'une FP (code validé, mesure bench, figure produite), mettre à jour le chapitre correspondant dans `rapport/chapters/0X-fpX.tex` :
- Remplacer les `\TODO{...}` par le contenu produit
- Placer les figures dans `rapport/figures/FPx/` (ou référencer `assets/FPx/` via `\graphicspath`)
- Style factuel, formules/unités en siunitx (`\SI{31.25}{\micro\second}`, `\SI{32}{\kilo\hertz}`)

Roadmap complète et checklist par phase : `docs/roadmap-soutenances.md`.
