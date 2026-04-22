# Rapport NeuralSpeech

Rapport LaTeX du projet — alimenté **en continu** au fil des FPs, compilé en PDF uniquement en fin de projet.

## Structure

```
rapport/
├── main.tex                    Document racine (classe, packages, meta)
├── chapters/
│   ├── 00-resume.tex           Résumé (à rédiger en fin)
│   ├── 01-introduction.tex     Contexte, objectifs, ET1-ET9
│   ├── 02-architecture.tex     Diagramme fonctionnel, pinout
│   ├── 03-fp1.tex              FP1 — ADC 32 kHz    [déjà rempli partiellement]
│   ├── 04-fp2.tex              FP2 — Filtrage      [skeleton]
│   ├── 05-fp3.tex              FP3 — Audacity      [skeleton]
│   ├── 06-fp4.tex              FP4 — MFCC          [skeleton]
│   ├── 07-fp5.tex              FP5 — CNN Python    [skeleton]
│   ├── 08-fp6.tex              FP6 — Inférence     [skeleton]
│   ├── 09-gestion-projet.tex   Gantt, répartition tâches
│   ├── 10-conclusion.tex       Bilan, perspectives
│   └── 11-annexes.tex          Code, schémas, datasheets
├── figures/                    Images générées (plots Python, schémas)
├── references.bib              Bibliographie (BibTeX)
├── .gitignore                  Ignore les .aux, .log, .pdf pendant le chantier
└── README.md                   Ce fichier
```

## Comment remplir

Chaque chapitre FP est structuré de la même façon :
- **Conception** : choix, justification, formules
- **Implémentation** : extraits de code clés (pas tout le code, uniquement les 10-15 lignes importantes par section)
- **Validation** : protocole de test, résultats chiffrés, captures/figures
- **Bilan** : synthèse en 3-5 lignes, ET validée ou non

Les `\TODO{...}` en rouge marquent ce qui reste à rédiger. Au fur et à mesure des réponses techniques (depuis les agents `signal-chain`, `mfcc-expert`, `ml-expert`, `hardware`), on remplit les sections correspondantes.

## Macros utiles

- `\etref{N}` → rend **ET1**, **ET2**, ...
- `\fpref{N}` → rend **FP1**, **FP2**, ...
- `\TODO{...}` → bloc rouge à compléter
- `\FILLED{...}` → coche verte « terminé »
- `\SI{valeur}{unité}` (siunitx) → pour toutes les unités (Hz, µs, dB, etc.)

## Compilation (en fin de projet uniquement)

```bash
cd rapport
latexmk -pdf main.tex
```

Ou via un IDE LaTeX (TeXstudio, Overleaf en collant les fichiers).

**Packages requis** (distribution TeX Live / MacTeX complète) :
`inputenc`, `fontenc`, `babel`, `csquotes`, `geometry`, `setspace`, `fancyhdr`, `titlesec`, `parskip`, `microtype`, `lmodern`, `xcolor`, `graphicx`, `float`, `caption`, `subcaption`, `booktabs`, `tabularx`, `multirow`, `amsmath`, `siunitx`, `listings`, `hyperref`.

## Règles d'édition équipe

1. **Un fichier par FP** → pas de conflit git si chacun édite sa FP
2. **Commits atomiques** : un commit = une section / une figure
3. **Éviter les retours arrière** sur les parties d'autrui — discuter avant
4. **Figures dans `figures/FPx/`** → chemins relatifs seulement, pas de chemins absolus
5. **Relecture croisée** avant chaque dépôt BC
