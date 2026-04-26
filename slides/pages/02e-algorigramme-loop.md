---
layout: default
---

# Algorigramme : `loop()` — flux par échantillon

```mermaid {scale: 0.6}
flowchart LR
  L([loop iter]):::ev --> CHK{Sample<br>dispo ?}:::dec
  CHK -->|oui| FILT[filter_sample Q15<br>~11 µs]:::act
  FILT --> OUT[DAC0/DAC1<br>buf8k push /4]:::act
  OUT --> SVC[FP3 service]:::act
  CHK -->|non| SVC
  SVC --> L

  classDef ev fill:#e2e8f0,stroke:#475569,color:#000
  classDef act fill:#bfdbfe,stroke:#2563eb,color:#000,stroke-width:1.5px
  classDef dec fill:#fed7aa,stroke:#ea580c,color:#000
```

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-top: 16px; font-size: 0.78em;">

<div>

**3 étapes par échantillon (mode IDLE)**

1. **Sample dispo ?** — pop ADC buffer si oui
2. **Filtrage Q15** — 40 MAC en ~11 µs, sortie sur DAC0 (brut) et DAC1 (filtré), 1/4 → buf8k
3. **FP3 service** — polling bouton D2 + envoi série (1 octet/tour)

</div>

<div>

**Coût total &lt; 15 µs**

→ Largement sous le budget ET3 (31,25 µs / sample).<br>
→ La loop tourne plus vite que l'ISR ne produit.<br>
→ `buf_used = 0–1 / 512` en régime permanent : aucune perte d'échantillon.

</div>

</div>
