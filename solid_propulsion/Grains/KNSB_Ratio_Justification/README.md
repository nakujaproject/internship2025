# KNSB Ratio Justification

## Aims of this Software

* To simulate composite propellant compositions and obtain burn rate exponents and other important information.
* To ease the analysis of the information obtained from the simulation.
* To develop mathematical model that justifies a required grain geometry 

## What this does
1. Determines the stoichiometric equation for combustion of KNSB using [Mazza, 2022].
2. Sweeps $KNO_3$:Sorbitol mass ratio from $63.5:36.5$ to $70:30$ and computes equilibrium thermochemical performance ($I_{sp}$, $C*$, $T_chamber$, $\rho_propellant$) via PyProPEP simulation under frozen-equilibrium conditions (i.e., assuming combustion products do not react further after initial equilibrium). Outputs a populated CSV and publication-quality plots.
3. Compares the performance of KNSB to other propellants and computes the characteristic performance metrics based on the .

## Why this exists
ProPEP's Fortran-era interface crashes on modern systems and requires manual transcription into spreadsheets. This automates the full pipeline.

## Dependencies
- `pypropep` — Python interface to CProPEP
- `numpy`, `pandas`, `matplotlib`

Install: `pip install -r requirements.txt`

## Usage
```bash
jupyter notebook stoichiometric_justification.ipynb
jupyter notebook thermochemical_justification.ipynb
```

Run all cells in order. Results are saved to `./csv/Thermochemical_Justification.csv`
and figures to `./figures/`.

## Key result
Optimal ratio is **65% KNO₃ : 35% Sorbitol** — maximises Isp while keeping 
grain processability and thermal stability within acceptable bounds.

## References
[Nakka, 2025], [NASA Glenn Coefficients, 2002]