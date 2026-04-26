---
layout: default
---

# Diagramme fonctionnel — 6 FP, 2 entrées, 2 sorties

```mermaid {scale: 0.7}
flowchart LR
  VOIX([Voix]):::io ==> FP1
  DATA([Jeux de<br>données]):::io ==> FP5

  FP1[FP1<br>Numériser le<br>signal audio]:::done -->|audio<br>numérisé| FP2[FP2<br>Conditionner<br>le signal audio]:::done
  FP2 -->|audio filtré| FP3[FP3<br>Écouter et valider<br>l'enregistrement]:::done
  FP2 -->|audio filtré| FP4[FP4<br>Caractériser<br>le timbre vocal]:::todo
  FP4 -->|MFCCs| FP6[FP6<br>Classifier les<br>enregistrements]:::todo
  FP5[FP5<br>Identifier les<br>résultats attendus]:::todo -->|poids NN| FP6

  FP3 ==> AUDIO([Enregistrement<br>audio]):::out_done
  FP6 ==> CMD([Commande<br>vocale]):::out_todo

  classDef io fill:#fff,stroke:#000,stroke-width:2px,color:#000
  classDef done fill:#86efac,stroke:#15803d,color:#000,stroke-width:2px
  classDef todo fill:#cbd5e1,stroke:#64748b,color:#1e293b,stroke-width:2px
  classDef out_done fill:#fff,stroke:#15803d,stroke-width:3px,color:#15803d
  classDef out_todo fill:#fff,stroke:#64748b,stroke-width:2px,color:#475569
```

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-top: 12px; font-size: 0.78em;">

<div style="background:#f0fdf4; border-left: 4px solid #16a34a; padding: 10px 14px;">

**Soutenance 1 — chaîne validée (vert)**

✓ FP1 numérisation · ✓ FP2 conditionnement · ✓ FP3 écoute Audacity<br>
→ Sortie « Enregistrement audio » fonctionnelle (ET1 à ET4)

</div>

<div style="background:#f1f5f9; border-left: 4px solid #64748b; padding: 10px 14px;">

**Soutenance 2 — chaîne IA (gris)**

FP4 MFCC · FP5 training CNN Python · FP6 inférence + LEDs<br>
→ Sortie « Commande vocale » à venir (ET5 à ET9)

</div>

</div>
