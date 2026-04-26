---
layout: image-right
image: /fp1_nyquist_16khz.jpeg
backgroundSize: contain
---

# Démo Nyquist : à F<sub>e</sub>/2 la reconstruction casse

**Test pédagogique** : injection à **16 kHz** = limite théorique de Nyquist.

- CH1 (entrée brute) → toujours bien présent
- CH2 (reconstruction DAC) → **plus reconnaissable**, amplitude divisée par 5

À la limite, 2 échantillons par période **ne suffisent plus** à capturer la phase du signal.

<br>

**Conclusion** : tout signal au-dessus de 4 kHz dans la bande utile (après décimation /4) **se replie** dans la voix.

→ **Justifie la nécessité du filtre FP2.**
