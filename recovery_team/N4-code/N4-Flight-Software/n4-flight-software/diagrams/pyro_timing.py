"""
N4 Pyro Firing Sequence Timeline
=================================
Gantt-style horizontal timeline showing:
  – Flight phase bands (colour-coded)
  – Key events as vertical markers
  – Pyro PWM windows as shaded regions

Generates:
    output/pyro_timing.svg
    output/pyro_timing.png

Run:  python pyro_timing.py
"""

import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch

os.makedirs("output", exist_ok=True)

# ─── Timeline definition ──────────────────────────────────────────────────────
# (phase_id, label, t_start, t_end, colour)
PHASES = [
    ("pre",        "PRE_FLIGHT\nGROUND",     0,    2,   "#D6E9FF"),
    ("powered",    "POWERED\nFLIGHT",        2,    8,   "#C8E6C9"),
    ("coasting",   "COASTING",               8,   18,   "#DCEDC8"),
    ("apogee",     "APOGEE",                18,   22,   "#FFF9C4"),
    ("drogue_dep", "DROGUE\nDEPLOY",        22,   27,   "#FFCCBC"),
    ("drogue_des", "DROGUE\nDESCENT",       27,   80,   "#E1F5FE"),
    ("main_dep",   "MAIN\nDEPLOY",          80,   85,   "#FFCCBC"),
    ("main_des",   "MAIN\nDESCENT",         85,  130,   "#E1F5FE"),
    ("post",       "POST_FLIGHT\nGROUND",  130,  135,   "#D6E9FF"),
]

# (t, label, y_offset, colour)
EVENTS = [
    (  2,   "Launch\ndetected",     +1, "#2E7D32"),
    (  8,   "Burnout /\ndecel",     +1, "#1565C0"),
    ( 18,   "Apogee\n(Kalman)",     +1, "#F57F17"),
    ( 19.5, "+1500 ms\ndelay",      -1, "#F57F17"),
    ( 21,   "GPIO 25\nfires",       +1, "#C62828"),
    ( 80,   "<500 m AGL",           -1, "#1565C0"),
    ( 80.5, "GPIO 12\nfires",       +1, "#C62828"),
    (130,   "Landed",               +1, "#2E7D32"),
]

# Pyro PWM windows  (t_start, duration, label, colour)
PYRO_WINDOWS = [
    (21,   5, "Drogue PWM  5 s  GPIO 25", "#EF9A9A"),
    (80.5, 5, "Main PWM  5 s  GPIO 12",   "#EF9A9A"),
]

# ─── Figure ───────────────────────────────────────────────────────────────────
T_MAX = 137
BAR_Y = 5.5
BAR_H = 1.4

fig, ax = plt.subplots(figsize=(18, 7))
ax.set_xlim(-2, T_MAX + 2)
ax.set_ylim(0, 9.5)
ax.axis("off")
fig.patch.set_facecolor("white")

# ─── Phase bands ──────────────────────────────────────────────────────────────
for pid, label, t0, t1, fc in PHASES:
    width = t1 - t0
    rect = mpatches.FancyBboxPatch(
        (t0, BAR_Y - BAR_H / 2), width, BAR_H,
        boxstyle="square,pad=0",
        facecolor=fc, edgecolor="#90A4AE", linewidth=0.8, zorder=2,
    )
    ax.add_patch(rect)
    cx = t0 + width / 2
    ax.text(cx, BAR_Y, label,
            ha="center", va="center",
            fontsize=7.5, fontweight="bold", color="#263238",
            linespacing=1.25, zorder=4)

# ─── Pyro windows (semi-transparent overlay) ─────────────────────────────────
for t0, dur, label, fc in PYRO_WINDOWS:
    rect = mpatches.FancyBboxPatch(
        (t0, BAR_Y - BAR_H / 2 - 0.05), dur, BAR_H + 0.1,
        boxstyle="square,pad=0",
        facecolor=fc, edgecolor="#C62828", linewidth=1.5,
        alpha=0.75, zorder=3,
    )
    ax.add_patch(rect)
    ax.text(t0 + dur / 2, BAR_Y - BAR_H / 2 - 0.55, label,
            ha="center", va="top",
            fontsize=8, color="#B71C1C", fontweight="bold", zorder=5)

# ─── Event markers ────────────────────────────────────────────────────────────
for t, label, side, col in EVENTS:
    top_y = BAR_Y + BAR_H / 2
    bot_y = BAR_Y - BAR_H / 2
    if side > 0:
        ax.plot([t, t], [top_y, top_y + 1.1], color=col, lw=1.5,
                zorder=5, linestyle="--")
        ax.text(t, top_y + 1.25, label,
                ha="center", va="bottom",
                fontsize=7.5, color=col, linespacing=1.25, zorder=6)
    else:
        ax.plot([t, t], [bot_y - 1.1, bot_y], color=col, lw=1.5,
                zorder=5, linestyle="--")
        ax.text(t, bot_y - 1.25, label,
                ha="center", va="top",
                fontsize=7.5, color=col, linespacing=1.25, zorder=6)
    ax.plot(t, BAR_Y, "o", color=col, ms=5, zorder=6)

# ─── Time axis ────────────────────────────────────────────────────────────────
ax.plot([-1, T_MAX + 1], [BAR_Y - BAR_H / 2 - 2.0] * 2,
        color="#455A64", lw=1.5, zorder=1)

tick_major = [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130]
for t in tick_major:
    ax.plot([t, t], [BAR_Y - BAR_H / 2 - 2.0, BAR_Y - BAR_H / 2 - 1.85],
            color="#455A64", lw=1.2)
    ax.text(t, BAR_Y - BAR_H / 2 - 2.3, f"{t} s",
            ha="center", va="top", fontsize=7.5, color="#546E7A")

ax.text(T_MAX / 2, BAR_Y - BAR_H / 2 - 3.1, "Time from launch (seconds)",
        ha="center", va="top", fontsize=9, color="#37474F")

# ─── Title ────────────────────────────────────────────────────────────────────
ax.text(T_MAX / 2, 9.1, "N4 Pyro Firing Sequence Timeline",
        ha="center", va="center",
        fontsize=13, fontweight="bold", color="#1A1A1A")

ax.text(T_MAX / 2, 8.5,
        "Note: durations are illustrative – actual coasting / descent times depend on altitude and conditions",
        ha="center", va="center",
        fontsize=8, color="#757575", style="italic")

plt.tight_layout(pad=0.4)
for fmt in ("svg", "png"):
    plt.savefig(f"output/pyro_timing.{fmt}",
                format=fmt, bbox_inches="tight", dpi=150)
    print(f"  ✓  output/pyro_timing.{fmt}")
plt.close()
