---
layout: default
---

# Algorigramme : ISR TC0 + state machine FP3

```mermaid {scale: 0.65}
flowchart LR
  S([TC0 trigger<br>32 kHz]):::ev --> A[Acquit TC_SR<br>flag IRQ effacé]:::isr
  A --> B[Start ADC<br>EOC poll ~1 µs]:::isr
  B --> C[adcBuffer push<br>head++ mask]:::isr
  C --> D([return]):::ev

  IDLE((IDLE)):::st -->|D2 falling<br>debounce 50 ms| ARM
  ARM((ARMING)):::st -->|8000 samples<br>captureBuffer plein| DUMP
  DUMP((DUMPING)):::st -->|16 008 octets<br>envoyés| IDLE

  classDef isr fill:#fecaca,stroke:#dc2626,color:#000,stroke-width:2px
  classDef st fill:#fde68a,stroke:#ca8a04,color:#000,stroke-width:2px
  classDef ev fill:#e2e8f0,stroke:#475569,color:#000
```

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-top: 18px; font-size: 0.78em;">

<div>

**ISR TC0 — producteur 32 kHz**

ISR ultra-courte (~1 µs) : pas de calcul, juste lecture ADC + push buffer. Garantit Fe stable à 0,04 % d'erreur près.

</div>

<div>

**FSM FP3 — bouton D2**

Non bloquante : la chaîne FP1+FP2 continue de filtrer pendant ARMING (3 s de capture) et DUMPING (~0,64 s de transfert série).

</div>

</div>
