---
layout: default
---

# Architecture matérielle : 4 fils + 1 bouton + 1 micro

```mermaid {scale: 0.7}
flowchart LR
  MIC["MAX9814<br><b>VCC</b> · <b>GND</b> · <b>OUT</b>"]:::mic ===>|3 fils| DUE
  BTN(["Bouton<br><b>D2</b> ↔ <b>GND</b>"]):::btn ==>|2 fils| DUE
  DUE["<b>Arduino Due</b><br>SAM3X8E 84 MHz<br>96 Kio SRAM"]:::due ==>|DAC0 → CH1| OSC["Oscilloscope<br>Siglent SDS 1102"]:::ext
  DUE ==>|DAC1 → CH2| OSC
  DUE <==>|USB Programming| PC["PC<br>PlatformIO · Python · Audacity"]:::pc

  classDef mic fill:#fef3c7,stroke:#d97706,color:#000,stroke-width:2px
  classDef btn fill:#fecaca,stroke:#dc2626,color:#000,stroke-width:2px
  classDef due fill:#dcfce7,stroke:#16a34a,color:#000,stroke-width:3px
  classDef ext fill:#e2e8f0,stroke:#475569,color:#000,stroke-width:2px
  classDef pc fill:#dbeafe,stroke:#2563eb,color:#000,stroke-width:2px
```

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-top: 16px; font-size: 0.78em;">

<div>

**Pinout retenu (Arduino Due)**

| Broche | Fonction |
|--------|----------|
| **A0** | ADC entrée audio (MAX9814 OUT) |
| **D2** | Bouton FP3 (pull-up interne) |
| **DAC0** (pin 66) | Signal **brut** → CH1 oscillo |
| **DAC1** (pin 67) | Signal **filtré** → CH2 oscillo |

</div>

<div>

**Choix d'intégration**

- MAX9814 alimenté en **3,3 V** (Due *non* 5 V tolérante)
- Bouton **sans résistance externe** (`INPUT_PULLUP` interne)
- Aucun shield → câblage direct breadboard
- **Masse commune** Due ↔ GBF ↔ oscilloscope

</div>

</div>
