"""
N4 Communication Architecture Diagram
======================================
Three-layer block diagram:
  Top    – N4 Flight Computer (ESP32) with MQTT / Beacon / XBee tasks
  Middle – CommunicationManager
  Bottom – Ground-station hardware nodes

Generates:
    output/comm_architecture.svg
    output/comm_architecture.png

Run:  python comm_architecture.py
"""

import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

os.makedirs("output", exist_ok=True)

# ─── Palette ──────────────────────────────────────────────────────────────────
MQTT_FC,   MQTT_EC   = "#E8F5E9", "#2E7D32"
BCN_FC,    BCN_EC    = "#FFF8E1", "#F57F17"
XBEE_FC,   XBEE_EC   = "#EDE7F6", "#6A1B9A"
CM_FC,     CM_EC     = "#ECEFF1", "#455A64"
ESP_FC,    ESP_EC    = "#FAFAFA", "#37474F"

FIG_W, FIG_H = 15, 10

fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))
ax.set_xlim(0, FIG_W)
ax.set_ylim(0, FIG_H)
ax.axis("off")
fig.patch.set_facecolor("white")


# ─── Helper: rounded rectangle + centred text ─────────────────────────────────
def draw_box(cx, cy, w, h, lines, fc, ec, fs=9.5, lw=2.0):
    """lines: list of (text, bold, colour) tuples or plain strings."""
    patch = FancyBboxPatch(
        (cx - w / 2, cy - h / 2), w, h,
        boxstyle="round,pad=0.12",
        facecolor=fc, edgecolor=ec, linewidth=lw, zorder=3,
    )
    ax.add_patch(patch)
    if isinstance(lines, str):
        lines = [(lines, True, ec)]
    n = len(lines)
    for i, item in enumerate(lines):
        if isinstance(item, str):
            text, bold, col = item, (i == 0), ec
        else:
            text, bold, col = item
        offset = (n - 1) * 0.16 / 2 - i * 0.16
        ax.text(cx, cy + offset, text,
                ha="center", va="center",
                fontsize=fs,
                fontweight="bold" if bold else "normal",
                color=col, zorder=4)


def draw_arrow(x1, y1, x2, y2, label="", lc="#607D8B", ls="-", lw=2.0):
    ax.annotate(
        "",
        xy=(x2, y2), xytext=(x1, y1),
        arrowprops=dict(
            arrowstyle="->",
            color=lc, lw=lw,
            mutation_scale=16,
            linestyle=ls,
        ),
        zorder=2,
    )
    if label:
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        ax.text(mx + 0.18, my, label,
                ha="left", va="center",
                fontsize=8, color="#444444",
                bbox=dict(boxstyle="round,pad=0.15",
                          facecolor="white", edgecolor="none", alpha=0.9),
                zorder=5)


# ─── ESP32 outline ────────────────────────────────────────────────────────────
esp_patch = FancyBboxPatch(
    (0.5, 5.2), 14.0, 4.3,
    boxstyle="round,pad=0.2",
    facecolor=ESP_FC, edgecolor=ESP_EC,
    linewidth=2.5, linestyle="--", zorder=1,
)
ax.add_patch(esp_patch)
ax.text(7.5, 9.2, "N4 FLIGHT COMPUTER  (ESP32 DevKit  –  FreeRTOS)",
        ha="center", va="center",
        fontsize=11, fontweight="bold", color="#263238", zorder=4)

# ─── Three task boxes ─────────────────────────────────────────────────────────
TASK_W, TASK_H = 3.4, 1.05

draw_box(3.0, 7.8, TASK_W, TASK_H,
         [("MQTT Task", True, MQTT_EC), ("WiFi STA · Core 1 · Pri 2", False, "#555")],
         MQTT_FC, MQTT_EC)

draw_box(7.5, 7.8, TASK_W, TASK_H,
         [("Beacon Task", True, BCN_EC), ("Raw 802.11 + ESP-NOW · Core 1 · Pri 2", False, "#555")],
         BCN_FC, BCN_EC)

draw_box(12.0, 7.8, TASK_W, TASK_H,
         [("XBee Task", True, XBEE_EC), ("UART transparent · Core 1 · Pri 2", False, "#555")],
         XBEE_FC, XBEE_EC)

# ─── CommunicationManager ─────────────────────────────────────────────────────
CM_Y = 6.1
draw_box(7.5, CM_Y, 12.8, 0.85,
         [("CommunicationManager", True, CM_EC),
          ("use_mqtt_mode  ·  use_beacon_mode  ·  use_xbee_mode  ·  RSSI monitoring", False, "#546E7A")],
         CM_FC, CM_EC, fs=9, lw=2.2)

# Connectors: task boxes → CommunicationManager
for tx in [3.0, 7.5, 12.0]:
    ax.plot([tx, tx],   [7.8 - TASK_H / 2, CM_Y + 0.43],
            color="#90A4AE", lw=1.6, zorder=2)

# ─── Downward arrows (ESP32 → ground) ────────────────────────────────────────
GND_Y_TOP = 4.4
draw_arrow(3.0,  CM_Y - 0.43, 3.0,  GND_Y_TOP,
           "WiFi 2.4 GHz\n~50–100 m", MQTT_EC)
draw_arrow(7.5,  CM_Y - 0.43, 7.5,  GND_Y_TOP,
           "ESP-NOW + Raw 802.11\n~2–4 km LOS", BCN_EC)
draw_arrow(12.0, CM_Y - 0.43, 12.0, GND_Y_TOP,
           "900 MHz serial\n1–30 km LOS", XBEE_EC)

# ─── Ground-station boxes ─────────────────────────────────────────────────────
GND_W, GND_H = 3.4, 1.3
GND_Y = 3.3

draw_box(3.0, GND_Y, GND_W, GND_H,
         [("MQTT Broker", True, MQTT_EC),
          ("Laptop / Raspberry Pi", False, "#555"),
          ("Port 1883 / WebSocket", False, "#777")],
         MQTT_FC, MQTT_EC)

draw_box(7.5, GND_Y, GND_W, GND_H,
         [("Base Station (Beacon)", True, BCN_EC),
          ("ESP32 beacon receiver", False, "#555"),
          ("RSSI + telemetry decode", False, "#777")],
         BCN_FC, BCN_EC)

draw_box(12.0, GND_Y, GND_W, GND_H,
         [("Base Station (XBee)", True, XBEE_EC),
          ("XBee Pro 900HP + BT HC-06", False, "#555"),
          ("UART → USB / Bluetooth", False, "#777")],
         XBEE_FC, XBEE_EC)

# ─── Mode labels ──────────────────────────────────────────────────────────────
for cx, txt in [(3.0, "Primary · low-latency"), (7.5, "Long-range link"), (12.0, "Fallback / RF backup")]:
    ax.text(cx, 2.35, txt,
            ha="center", va="center", fontsize=7.5, color="#777777", style="italic")

# ─── Title ────────────────────────────────────────────────────────────────────
ax.text(7.5, 1.05, "N4 Communication Architecture",
        ha="center", va="center",
        fontsize=13, fontweight="bold", color="#1A1A1A")

plt.tight_layout(pad=0.3)
for fmt in ("svg", "png"):
    plt.savefig(f"output/comm_architecture.{fmt}",
                format=fmt, bbox_inches="tight", dpi=150)
    print(f"  ✓  output/comm_architecture.{fmt}")
plt.close()
