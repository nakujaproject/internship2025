"""
N4 FreeRTOS Task & Queue Data-Flow Diagram
==========================================
Four-column layout:
  Col A – Sensor tasks (Core 0)
  Col B – FreeRTOS queues
  Col C – Processing (Kalman + State Machine, Core 0)
  Col D – Output tasks  (Core 1) + Storage

Generates:
    output/task_queue.svg
    output/task_queue.png

Run:  python task_queue.py
"""

import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

os.makedirs("output", exist_ok=True)

# ─── Palette ──────────────────────────────────────────────────────────────────
TASK_FC,  TASK_EC  = "#E3F2FD", "#1565C0"
PROC_FC,  PROC_EC  = "#FFF8E1", "#E65100"
OUT_FC,   OUT_EC   = "#E8F5E9", "#2E7D32"
QUEUE_FC, QUEUE_EC = "#F3E5F5", "#7B1FA2"
STORE_FC, STORE_EC = "#FFF3E0", "#BF360C"
ARROW_C = "#78909C"

FIG_W, FIG_H = 18, 12

fig, ax = plt.subplots(figsize=(FIG_W, FIG_H))
ax.set_xlim(0, FIG_W)
ax.set_ylim(0.5, FIG_H)
ax.axis("off")
fig.patch.set_facecolor("white")


# ─── Helpers ──────────────────────────────────────────────────────────────────
def box(cx, cy, w, h, lines, fc, ec, fs=8.5, lw=1.8):
    patch = FancyBboxPatch(
        (cx - w / 2, cy - h / 2), w, h,
        boxstyle="round,pad=0.10",
        facecolor=fc, edgecolor=ec, linewidth=lw, zorder=3,
    )
    ax.add_patch(patch)
    if isinstance(lines, str):
        lines = [lines]
    n = len(lines)
    for i, txt in enumerate(lines):
        offset = (n - 1) * 0.14 / 2 - i * 0.14
        ax.text(cx, cy + offset, txt,
                ha="center", va="center",
                fontsize=fs,
                fontweight="bold" if i == 0 else "normal",
                color=ec, zorder=4)


def arr(x1, y1, x2, y2, label=""):
    ax.annotate(
        "",
        xy=(x2, y2), xytext=(x1, y1),
        arrowprops=dict(arrowstyle="->", color=ARROW_C, lw=1.4,
                        mutation_scale=13),
        zorder=2,
    )
    if label:
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        ax.text(mx, my + 0.09, label,
                ha="center", va="bottom", fontsize=7, color="#555",
                bbox=dict(boxstyle="round,pad=0.08", facecolor="white",
                          edgecolor="none", alpha=0.85),
                zorder=5)


# ─── Column x-centres ─────────────────────────────────────────────────────────
CA = 2.2    # Sensor tasks
CB = 7.0    # Queues
CC = 11.8   # Processing
CD = 16.0   # Output / Storage

TW = 3.2    # task box width
TH = 0.82   # task box height
QW = 2.8    # queue box width
QH = 0.68   # queue box height

# ─── Column A: Sensor Tasks ───────────────────────────────────────────────────
SENS = [
    (10.2, "altimeter_task",  "Core 0 · BMP388"),
    ( 8.2, "gyroscope_task",  "Core 0 · MPU-6050"),
    ( 6.2, "gps_task",        "Core 0 · NMEA UART"),
]
for cy, name, sub in SENS:
    box(CA, cy, TW, TH, [name, sub], TASK_FC, TASK_EC)

# ─── Column B: Queues ─────────────────────────────────────────────────────────
QUEUES = [
    (10.5, "altimeter_queue",       "depth 10"),
    ( 9.3, "filtered_data_queue",   "depth 10"),
    ( 8.2, "gyroscope_queue",       "depth 10"),
    ( 7.0, "gps_queue",             "depth 24"),
    ( 5.3, "telemetry_data_queue",  "depth 10"),
    ( 4.2, "flight_states_queue",   "depth  1"),
    ( 3.1, "log_to_mem_queue",      "depth 64"),
]
queue_y = {name: cy for cy, name, _ in QUEUES}
for cy, name, sub in QUEUES:
    box(CB, cy, QW, QH, [name, sub], QUEUE_FC, QUEUE_EC, fs=8)

