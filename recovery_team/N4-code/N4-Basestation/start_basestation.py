#!/usr/bin/env python3
"""
N4 Base Station Startup Script
Starts all required services for the base station and ensures ports are free.
"""

import subprocess
import sys
import os
import time
import shutil

PROC_GROUP = []

PORTS = {
    "tiles": 8080,
    "vite": 5173,
    "mqtt": 1883,
    "api": 3000,
}

def kill_on_port(port: int):
    """Kill any processes listening on the given TCP port (Windows)."""
    try:
        # Use netstat to find PIDs, then force kill each PID
        cmd = f"netstat -ano | findstr :{port}"
        res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if res.returncode == 0 and res.stdout:
            lines = [ln.strip() for ln in res.stdout.strip().splitlines() if ln.strip()]
            pids = set()
            for ln in lines:
                parts = ln.split()
                if len(parts) >= 5 and 'LISTENING' in ln:
                    pid = parts[-1]
                    if pid.isdigit():
                        pids.add(pid)
            for pid in pids:
                try:
                    print(f"🔪 Killing PID {pid} on port {port}…")
                    subprocess.run(f"taskkill /PID {pid} /F", shell=True, capture_output=True)
                except Exception:
                    pass
    except Exception:
        pass

def _spawn(name, cmd, cwd=None):
    """Spawn a child process and store it for cleanup."""
    print(f"▶ Starting {name}: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    p = subprocess.Popen(
        cmd,
        shell=True,
        cwd=cwd or os.getcwd(),
        stdout=None,
        stderr=None,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    PROC_GROUP.append((name, p))
    return p

def _cleanup():
    """Terminate all spawned processes."""
    print("\n🧹 Cleaning up child processes...")
    for name, p in PROC_GROUP:
        try:
            if p.poll() is None:
                print(f"⏹ Stopping {name}...")
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    print(f"⚠ Forcing kill of {name}...")
                    p.kill()
        except Exception:
            pass

def _ensure_npm_deps():
    """Install npm dependencies if missing (checks for node_modules/sql.js)."""
    try:
        need_install = False
        nm = os.path.join(os.getcwd(), 'node_modules')
        sqljs = os.path.join(nm, 'sql.js')
        if not os.path.isdir(nm) or not os.path.isdir(sqljs):
            need_install = True
        if need_install:
            print("📦 Installing npm dependencies (one-time)...")
            lockfile = os.path.join(os.getcwd(), 'package-lock.json')
            if os.path.exists(lockfile):
                    subprocess.run(["cmd", "/c", "npm", "install"], check=False)
            else:
                subprocess.run(["cmd", "/c", "npm", "install", "--no-fund", "--no-audit"], check=False)
        else:
            print("📦 npm dependencies present")
    except Exception:
        # Don't fail startup if install check fails; we'll let runtime errors surface
        pass

def start_basestation():
    """Start the base station with all services"""
    print("🚀 Starting N4 Base Station...")
    print()
    print("This will start:")
    print("- Python server with auto USB reconnection")
    print("- npm dev server (React app)")
    print("- Tileserver for maps")
    print("- Mosquitto MQTT broker")
    print()
    print("Press Ctrl+C to stop all services")
    print()
    
    try:
        # Change to the script directory
        os.chdir(os.path.dirname(os.path.abspath(__file__)))

        # Ensure ports are free
        print("🔧 Ensuring required ports are free…")
        for label, prt in PORTS.items():
            kill_on_port(prt)
        time.sleep(1)

        mbtiles = "osm-2020-02-10-v3.11_africa_kenya.mbtiles"

        # Ensure npm deps before starting JS processes
        _ensure_npm_deps()

        # 1) Mosquitto (matches guide: mosquitto -c mosquitto.conf)
        _spawn("mosquitto", ["cmd", "/c", "mosquitto", "-c", "mosquitto.conf"]) 

        # 2) TileServer-GL (CLI via tileserver-gl or npx tileserver-gl)
        mb_abs = os.path.abspath(mbtiles)
        if not os.path.exists(mb_abs):
            print(f"⚠ MBTiles not found at {mb_abs}. Tiles may not load.")
        if shutil.which("tileserver-gl"):
            _spawn("tileserver-gl", ["cmd", "/c", "tileserver-gl", "--file", mb_abs])
        else:
            _spawn("tileserver-gl(npx)", ["cmd", "/c", "npx", "--yes", "tileserver-gl", "--file", mb_abs])

        # 3) Frontend Vite (no concurrently)
        _spawn("vite", ["cmd", "/c", "npm", "run", "dev:client"]) 

        # 4) Node API server
        _spawn("node api", ["cmd", "/c", "node", "server.js"]) 

        # 5) Backend Python server (runs headless by design)
        print("▶ Starting server.py …")
        # Pass N4_SIM=1 to server.py
        env = os.environ.copy()
        env["N4_SIM"] = "0"
        srv = subprocess.Popen(
            ["cmd", "/c", sys.executable, "server.py"],
            shell=True,
            cwd=os.getcwd(),
            env=env,
            stdout=None,
            stderr=None,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
        PROC_GROUP.append(("server.py", srv))
        srv.wait()

    except KeyboardInterrupt:
        print("\n🛑 Shutting down base station...")
    except Exception as e:
        print(f"❌ Error starting base station: {e}")
    finally:
        _cleanup()
        print("👋 Base station stopped.")

if __name__ == "__main__":
    start_basestation()
