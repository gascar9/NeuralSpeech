---
layout: default
---

# Pipeline NeuralSpeech : 6 fonctions, 3 closes

<br>

```mermaid {scale: 0.85}
flowchart LR
  MIC[Micro<br>MAX9814]:::done --> FP1
  FP1[FP1<br>ADC 32 kHz]:::done --> FP2
  FP2[FP2<br>Filtre + 8 kHz]:::done --> FP3
  FP2 --> FP4
  FP3[FP3<br>Audacity]:::done --> CHECK([validation])
  FP4[FP4<br>MFCC 13 coefs]:::todo --> FP6
  FP5[FP5<br>CNN training]:::todo --> FP6
  FP6[FP6<br>Inférence + LEDs]:::todo --> OUT[LEDs]:::todo

  classDef done fill:#22c55e,stroke:#15803d,color:#fff
  classDef todo fill:#cbd5e1,stroke:#64748b,color:#1e293b
```

<div class="grid grid-cols-2 gap-6 mt-6 text-sm">

<div class="bg-green-50 border-l-4 border-green-500 p-3">

**Soutenance 1 (aujourd'hui)** — ✓ FP1, FP2, FP3<br>
ET1, ET2, ET3, ET4 validées

</div>

<div class="bg-slate-100 border-l-4 border-slate-400 p-3">

**Soutenance 2** — FP4, FP5, FP6<br>
ET5, ET6, ET7, ET8, ET9 à venir

</div>

</div>
