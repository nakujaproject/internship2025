"""
N4 Flight Software — Pyro Ejection Sequence Diagram
Generates: output/pyro_sequence_diagram.png
"""
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import os

os.makedirs(os.path.join(os.path.dirname(__file__), "output"), exist_ok=True)
OUTPUT = os.path.join(os.path.dirname(__file__), "output", "pyro_sequence_diagram.png")

# ── Colours ──────────────────────────────────────────────────────
BG           = "#0d1117"
COLOR_APOGEE = "#3b2a6e"
COLOR_WAIT   = "#1c3a5e"
COLOR_PYRO   = "#5c1f1f"
COLOR_DESCENT= "#1a3a2a"
COLOR_GROUND = "#1c3a5e"
COLOR_COND   = "#1c2a3e"
BORDER       = "#58a6ff"
BORDER_PYRO  = "#ff7b72"
TEXT_MAIN    = "#e6edf3"
TEXT_DIM     = "#8b949e"
TEXT_PYRO    = "#ff7b72"
ARROW_COLOR  = "#58a6ff"
ARROW_PYRO   = "#ff7b72"

# ── Step definitions ─────────────────────────────────────────────
# (label, sublabel, color, border_color, is_condition)
STEPS = [
    ("APOGEE DETECTED",             "Kalman velocity reversal confirmed",
     COLOR_APOGEE, BORDER, False),

    ("Wait 1500 ms",                  "DROGUE_DEPLOY_DELAY_MS",
     COLOR_WAIT,   BORDER, True),

    ("⚡  DROGUE_DEPLOY",            "Fire GPIO 25 · 5 s PWM · Set DROGUE_DEPLOY_FLAG = 1",
     COLOR_PYRO,   BORDER_PYRO, False),

    ("DROGUE_DESCENT",              "Monitoring Kalman-filtered altitude",
     COLOR_DESCENT, BORDER, False),

    ("Altitude < 500 m AGL",         "MAIN_EJECTION_HEIGHT threshold crossed",
     COLOR_WAIT,   BORDER, True),

    ("⚡  MAIN_DEPLOY",              "Fire GPIO 12 · 5 s PWM · Set MAIN_CHUTE_EJECT_FLAG = 1",
     COLOR_PYRO,   BORDER_PYRO, False),

    ("MAIN_DESCENT",                "Monitoring Kalman vertical velocity",
     COLOR_DESCENT, BORDER, False),

    ("Near-zero velocity",           "Landing detected",
     COLOR_WAIT,   BORDER, True),

    ("POST_FLIGHT_GROUND",          "Logging stopped · System idle",
     COLOR_GROUND, BORDER, False),
]

# ── Layout ────────────────────────────────────────────────────────
N       = len(STEPS)
BOX_W   = 7.2
BOX_H   = 0.85
BOX_X   = 1.65
GAP     = 1.65
FIG_H   = N * GAP + 1.6

fig, ax = plt.subplots(figsize=(11, FIG_H))
ax.set_xlim(0, 11)
ax.set_ylim(-0.3, FIG_H)
ax.axis("off")
fig.patch.set_facecolor(BG)
ax.set_facecolor(BG)


def step_y(i):
    return FIG_H - 1.3 - i * GAP


for i, (label, sublabel, color, border_color, is_cond) in enumerate(STEPS):
    y = step_y(i)

    # ── Box (conditions get a slightly different shape/dash) ──────
    linestyle = (0, (4, 2)) if is_cond else "solid"
    alpha     = 0.70        if is_cond else 1.0
    p = FancyBboxPatch(
        (BOX_X, y), BOX_W, BOX_H,
        boxstyle="round,pad=0.07",
        facecolor=color, edgecolor=border_color,
        linewidth=1.8, alpha=alpha, zorder=3,
        linestyle=linestyle,
    )
    ax.add_patch(p)

    ty = y + BOX_H / 2 + (0.12 if sublabel else 0)
    txt_col = TEXT_PYRO if (not is_cond and "⚡" in label) else TEXT_MAIN
    ax.text(BOX_X + BOX_W / 2, ty, label,
            ha="center", va="center", color=txt_col,
            fontsize=10, fontweight="bold", zorder=4)
    if sublabel:
        ax.text(BOX_X + BOX_W / 2, y + BOX_H / 2 - 0.20, sublabel,
                ha="center", va="center", color=TEXT_DIM,
                fontsize=8, zorder=4)

    # ── Arrow to next step ────────────────────────────────────────
    if i < N - 1:
        ax_x   = BOX_X + BOX_W / 2
        y_top  = y
        y_bot  = step_y(i + 1) + BOX_H
        a_col  = ARROW_PYRO if (not is_cond and "⚡" in label) else ARROW_COLOR

        ax.plot([ax_x, ax_x], [y_top, y_bot + 0.12],
                color=a_col, lw=1.8, zorder=2)
        ax.annotate(
            "", xy=(ax_x, y_bot),
            xytext=(ax_x, y_bot + 0.12),
            arrowprops=dict(arrowstyle="-|>", color=a_col,
                            lw=1.8, mutation_scale=13),
            zorder=3,
        )

# ── GPIO labels in right margin ───────────────────────────────────
pyro_labels = [
    (2, "GPIO 25\nLEDC ch.3"),
    (5, "GPIO 12\nLEDC ch.4"),
]
for step_i, gpio_lbl in pyro_labels:
    y = step_y(step_i) + BOX_H / 2
    ax.text(BOX_X + BOX_W + 0.20, y, gpio_lbl,
            ha="left", va="center", color=TEXT_PYRO,
            fontsize=8.5, fontweight="bold", zorder=4)
    # bracket line
    ax.plot([BOX_X + BOX_W + 0.08, BOX_X + BOX_W + 0.08],
            [y - 0.3, y + 0.3],
            color=BORDER_PYRO, lw=1.5, zorder=3)

# ── Title ─────────────────────────────────────────────────────────
ax.text(BOX_X + BOX_W / 2, FIG_H - 0.5,
        "N4 Pyro Ejection Sequence",
        ha="center", va="center", color=BORDER,
        fontsize=14, fontweight="bold", zorder=4)

# ── Legend ────────────────────────────────────────────────────────
leg_x, leg_y = 0.1, FIG_H - 1.15
legend_items = [
    (COLOR_PYRO,   BORDER_PYRO, "solid",      "Pyro deployment state"),
    (COLOR_DESCENT, BORDER,     "solid",      "Descent / flight state"),
    (COLOR_WAIT,   BORDER,      (0, (4, 2)), "Transition condition / delay"),
]
for fc, ec, ls, lbl in legend_items:
    p = FancyBboxPatch((leg_x, leg_y - 0.18), 0.28, 0.32,
                       boxstyle="round,pad=0.03",
                       facecolor=fc, edgecolor=ec,
                       linewidth=1.2, zorder=4, linestyle=ls)
    ax.add_patch(p)
    ax.text(leg_x + 0.42, leg_y, lbl,
            ha="left", va="center", color=TEXT_DIM,
            fontsize=8, zorder=4)
    leg_y -= 0.44

# ── Save ──────────────────────────────────────────────────────────
plt.tight_layout(pad=0.3)
plt.savefig(OUTPUT, dpi=200, bbox_inches="tight", facecolor=BG)
print(f"Saved  {OUTPUT}")
plt.close()
