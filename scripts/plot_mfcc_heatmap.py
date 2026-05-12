#!/usr/bin/env python3
"""Plot side-by-side MFCC heatmaps for two recordings (rapport bonus FP4)."""
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt

if len(sys.argv) < 4:
    print("Usage: plot_mfcc_heatmap.py <a.npy> <b.npy> <out.png>")
    sys.exit(1)

a_path, b_path, out_path = sys.argv[1:4]
a = np.load(a_path)
b = np.load(b_path)

fig, axes = plt.subplots(1, 2, figsize=(10, 4), sharey=True)
for ax, mfcc, label in zip(axes, [a, b], [Path(a_path).stem, Path(b_path).stem]):
    im = ax.imshow(mfcc.T, aspect='auto', origin='lower', cmap='viridis')
    ax.set_title(label)
    ax.set_xlabel('Frame (16 ms)')
    ax.set_ylabel('Coef MFCC')
    fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)

plt.tight_layout()
plt.savefig(out_path, dpi=150, bbox_inches='tight')
print(f"Saved: {out_path}")
