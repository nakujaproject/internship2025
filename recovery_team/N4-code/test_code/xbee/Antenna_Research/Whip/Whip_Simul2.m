% --- FIXED DUCK ANTENNA SIMULATION (Corrected Frequency Sweep) ---
clear; clc; close all;

% 1. Setup Frequency Sweep
% We define the list of frequencies FIRST so we can calculate Z for all of them.
freq_center = 900e6;
freq_sweep  = linspace(800e6, 1000e6, 51); % 51 points from 800 to 1000 MHz

% 2. Define Inductance
L_coil = 30e-9;     % 30 nH (Inductance estimate)

% 3. Calculate Impedance Vector
% Z must be calculated for EVERY frequency in the sweep list.
% Formula: Z = j * 2 * pi * f * L
w_sweep = 2 * pi * freq_sweep;
Z_coil_vector = 1i * w_sweep * L_coil; 

% 4. Define Short Dipole (105mm Total Length)
d = dipole;
d.Length = 0.105;   % 105mm (Physical length)
d.Width = 0.005;    % 5mm radius
d.Tilt = 90;        % Vertical orientation
d.TiltAxis = [0 1 0];

% 5. Create the Load using the Vector
% Now the load knows its impedance at every frequency step.
load = lumpedElement;
load.Frequency = freq_sweep;    % Assign the frequency list
load.Impedance = Z_coil_vector; % Assign the matching Z list
d.Load = load;

% --- ANALYSIS ---

% Plot 1: 3D Radiation Pattern (The Donut) at Center Frequency
figure(1);
pattern(d, freq_center);
title('3D Radiation Pattern (105mm Loaded Duck @ 900 MHz)');

% Plot 2: 2D Directional Cuts (Azimuth & Elevation)
figure(2);
pattern(d, freq_center); 
% Note: Calling pattern without extra arguments usually defaults to 3D.
% For 2D cuts, we use specific slices:
figure(2);
patternElevation(d, freq_center, 0); % 0 deg Azimuth cut
title('2D Elevation Cut (Side View - Shows Nulls at Top/Bottom)');

% Plot 3: SWR
% We use the SAME frequency vector we defined earlier to avoid mismatch errors.
figure(3);
vswr(d, freq_sweep);
title('VSWR (Voltage Standing Wave Ratio)');
yline(2, 'r--', 'Acceptable Limit (2.0)');
grid on;

% Check Minimum SWR
s = vswr(d, freq_sweep);
[min_swr, idx] = min(s);
fprintf('Minimum SWR is %.2f at %.2f MHz\n', min_swr, freq_sweep(idx)/1e6);

if min_swr > 2
    fprintf('WARNING: Antenna is not perfectly tuned. Try changing L_coil slightly.\n');
else
    fprintf('SUCCESS: Antenna is tuned well for the band.\n');
end