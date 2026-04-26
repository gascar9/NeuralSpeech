---
layout: image-right
image: /fp1_reconstruction_1kHz.jpeg
backgroundSize: contain
---

# ET1 validée : Fe = 32 012 Hz, stable

**3 preuves complémentaires :**

**1. Console série** — comptage des ISR sur 1 seconde<br>
&nbsp;&nbsp;&nbsp;&nbsp;→ Te = 1 / 32 012 = **31,238 µs**

**2. Reconstruction oscillo** — sinus 1 kHz<br>
&nbsp;&nbsp;&nbsp;&nbsp;CH1 (entrée) ≡ CH2 (DAC0) — *voir image*

**3. Buffer non saturé** — `buf_used = 0–1 / 512`<br>
&nbsp;&nbsp;&nbsp;&nbsp;la loop consomme plus vite que l'ISR ne produit

<br>

Erreur de fréquence : **0,04 %** vs cible 32 kHz.
