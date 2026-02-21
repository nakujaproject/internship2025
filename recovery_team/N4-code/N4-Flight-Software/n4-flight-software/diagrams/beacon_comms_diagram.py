"""
N4 Flight Software — Beacon Communication Link Diagram
Generates: output/beacon_comms_diagram.png
"""
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import os

os.makedirs(os.path.join(os.path.dirname(__file__), "output"), exist_ok=True)
OUTPUT = os.path.join(os.path.dirname(__file__), "output", "beacon_comms_diagram.png")

# ── Colours ──────────────────────────────────────────────────────
BG           = "#0d1117"
ROCKET_COLOR = "#5c1f1f"
GROUND_COLOR = "#1a3a2a"
BORDER       = "#58a6ff"
TEXT_MAIN    = "#e6edf3"
TEXT_DIM     = "#8b949e"
ARROW_TLM    = "#3fb950"   # green  — telemetry (rocket → ground)
ARROW_CMD    = "#f78166"   # orange — commands  (ground → rocket)

# ── Canvas ───────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(13, 5.5))
ax.set_xlim(0, 13)
ax.set_ylim(0, 5.5)
ax.axis("off")
fig.patch.set_facecolor(BG)
ax.set_facecolor(BG)


def device_box(x, y, w, h, color, line1, line2):
    p = FancyBboxPatch(
        (x, y), w, h,
        boxstyle="round,pad=0.1",
        facecolor=color, edgecolor=BORDER,
        linewidth=2.0, zorder=3,
    )
    ax.add_patch(p)
    ax.text(x + w / 2, y + h / 2 + 0.10, line1,
            ha="center", va="center", color=TEXT_MAIN,
            fontsize=12, fontweight="bold", zorder=4)
    ax.text(x + w / 2, y + h / 2 - 0.30, line2,
            ha="center", va="center", color=TEXT_DIM,
            fontsize=9.5, zorder=4)


device_box(0.3,  1.2, 2.8, 2.8, ROCKET_COLOR, "ROCKET",       "ESP32")
device_box(9.9,  1.2, 2.8, 2.8, GROUND_COLOR, "BASE STATION", "ESP32")

# ── Telemetry arrow (left→right, upper lane) ─────────────────────
ax.annotate(
    "", xy=(9.9, 3.45), xytext=(3.1, 3.45),
    arrowprops=dict(arrowstyle="-|>", color=ARROW_TLM,
                    lw=2.5, mutation_scale=16),
    zorder=2,
)
ax.text(6.5, 3.85, "Raw 802.11 Beacon Frame",
        ha="center", va="center", color=ARROW_TLM,
        fontsize=11, fontweight="bold", zorder=4)
ax.text(6.5, 3.50, "Telemetry  ·  No ACK required  ·  Proven range: 4 km LOS",
        ha="center", va="center", color=TEXT_DIM,
        fontsize=8.5, zorder=4)

# ── Command arrow (right→left, lower lane) ───────────────────────
ax.annotate(
    "", xy=(3.1, 2.05), xytext=(9.9, 2.05),
    arrowprops=dict(arrowstyle="-|>", color=ARROW_CMD,
                    lw=2.5, mutation_scale=16),
    zorder=2,
)
ax.text(6.5, 2.45, "ESP-NOW Unicast Packet",
        ha="center", va="center", color=ARROW_CMD,
        fontsize=11, fontweight="bold", zorder=4)
ax.text(6.5, 2.05, "Commands  ·  ACK'd delivery  ·  Uplink (ground → rocket)",
        ha="center", va="center", color=TEXT_DIM,
        fontsize=8.5, zorder=4)

# ── Channel/frequency badge ──────────────────────────────────────
badge = FancyBboxPatch(
    (5.4, 0.2), 2.2, 0.75,
    boxstyle="round,pad=0.06",
    facecolor="#1c3a5e", edgecolor=BORDER,
    linewidth=1.2, zorder=3,
)
ax.add_patch(badge)
ax.text(6.5, 0.58, "2.4 GHz ISM  ·  Channel 1  (2.412 GHz)",
        ha="center", va="center", color=BORDER,
        fontsize=8.5, fontweight="bold", zorder=4)

# ── Title ─────────────────────────────────────────────────────────
ax.text(6.5, 5.1, "N4 Beacon Communication Link",
        ha="center", va="center", color=BORDER,
        fontsize=14, fontweight="bold", zorder=4)

# ── Save ──────────────────────────────────────────────────────────
plt.tight_layout(pad=0.3)
plt.savefig(OUTPUT, dpi=200, bbox_inches="tight", facecolor=BG)
print(f"Saved  {OUTPUT}")
plt.close()
