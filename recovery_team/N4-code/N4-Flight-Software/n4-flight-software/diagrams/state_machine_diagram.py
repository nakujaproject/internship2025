"""
N4 Flight Software — Flight State Machine Diagram
Generates: output/state_machine_diagram.png
"""
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import os

os.makedirs(os.path.join(os.path.dirname(__file__), "output"), exist_ok=True)
OUTPUT = os.path.join(os.path.dirname(__file__), "output", "state_machine_diagram.png")

# ── Colour palette ──────────────────────────────────────────────
BG           = "#0d1117"
COLOR_IDLE   = "#1c3a5e"   # pre/post ground states
COLOR_FLIGHT = "#1a3a2a"   # powered / coasting / descent
COLOR_APOGEE = "#3b2a6e"   # apogee
COLOR_PYRO   = "#5c1f1f"   # deployment states
BORDER       = "#58a6ff"
TEXT_MAIN    = "#e6edf3"
TEXT_DIM     = "#8b949e"
ARROW_COLOR  = "#58a6ff"
COND_COLOR   = "#8b949e"

STATES = [
    # (id, name, colour, transition_condition)
    (0, "PRE_FLIGHT_GROUND",   COLOR_IDLE,   "altitude rises > 10 m  (LAUNCH_DETECTION_THRESHOLD)"),
    (1, "POWERED_FLIGHT",      COLOR_FLIGHT, "motor burnout / deceleration detected"),
    (2, "COASTING",            COLOR_FLIGHT, "Kalman velocity reversal  (APOGEE_DETECTION_THRESHOLD = 3 m)"),
    (3, "APOGEE",              COLOR_APOGEE, "+ 1500 ms delay  (DROGUE_DEPLOY_DELAY_MS)"),
    (4, "DROGUE_DEPLOY",       COLOR_PYRO,   "GPIO 25 fired  →  automatic transition"),
    (5, "DROGUE_DESCENT",      COLOR_FLIGHT, "filtered altitude < 500 m AGL  (MAIN_EJECTION_HEIGHT)"),
    (6, "MAIN_DEPLOY",         COLOR_PYRO,   "GPIO 12 fired  →  automatic transition"),
    (7, "MAIN_DESCENT",        COLOR_FLIGHT, "near-zero Kalman vertical velocity"),
    (8, "POST_FLIGHT_GROUND",  COLOR_IDLE,   None),   # terminal state
]

# ── Layout constants ──────────────────────────────────────────────
BOX_W    = 7.0
BOX_H    = 0.82
BOX_X    = 1.8
GAP      = 1.90   # centre-to-centre spacing
N        = len(STATES)
FIG_H    = N * GAP + 1.8

fig, ax = plt.subplots(figsize=(11.5, FIG_H))
ax.set_xlim(0, 11.5)
ax.set_ylim(-0.5, FIG_H)
ax.axis("off")
fig.patch.set_facecolor(BG)
ax.set_facecolor(BG)


def state_y(i):
    """Y coordinate of the bottom edge of state i (top-to-bottom ordering)."""
    return FIG_H - 1.4 - i * GAP


for i, (sid, name, color, cond) in enumerate(STATES):
    y = state_y(i)

    # ── Box ──────────────────────────────────────────────────────
    p = FancyBboxPatch(
        (BOX_X, y), BOX_W, BOX_H,
        boxstyle="round,pad=0.07",
        facecolor=color, edgecolor=BORDER,
        linewidth=1.6, zorder=3,
    )
    ax.add_patch(p)

    # State number badge
    badge = FancyBboxPatch(
        (BOX_X + 0.08, y + 0.14), 0.55, 0.55,
        boxstyle="round,pad=0.04",
        facecolor=BG, edgecolor=BORDER,
        linewidth=1.2, zorder=4,
    )
    ax.add_patch(badge)
    ax.text(BOX_X + 0.35, y + BOX_H / 2 - 0.01, str(sid),
            ha="center", va="center", color=BORDER,
            fontsize=9.5, fontweight="bold", zorder=5)

    # State name
    ax.text(BOX_X + BOX_W / 2 + 0.2, y + BOX_H / 2, name,
            ha="center", va="center", color=TEXT_MAIN,
            fontsize=10, fontweight="bold", zorder=4)

    # Pyro annotation badge on DROGUE_DEPLOY and MAIN_DEPLOY
    if sid == 4:
        ax.text(BOX_X + BOX_W - 0.15, y + BOX_H / 2, "⚡ DROGUE",
                ha="right", va="center", color="#ff7b72",
                fontsize=8, fontweight="bold", zorder=4)
    elif sid == 6:
        ax.text(BOX_X + BOX_W - 0.15, y + BOX_H / 2, "⚡ MAIN",
                ha="right", va="center", color="#ff7b72",
                fontsize=8, fontweight="bold", zorder=4)

    # ── Arrow + condition ────────────────────────────────────────
    if cond:
        arrow_x = BOX_X + BOX_W / 2
        y_arrow_start = y
        y_arrow_end   = y - GAP + BOX_H

        # Vertical line segment
        ax.plot([arrow_x, arrow_x],
                [y_arrow_start, y_arrow_end + 0.12],
                color=ARROW_COLOR, lw=1.8, zorder=2)

        # Arrowhead
        ax.annotate(
            "", xy=(arrow_x, y_arrow_end),
            xytext=(arrow_x, y_arrow_end + 0.12),
            arrowprops=dict(arrowstyle="-|>", color=ARROW_COLOR,
                            lw=1.8, mutation_scale=13),
            zorder=3,
        )

        # Condition label (right of arrow)
        mid_y = (y_arrow_start + y_arrow_end) / 2
        ax.text(arrow_x + 0.28, mid_y, cond,
                ha="left", va="center", color=COND_COLOR,
                fontsize=8, style="italic", zorder=4)

# ── Title ─────────────────────────────────────────────────────────
ax.text(BOX_X + BOX_W / 2, FIG_H - 0.55,
        "N4 Flight State Machine",
        ha="center", va="center", color=BORDER,
        fontsize=14, fontweight="bold", zorder=4)

# ── Legend ────────────────────────────────────────────────────────
legend_items = [
    (COLOR_IDLE,   "Ground / idle"),
    (COLOR_FLIGHT, "Active flight"),
    (COLOR_APOGEE, "Apogee hold"),
    (COLOR_PYRO,   "Pyro deployment"),
]
lx, ly = 0.05, FIG_H - 1.3
for col, lbl in legend_items:
    p = FancyBboxPatch((lx, ly - 0.16), 0.28, 0.3,
                       boxstyle="round,pad=0.03",
                       facecolor=col, edgecolor=BORDER,
                       linewidth=1.2, zorder=4)
    ax.add_patch(p)
    ax.text(lx + 0.38, ly, lbl,
            ha="left", va="center", color=TEXT_DIM,
            fontsize=8, zorder=4)
    ly -= 0.45

# ── Save ──────────────────────────────────────────────────────────
plt.tight_layout(pad=0.3)
plt.savefig(OUTPUT, dpi=200, bbox_inches="tight", facecolor=BG)
print(f"Saved  {OUTPUT}")
plt.close()
