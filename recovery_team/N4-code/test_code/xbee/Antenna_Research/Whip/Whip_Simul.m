% Define the frequency of interest (ISM Band Center)
freq = 915e6; % 915 MHz

% Create a Dipole Antenna Object
% A standard "Duck" antenna is electrically a half-wave dipole.
d = dipole;

% Design (Tune) the antenna for 915 MHz
% MATLAB automatically calculates the length for resonance
d = design(d, freq);

% Display the physical properties
disp('Calculated Antenna Dimensions (meters):');
disp(['Length: ', num2str(d.Length)]);
disp(['Width (Radius): ', num2str(d.Width)]);

% --- ANALYSIS ---

% 1. Show the Antenna Structure
figure;
show(d);
title('915 MHz Dipole ("Duck") Antenna Geometry');

% 2. Calculate and Plot Impedance
% We check impedance from 800 MHz to 1 GHz to see the bandwidth
figure;
impedance(d, linspace(800e6, 1e9, 50));
title('Impedance vs Frequency');

% 3. Calculate and Plot SWR (Standing Wave Ratio)
% We want this to be close to 1.0 at 915 MHz
figure;
vswr(d, linspace(800e6, 1e9, 50));
title('Voltage Standing Wave Ratio (VSWR)');

% 4. 3D Radiation Pattern (The "Donut")
% This visualizes the nulls we discussed in the report
figure;
pattern(d, freq);
title('3D Radiation Pattern at 915 MHz');