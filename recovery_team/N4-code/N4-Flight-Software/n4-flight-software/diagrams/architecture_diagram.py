"""
N4 Flight Software — System Architecture Diagram
Generates: output/architecture_diagram.png
"""
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch
import os

os.makedirs(os.path.join(os.path.dirname(__file__), "output"), exist_ok=True)
OUTPUT = os.path.join(os.path.dirname(__file__), "output", "architecture_diagram.png")

# ── Colour palette ──────────────────────────────────────────────
BG          = "#0d1117"
ESP32_BG    = "#161b22"
TASK_BG     = "#1c3a5e"
MANAGER_BG  = "#3b2a6e"
GROUND_BG   = "#1a3a2a"
BORDER      = "#58a6ff"
TEXT_MAIN   = "#e6edf3"
TEXT_DIM    = "#8b949e"
ARROW_MQTT  = "#58a6ff"
ARROW_BCN   = "#3fb950"
ARROW_XBEE  = "#f78166"

# ── Canvas ───────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(15, 11))
ax.set_xlim(0, 15)
ax.set_ylim(0, 11)
ax.axis("off")
fig.patch.set_facecolor(BG)
ax.set_facecolor(BG)


def box(x, y, w, h, color, title, subtitle=None, title_size=10):
    """Draw a rounded box with optional subtitle."""
    p = FancyBboxPatch(
        (x, y), w, h,
        boxstyle="round,pad=0.07",
        facecolor=color, edgecolor=BORDER,
        linewidth=1.6, zorder=3,
    )
    ax.add_patch(p)
    ty = y + h / 2 + (0.14 if subtitle else 0)
    ax.text(x + w / 2, ty, title,
            ha="center", va="center", color=TEXT_MAIN,
            fontsize=title_size, fontweight="bold", zorder=4)
    if subtitle:
        ax.text(x + w / 2, y + h / 2 - 0.22, subtitle,
                ha="center", va="center", color=TEXT_DIM,
                fontsize=title_size - 1.5, zorder=4)


def arrow(ax, x, y0, y1, color, label=None):
    """Draw a vertical arrow from y0 down to y1, with optional mid label."""
    ax.annotate(
        "", xy=(x, y1), xytext=(x, y0),
        arrowprops=dict(arrowstyle="-|>", color=color, lw=2.0,
                        mutation_scale=14),
        zorder=2,
    )
    if label:
        mid = (y0 + y1) / 2
        ax.text(x + 0.18, mid, label,
                ha="left", va="center", color=TEXT_DIM,
                fontsize=8, zorder=4)


# ── Outer ESP32 border box ────────────────────────────────────────
outer = FancyBboxPatch(
    (0.4, 4.2), 14.2, 6.5,
    boxstyle="round,pad=0.12",
    facecolor=ESP32_BG, edgecolor=BORDER,
    linewidth=2.5, zorder=1,
)
ax.add_patch(outer)
ax.text(7.5, 10.35, "N4 FLIGHT COMPUTER  (ESP32 DevKit)",
        ha="center", va="center", color=BORDER,
        fontsize=13, fontweight="bold", zorder=4)

# ── FreeRTOS Task boxes (top row, inside ESP32 box) ───────────────
# MQTT Task
box(0.8,  8.0, 3.0, 1.5, TASK_BG, "MQTT Task",  "(WiFi STA)")
# Beacon Task
box(5.25, 8.0, 4.0, 1.5, TASK_BG, "Beacon Task", "(802.11 raw + ESP-NOW)")
# XBee Task
box(10.9, 8.0, 3.2, 1.5, TASK_BG, "XBee Task",  "(UART1, transparent)")

# Task centre x-values
cx_mqtt  = 0.8  + 3.0 / 2   # 2.30
cx_bcn   = 5.25 + 4.0 / 2   # 7.25
cx_xbee  = 10.9 + 3.2 / 2   # 12.50

# ── Arrows: tasks → CommunicationManager ─────────────────────────
for cx, col in [(cx_mqtt, ARROW_MQTT), (cx_bcn, ARROW_BCN), (cx_xbee, ARROW_XBEE)]:
    arrow(ax, cx, 8.0, 7.1, col)

# ── CommunicationManager box ──────────────────────────────────────
box(1.2, 5.5, 12.6, 1.4, MANAGER_BG,
    "CommunicationManager",
    "use_mqtt_mode  |  use_beacon_mode  |  use_xbee_mode",
    title_size=11)

# ── Arrows: manager → transport labels → ground stations ──────────
transport = [
    (cx_mqtt,  ARROW_MQTT, "WiFi / MQTT"),
    (cx_bcn,   ARROW_BCN,  "ESP-NOW +\nRaw 802.11"),
    (cx_xbee,  ARROW_XBEE, "XBee Pro 900HP\n900 MHz UART"),
]
for cx, col, lbl in transport:
    arrow(ax, cx, 5.5, 4.1, col)
    ax.text(cx, 4.8, lbl,
            ha="center", va="center", color=col,
            fontsize=8.5, fontweight="bold", zorder=4)

# ── Ground station boxes (below ESP32 border) ─────────────────────
box(0.8,  0.6, 3.0, 1.6, GROUND_BG, "MQTT Broker",    "(Laptop / Pi)")
box(5.25, 0.6, 4.0, 1.6, GROUND_BG, "Base Station",   "ESP32")
box(10.9, 0.6, 3.2, 1.6, GROUND_BG, "Base Station",   "ESP32  (XBee + BT)")

# Arrows into ground stations
for cx, col in [(cx_mqtt, ARROW_MQTT), (cx_bcn, ARROW_BCN), (cx_xbee, ARROW_XBEE)]:
    arrow(ax, cx, 4.2, 2.2, col)

ax.text(7.5, 0.15, "GROUND STATION",
        ha="center", va="center", color=TEXT_DIM,
        fontsize=10, fontstyle="italic", zorder=4)

# ── Legend ────────────────────────────────────────────────────────
leg_x, leg_y = 0.6, 3.7
for col, lbl in [(ARROW_MQTT, "MQTT (WiFi 2.4 GHz)"),
                 (ARROW_BCN,  "Beacon (802.11 raw + ESP-NOW)"),
                 (ARROW_XBEE, "XBee (900 MHz UART)")]:
    ax.plot([leg_x, leg_x + 0.4], [leg_y, leg_y], color=col, lw=2, zorder=4)
    ax.text(leg_x + 0.55, leg_y, lbl,
            ha="left", va="center", color=TEXT_DIM, fontsize=8.5, zorder=4)
    leg_y -= 0.4

# ── Save ──────────────────────────────────────────────────────────
plt.tight_layout(pad=0.3)
plt.savefig(OUTPUT, dpi=200, bbox_inches="tight", facecolor=BG)
print(f"Saved  {OUTPUT}")
plt.close()
