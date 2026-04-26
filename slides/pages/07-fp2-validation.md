---
layout: default
---

# ET2 démontrée à l'oscilloscope (DAC0 vs DAC1)

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px 24px; margin-top: 4px;">

<div style="text-align: center;">
  <img src="/fp2_oscilloscope_1khz.jpeg" style="max-height: 145px; border-radius: 6px; box-shadow: 0 2px 6px rgba(0,0,0,.15);" />
  <div style="font-size: 0.72em; margin-top: 4px;"><b>1 kHz</b> — CH1 = CH2 (passe)</div>
</div>

<div style="text-align: center;">
  <img src="/fp2_oscilloscope_2khz.jpeg" style="max-height: 145px; border-radius: 6px; box-shadow: 0 2px 6px rgba(0,0,0,.15);" />
  <div style="font-size: 0.72em; margin-top: 4px;"><b>2 kHz</b> — toujours en bande passante</div>
</div>

<div style="text-align: center;">
  <img src="/fp2_oscilloscope_3khz.jpeg" style="max-height: 145px; border-radius: 6px; box-shadow: 0 2px 6px rgba(0,0,0,.15);" />
  <div style="font-size: 0.72em; margin-top: 4px;"><b>3 kHz</b> — entrée en transition</div>
</div>

<div style="text-align: center;">
  <img src="/fp2_oscilloscope_4khz.jpeg" style="max-height: 145px; border-radius: 6px; box-shadow: 0 2px 6px rgba(0,0,0,.15);" />
  <div style="font-size: 0.72em; margin-top: 4px;"><b>4 kHz</b> — CH2 effondré → ET2 ✓</div>
</div>

</div>

<div style="font-size: 0.72em; text-align: center; margin-top: 14px; opacity: 0.7;">
DAC0 = signal brut (CH1) · DAC1 = signal filtré (CH2) · GBF 1 V<sub>pp</sub> sinus
</div>
