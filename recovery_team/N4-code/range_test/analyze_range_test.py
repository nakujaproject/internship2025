
import pandas as pd
import matplotlib.pyplot as plt

# ======== USER CONFIGURATION ========
TX_FILE = r"PATH/TO/transmitted.csv"
RX_FILE = r"PATH/TO/received.csv"   # first column must be rx_timestamp_ms
OUTPUT_DIR = "output"
# ====================================

import os
os.makedirs(OUTPUT_DIR, exist_ok=True)

tx = pd.read_csv(TX_FILE)
rx = pd.read_csv(RX_FILE)

if rx.columns[0] != "rx_timestamp_ms":
    rx = rx.rename(columns={rx.columns[0]:"rx_timestamp_ms"})

merged = pd.merge(tx, rx[["rx_timestamp_ms","record_number"]], on="record_number", how="left")
merged["received"] = merged["rx_timestamp_ms"].notna()

tx_count=len(tx)
rx_count=len(rx)
pdr=100*rx_count/tx_count
lost=tx_count-rx_count
missing=merged.loc[~merged.received,"record_number"].tolist()

dups=rx["record_number"][rx["record_number"].duplicated()].tolist()
ooo=(rx["record_number"].diff()<0).sum()

rec=rx.sort_values("rx_timestamp_ms").copy()
rec["latency_ms"]=rec["rx_timestamp_ms"]-rec["tx_timestamp_ms"]
rec["update_ms"]=rec["rx_timestamp_ms"].diff()
rec["update_rate_hz"]=1000/rec["update_ms"]
rec["throughput_Bps"]=rec["packet_size"]/(rec["update_ms"]/1000)

print("===== SUMMARY =====")
print(f"TX:{tx_count}")
print(f"RX:{rx_count}")
print(f"PDR:{pdr:.2f}%")
print(f"Lost:{lost}")
print(f"Duplicates:{len(dups)}")
print(f"Out of order:{ooo}")
print(f"Latency avg:{rec['latency_ms'].mean():.2f} ms")
print(f"Jitter:{rec['latency_ms'].std():.2f} ms")
print(f"Update rate:{rec['update_rate_hz'].mean():.2f} Hz")
print(f"Throughput:{rec['throughput_Bps'].mean():.2f} B/s")
if "battery_voltage" in rec:
    print(f"Battery mean:{rec['battery_voltage'].mean():.2f} V")
if "wifi_rssi" in rec:
    print(f"RSSI mean:{rec['wifi_rssi'].mean():.2f}")

with open(os.path.join(OUTPUT_DIR,"summary.txt"),"w") as f:
    f.write(f"PDR={pdr:.2f}%\nLost={lost}\nMissing={missing}\nDuplicates={dups}\n")

plots=[
("latency_ms","Latency (ms)","latency.png"),
("update_rate_hz","Update Rate (Hz)","update_rate.png"),
("throughput_Bps","Throughput (B/s)","throughput.png")
]
if "battery_voltage" in rec: plots.append(("battery_voltage","Battery Voltage (V)","battery.png"))
if "wifi_rssi" in rec: plots.append(("wifi_rssi","RSSI","rssi.png"))
if "rel_altitude" in rec: plots.append(("rel_altitude","Relative Altitude","altitude.png"))
for col,title,name in plots:
    plt.figure(figsize=(8,4))
    plt.plot(rec[col].values)
    plt.title(title)
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(os.path.join(OUTPUT_DIR,name))
    plt.close()

if {"latitude","longitude"}.issubset(rec.columns):
    plt.figure(figsize=(6,6))
    plt.plot(rec["longitude"],rec["latitude"])
    plt.xlabel("Longitude"); plt.ylabel("Latitude"); plt.grid(True); plt.tight_layout()
    plt.savefig(os.path.join(OUTPUT_DIR,"gps_path.png"))
    plt.close()

merged.to_csv(os.path.join(OUTPUT_DIR,"merged.csv"),index=False)
print("Done. Results in output/")
