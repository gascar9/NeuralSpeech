# Slides — NeuralSpeech Soutenance 1

Présentation 8 minutes obligatoire (barème ECE) pour la soutenance 1.
Construite avec [Slidev](https://sli.dev) — markdown + Vue + Mermaid.

## Structure

```
slides/
├── slides.md              ← entry point (frontmatter global + imports)
├── pages/
│   ├── 01-context.md         le défi : reco vocale embarquée
│   ├── 02-architecture.md    diagramme fonctionnel FP1→FP6
│   ├── 03-fp1-conception.md  ADC 32 kHz + TC0 + ISR
│   ├── 04-fp1-validation.md  Fe = 32 012 Hz, 3 preuves
│   ├── 05-fp1-nyquist.md     démo limite 16 kHz → motive FP2
│   ├── 06-fp2-conception.md  Parks-McClellan 40 taps
│   ├── 07-fp2-validation.md  4 captures oscillo 1/2/3/4 kHz
│   ├── 08-fp2-optim.md       voyage 900 µs → 11 µs
│   ├── 09-fp3.md             bouton → WAV → Audacity
│   ├── 10-bilan.md           4 ET sur 9 + ressources Due
│   ├── 11-questions.md       slide finale
│   └── 99-backup-code.md     extraits code (filtre Q15, DWT)
├── package.json
└── README.md
```

12 slides + 1 backup. **Time budget : 8 min ≈ 40 s par slide**.

## Répartition équipe (suggérée)

| Slide(s) | Responsable | Contenu |
|----------|-------------|---------|
| 1, 11 | Tous | titre, questions |
| 2, 3, 4, 5 | **Gaspard / Albin** | FP1 (ADC + Nyquist) |
| 6, 7, 8 | **Maxence / Timothy** | FP2 (filtre + ET3) |
| 9 | binôme | FP3 (Audacity demo) |
| 10 | tous | bilan + roadmap |

À adapter selon vos préférences. **Évitez les rebasements** — chacun édite ses pages.

## Installation et utilisation

### Prérequis
- Node.js ≥ 18 (`brew install node` sur macOS)
- Slidev CLI : `npm install -g @slidev/cli`

### Démarrer le serveur de prévisualisation

```bash
cd slides
npm install
npm run dev
```

Slidev ouvre automatiquement le navigateur sur `http://localhost:3030/`.

### Navigation présentateur

- **Espace** ou **flèche droite** : slide suivante
- **Flèche gauche** : slide précédente
- **F** : plein écran
- **D** : mode sombre
- **O** : vue d'ensemble (toutes les slides)
- **G** : aller à un numéro de slide

### Exporter en PDF (pour le dépôt BC)

```bash
npm run export
```

Génère `../rapport/figures/slides-soutenance-1.pdf`.

### Exporter en PNG (pour partage)

```bash
npm run export-png
```

## Contraintes de design auto-imposées

Inspirées du plugin Slidev "evidence-based" :

- **≤ 6 éléments visuels par slide** — éviter la surcharge cognitive
- **< 50 mots de texte** — la slide soutient le discours, ne le remplace pas
- **Titres assertifs** — « ET1 validée : Fe = 32 012 Hz » et non « Validation FP1 »
- **Police ≥ 18pt** — lisibilité projecteur
- **Palette accessible** — bleu / orange + vert/gris (color-blind safe)

## Ce qu'il faut éviter (rappel barème)

> "**Pas un résumé du rapport**" — le sujet impose des **schémas électriques, algorigrammes, courbes de résultats, tests** plutôt que des paragraphes de prose.

→ Les slides actuelles respectent ce principe : 1 figure ou table par slide, prose minimale.

## TODO avant la soutenance

- [ ] **Répétition chronométrée** (objectif : 8 min pile, 4 min démo)
- [ ] **Test du projecteur la veille** (USB-C → HDMI dans le sac)
- [ ] **Capture Audacity du WAV "Électronique"** à insérer dans la slide FP3 (actuellement implicite)
- [ ] **Diagramme câblage hardware** propre (KiCad ou même main levée scannée) si possible
- [ ] **Backup laptop** chargé + Arduino Due fonctionnelle testée 1 h avant
- [ ] **Dépôt BC** du firmware + slides + rapport intermédiaire **avant** la deadline
