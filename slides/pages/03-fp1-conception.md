---
layout: two-cols
layoutClass: gap-8
---

# FP1 — Échantillonnage à Fe = 32 kHz exacts

**Choix : Timer Counter (TC0) + ADC** plutôt qu'ADC free-running.

- Cadence isochrone, indépendante du CPU
- Conforme à l'exigence "interruption sur timer"

<br>

**Calcul du registre RC**

$$\mathrm{RC} = \left\lfloor \frac{F_{MCK/8}}{F_e} \right\rfloor = \left\lfloor \frac{10\,500\,000}{32\,000} \right\rfloor = 328$$

$$F_e^{\text{réelle}} = \frac{10\,500\,000}{328} = 32\,012\ \text{Hz}$$

Erreur : **0,04 %**.

::right::

```cpp {all|2-3|7-8|all}
void TC0_Handler(void) {
  // 1) Acquittement IRQ (lecture du SR)
  TC0->TC_CHANNEL[0].TC_SR;

  // 2) Conversion ADC bloquante minimale
  ADC->ADC_CR = ADC_CR_START;
  while (!(ADC->ADC_ISR & (1u << 7))) {}
  uint16_t s = ADC->ADC_CDR[7] & 0x0FFF;

  // 3) Push buffer circulaire (1 cycle)
  adcBuffer[bufHead] = s;
  bufHead = (bufHead + 1) & BUFFER_MASK;
  sampleCount++;
}
```

ISR ultra-courte : **~ 1 µs**, déterministe.
