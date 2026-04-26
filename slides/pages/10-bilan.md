---
layout: default
---

# Bilan Soutenance 1 — 4 ET sur 9 validées

<div class="grid grid-cols-2 gap-6 mt-4">

<div>

**ET validées**

<div class="text-sm">

| ET | Critère | Mesuré |
|----|---------|--------|
| **ET1** | Fe = 32 kHz timer | **32 012 Hz** |
| **ET2** | Att. ≥ 30 dB @ 4 kHz | **−41 dB** théorique |
| **ET3** | Filtrage < 31 µs | **11,3 µs avg** |
| **ET4** | « Électronique » Audacity | ✓ audible |

</div>

</div>

<div>

**Ressources Arduino Due**

<div class="text-sm">

| Ressource | Utilisé | Capacité | % |
|-----------|---------|----------|---|
| SRAM | 24 Kio | 96 Kio | **24,5 %** |
| Flash | 27 Kio | 512 Kio | **5,2 %** |
| ISR TC0 | 1 µs | 31,25 µs | 3 % |

</div>

→ **75 Kio SRAM libre** pour FP4 (MFCC) + FP6 (inférence CNN).

</div>

</div>

<br>

<div class="bg-green-50 border-l-4 border-green-500 p-3 text-sm">

**Roadmap Soutenance 2** : FP4 MFCC (62×13 par seconde) → FP5 CNN training Python → FP6 inférence embarquée + LEDs → < 5 % d'erreur sur 10 mots.

</div>
