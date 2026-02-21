# N4 Diagrams

Python scripts that generate all architecture and flow diagrams for the N4 flight software documentation.

## Quick Start

```powershell
cd n4-flight-software/diagrams
pip install -r requirements.txt
python generate_all.py
```

Output SVG and PNG files land in `output/`. Reference them from markdown with:
```markdown
![State Machine](../diagrams/output/state_machine.png)
```

## Diagrams

| Script | Output | Description |
|--------|--------|-------------|
| `state_machine.py` | `output/state_machine.*` | 9-state flight FSM with transition conditions |
| `comm_architecture.py` | `output/comm_architecture.*` | Multi-mode communication block diagram |
| `task_queue.py` | `output/task_queue.*` | FreeRTOS task and queue data-flow |
| `pyro_timing.py` | `output/pyro_timing.*` | Pyro firing sequence timeline |

## Requirements

- Python ≥ 3.8
- `matplotlib` ≥ 3.5 (`pip install -r requirements.txt`)
- No system-level dependencies (no LaTeX, no Graphviz install needed)

## Notes

- All scripts write to `output/` (created automatically).
- Each script can be run individually: `python state_machine.py`
- The markdown files in `docs/` also contain **Mermaid** versions of the same diagrams  
  that render directly on GitHub — no build step required.
