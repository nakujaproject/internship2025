"""
N4 Flight Software — Generate All Diagrams
Run from the diagrams/ directory:
    python generate_all.py
"""
import importlib.util
import os
import sys
import time

SCRIPTS = [
    "architecture_diagram.py",
    "state_machine_diagram.py",
    "beacon_comms_diagram.py",
    "pyro_sequence_diagram.py",
]


def run(script_name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), script_name)
    spec = importlib.util.spec_from_file_location("_mod", path)
    mod  = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    print("=" * 55)
    print(" N4 Diagram Generator")
    print("=" * 55)
    errors = []
    for s in SCRIPTS:
        t0 = time.time()
        try:
            run(s)
            print(f"  OK  {s:<38}  ({time.time()-t0:.1f}s)")
        except Exception as e:
            errors.append((s, e))
            print(f"  ERR {s:<38}  {e}")

    print("=" * 55)
    if errors:
        print(f"  {len(errors)} script(s) failed.")
        sys.exit(1)
    else:
        out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")
        pngs = [f for f in os.listdir(out_dir) if f.endswith(".png")]
        print(f"  All done — {len(pngs)} PNG(s) in diagrams/output/")
        for p in sorted(pngs):
            size = os.path.getsize(os.path.join(out_dir, p))
            print(f"    - {p}  ({size // 1024} KB)")
