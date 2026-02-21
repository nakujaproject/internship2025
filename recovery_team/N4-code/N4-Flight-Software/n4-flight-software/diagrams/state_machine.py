"""
N4 Flight Computer – Flight State Machine
==========================================
Generates:
    output/state_machine.svg
    output/state_machine.png

Run:  python state_machine.py
"""

import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

os.makedirs("output", exist_ok=True)

# ─── Palette ──────────────────────────────────────────────────────────────────
C = {
    "ground":  dict(face="#D6E9FF", edge="#1565C0"),
    "flight":  dict(face="#E8F5E9", edge="#2E7D32"),
    "apogee":  dict(face="#FFF8E1", edge="#F57F17"),
    "pyro":    dict(face="#FFEBEE", edge="#C62828"),
    "arrow":   "#607D8B",
    "note":    "#B71C1C",
    "label":   "#37474F",
}

# ─── State definitions  (id, display_label, y_centre, palette_key, side_note) ─
STATES = [
    ("pre",        "PRE_FLIGHT_GROUND",    18.5, "ground", None),
    ("powered",    "POWERED_FLIGHT",       16.5, "flight", None),
    ("coasting",   "COASTING",             14.5, "flight", None),
    ("apogee",     "APOGEE",               12.5, "apogee", None),
    ("drogue_dep", "DROGUE_DEPLOY",        10.5, "pyro",   "⚡  GPIO 25 · 5 s PWM"),
    ("drogue_des", "DROGUE_DESCENT",        8.5, "flight", None),
    ("main_dep",   "MAIN_DEPLOY",           6.5, "pyro",   "⚡  GPIO 12 · 5 s PWM"),
    ("main_des",   "MAIN_DESCENT",          4.5, "flight", None),
    ("post",       "POST_FLIGHT_GROUND",    2.5, "ground", None),
]

# ─── Transition labels ─────────────────────────────────────────────────────────
TRANSITIONS = [
    ("pre",        "powered",    "altitude rises  >10 m"),
    ("powered",    "coasting",   "motor burnout / net decel"),
    ("coasting",   "apogee",     "Kalman vel. reversal  (±3 m/s)"),
    ("apogee",     "drogue_dep", "+1 500 ms delay"),
    ("drogue_dep", "drogue_des", "PWM complete"),
    ("drogue_des", "main_dep",   "altitude < 500 m AGL"),
    ("main_dep",   "main_des",   "PWM complete"),
    ("main_des",   "post",       "near-zero velocity"),
]

# ─── Layout constants ─────────────────────────────────────────────────────────
CX = 4.5          # horizontal centre of all state boxes
BW = 3.5          # box width
BH = 0.70         # box height
FIG_W, FIG_H = 11, 20

fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))
ax.set_xlim(0, FIG_W)
ax.set_ylim(1.2, 20.0)
ax.axis("off")
fig.patch.set_facecolor("white")

state_top  = {sid: y + BH / 2 for sid, _, y, *_ in STATES}
state_bot  = {sid: y - BH / 2 for sid, _, y, *_ in STATES}

# ─── Draw state boxes ─────────────────────────────────────────────────────────
for sid, label, y, ckey, note in STATES:
    fc, ec = C[ckey]["face"], C[ckey]["edge"]
    patch = FancyBboxPatch(
        (CX - BW / 2, y - BH / 2), BW, BH,
        boxstyle="round,pad=0.10",
        facecolor=fc, edgecolor=ec, linewidth=2.2, zorder=3,
    )
    ax.add_patch(patch)
    ax.text(CX, y, label,
            ha="center", va="center",
            fontsize=10, fontweight="bold", color=ec,
            fontfamily="monospace", zorder=4)
    if note:
        ax.text(CX + BW / 2 + 0.25, y, note,
                ha="left", va="center",
                fontsize=8.5, color=C["note"], style="italic", zorder=4)

# ─── Draw transitions ─────────────────────────────────────────────────────────
for from_id, to_id, tlabel in TRANSITIONS:
    y1 = state_bot[from_id] - 0.04
    y2 = state_top[to_id]   + 0.04
    mid_y = (y1 + y2) / 2

    ax.annotate(
        "",
        xy=(CX, y2), xytext=(CX, y1),
        arrowprops=dict(
            arrowstyle="->",
            color=C["arrow"],
            lw=1.8,
            mutation_scale=16,
        ),
        zorder=2,
    )

    ax.text(
        CX + BW / 2 + 0.25, mid_y,
        tlabel,
        ha="left", va="center",
        fontsize=8, color=C["label"],
        linespacing=1.3,
        zorder=4,
    )

# ─── Title ────────────────────────────────────────────────────────────────────
ax.text(CX, 19.4, "N4 Flight State Machine",
        ha="center", va="center",
        fontsize=14, fontweight="bold", color="#1A1A1A",
        bbox=dict(boxstyle="round,pad=0.35", facecolor="#F5F5F5", edgecolor="#BDBDBD"))

# ─── Legend ───────────────────────────────────────────────────────────────────
legend_items = [
    ("Ground / Idle states",  C["ground"]["face"], C["ground"]["edge"]),
    ("In-flight states",      C["flight"]["face"], C["flight"]["edge"]),
    ("Apogee detection",      C["apogee"]["face"], C["apogee"]["edge"]),
    ("Pyro-fire states",      C["pyro"]["face"],   C["pyro"]["edge"]),
]
lx, ly = 0.4, 1.85
for txt, fc, ec in legend_items:
    patch = FancyBboxPatch((lx, ly - 0.18), 0.45, 0.32,
                           boxstyle="round,pad=0.05",
                           facecolor=fc, edgecolor=ec, linewidth=1.5, zorder=3)
    ax.add_patch(patch)
    ax.text(lx + 0.6, ly, txt, ha="left", va="center", fontsize=8, color="#333333")
    lx += 2.5

plt.tight_layout(pad=0.5)
for fmt in ("svg", "png"):
    plt.savefig(f"output/state_machine.{fmt}",
                format=fmt, bbox_inches="tight", dpi=150)
    print(f"  ✓  output/state_machine.{fmt}")
plt.close()
