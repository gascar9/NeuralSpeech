---
layout: two-cols
layoutClass: gap-8
---

# ET3 : 900 µs → 11 µs en 4 fixes

**Budget ET3 : < 31,25 µs par échantillon**

<br>

| # | Fix | Effet |
|---|---|---|
| 1 | Q15 fixed-point | float émulé éliminé |
| 2 | `build_unflags=-Os` | `-O2` enfin actif |
| 3 | Mesure DWT cycle counter | sonde fiable |
| 4 | ADC prescaler 41 → 1 | ISR 20 µs → 1 µs |

<br>

**Mesure finale (30 s console)**

- `filter_us_avg` = **11,29 µs** (constant)
- `filter_us_max` = 14,64 µs
- `buf_used` = 0–1 / 512

→ Marge **53 %** sur ET3.

::right::

<img src="/fp2_console_dwt.png" class="rounded shadow-md mt-4" />

<div class="text-xs opacity-70 text-center mt-2">
Console série mesurée par le compteur de cycles<br>
matériel DWT (1 cycle de lecture, zéro overhead).
</div>

<div class="mt-4 text-sm bg-blue-50 border-l-4 border-blue-500 p-2">
💡 **Leçon DSP embarqué** : sur Cortex-M3 sans FPU, tout traitement temps-réel passe par du fixed-point.
</div>
