---
layout: default
---

# Le défi : reconnaître la voix sans GPU

<br>

L'objectif est de classer un mot prononcé par **une IA tournant intégralement sur un microcontrôleur 84 MHz**, sans aucun framework lourd.

<br>

<div class="grid grid-cols-2 gap-8 mt-6">

<div>

**Plateforme imposée**

- Arduino **Due** (SAM3X8E)
- Cortex-M3 — **pas de FPU**
- 96 Kio SRAM, 512 Kio Flash

</div>

<div>

**Cahier des charges**

- 9 exigences techniques (ET1–ET9)
- Pipeline complet : ADC → MFCC → CNN → LEDs
- Malus −12 si non Arduino Due

</div>

</div>

<div class="abs-br m-6 text-xs opacity-50">
  ET1–ET9 : exigences techniques du sujet NeuralSpeech 2026 V1.3
</div>
