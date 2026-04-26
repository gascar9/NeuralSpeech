---
layout: two-cols
layoutClass: gap-4
hideInToc: true
---

# Backup — Filtre RIF Q15

```cpp {all|2-3|6-7|9-13|14|all}
int16_t filter_sample(uint16_t new_sample) {
  // 1) Centrer ADC autour de 0
  int16_t centered = (int16_t)new_sample - 2048;

  // 2) Pousser dans buffer circulaire RIF
  firBuf[firBufHead] = centered;
  firBufHead = (firBufHead + 1) & FIR_BUF_MASK;

  // 3) Convolution Q15 — 40 MAC int32
  int32_t acc = 0;
  uint32_t idx = (firBufHead - 1) & FIR_BUF_MASK;
  for (uint32_t k = 0; k < FILTER_TAPS; k++) {
    acc += (int32_t)FILTER_COEFS_Q15[k]
         * (int32_t)firBuf[idx];
    idx = (idx - 1) & FIR_BUF_MASK;
  }
  return (int16_t)(acc >> 15);  // Q15 → Q0
}
```

::right::

# Backup — Mesure DWT cycle counter

```cpp {all|3-4|6|9-10|all}
// Setup unique
SCB_DEMCR  |= DEMCR_TRCENA;
DWT_CYCCNT  = 0;
DWT_CTRL   |= DWT_CTRL_CYCCNTENA;

// Mesure : 1 cycle hardware par lecture
uint32_t c0 = DWT_CYCCNT;
int16_t y = filter_sample(x);
uint32_t c1 = DWT_CYCCNT;

uint32_t us_x100 = ((c1 - c0) * 100UL) / 84UL;
```

<br>

**Pourquoi pas `micros()` ?** Sur Due, `micros()` exécute un retry-loop sensible aux ISR TC0. Coût mesuré : **20–30 µs par appel**, qui polluait notre mesure de filtrage initial à hauteur de 60 µs.

DWT = lecture directe du compteur de cycles ARM Cortex-M3.<br>
Précision **± 1 cycle ≈ 12 ns**.