# ─── Column C: Processing ─────────────────────────────────────────────────────
box(CC, 10.5, TW, TH, ["kalman_task", "Core 0 · Pri 3  (1D altitude filter)"], PROC_FC, PROC_EC)
box(CC,  8.0, TW, TH + 0.2, ["state_machine_task", "Core 0 · Pri 4  (flight FSM)"], PROC_FC, PROC_EC)

# ─── Column D: Output / Storage ───────────────────────────────────────────────
OUT = [
    (10.5, "mqtt_telemetry",   "Core 1 · Pri 2"),
    ( 8.8, "xbee_telemetry",   "Core 1 · Pri 2"),
    ( 7.1, "beacon_transmit",  "Core 1 · Pri 2"),
    ( 5.5, "sd_logger",        "Core 1 · Pri 1"),
    ( 3.8, "flash_logger",     "Core 1 · Pri 1  (CustomSerialFlash)"),
]
for cy, name, sub in OUT:
    is_store = "logger" in name
    box(CD, cy, TW, TH,
        [name, sub],
        STORE_FC if is_store else OUT_FC,
        STORE_EC if is_store else OUT_EC)

# ─── Arrows: Sensor tasks → queues ───────────────────────────────────────────
arr(CA + TW/2, 10.2, CB - QW/2, 10.5)           # altimeter → altimeter_queue
arr(CA + TW/2,  8.2, CB - QW/2,  8.2)           # gyroscope → gyroscope_queue
arr(CA + TW/2,  6.2, CB - QW/2,  7.0)           # gps       → gps_queue

# ─── Arrows: altimeter_queue → Kalman ────────────────────────────────────────
arr(CB + QW/2, 10.5, CC - TW/2, 10.5)

# ─── Kalman → filtered_data_queue ────────────────────────────────────────────
# Kalman writes filtered altitude back to queue
arr(CC - TW/2, 9.9, CB + QW/2, 9.3)

# ─── Queues → state_machine ──────────────────────────────────────────────────
for qname, label in [
    ("filtered_data_queue", "filtered alt"),
    ("gyroscope_queue",     "IMU data"),
    ("gps_queue",           "GPS fix"),
]:
    arr(CB + QW/2, queue_y[qname], CC - TW/2, 8.0, label)

# ─── state_machine → output queues ───────────────────────────────────────────
arr(CC + TW/2, 8.2, CB + QW/2, 5.3, "packet")          # → telemetry_data_queue
arr(CC + TW/2, 7.8, CB + QW/2, 4.2, "state ID")        # → flight_states_queue
arr(CC + TW/2, 7.6, CB + QW/2, 3.1, "log entry")       # → log_to_mem_queue

# ─── Output queues → telemetry tasks ─────────────────────────────────────────
for qname, dy in [
    ("telemetry_data_queue", 10.5),
    ("telemetry_data_queue",  8.8),
    ("telemetry_data_queue",  7.1),
]:
    arr(CB + QW/2, queue_y[qname], CD - TW/2, dy)

# GPS queue direct path to beacon transmitter
arr(CB + QW/2, queue_y["gps_queue"], CD - TW/2, 7.1, "GPS")

# ─── Log queue → loggers ──────────────────────────────────────────────────────
arr(CB + QW/2, queue_y["log_to_mem_queue"], CD - TW/2, 5.5)
arr(CB + QW/2, queue_y["log_to_mem_queue"], CD - TW/2, 3.8)

# ─── Column headers ───────────────────────────────────────────────────────────
for cx, txt, col in [
    (CA, "Sensor Tasks\n(Core 0)", TASK_EC),
    (CB, "FreeRTOS Queues",        QUEUE_EC),
    (CC, "Processing\n(Core 0)",   PROC_EC),
    (CD, "Output & Storage\n(Core 1)", OUT_EC),
]:
    ax.text(cx, 11.65, txt,
            ha="center", va="center",
            fontsize=9.5, fontweight="bold", color=col,
            linespacing=1.3,
            bbox=dict(boxstyle="round,pad=0.25",
                      facecolor="#F5F5F5", edgecolor=col, linewidth=1.2))

# ─── Title ────────────────────────────────────────────────────────────────────
ax.text(FIG_W / 2, 0.85, "N4 FreeRTOS Task & Queue Data Flow",
        ha="center", va="center",
        fontsize=13, fontweight="bold", color="#1A1A1A")

plt.tight_layout(pad=0.4)
for fmt in ("svg", "png"):
    plt.savefig(f"output/task_queue.{fmt}",
                format=fmt, bbox_inches="tight", dpi=150)
    print(f"  ✓  output/task_queue.{fmt}")
plt.close()
