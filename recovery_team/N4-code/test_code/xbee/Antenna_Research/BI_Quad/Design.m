% --- FIXED 915 MHz BI-QUAD SIMULATION ---
clear; clc; close all;

% 1. Setup Frequency & Dimensions
freq = 915e6;           % 915 MHz
lambda = 3e8 / freq;    % Wavelength (~328 mm)

% Dimensions (Optimized for 915 MHz)
L1 = lambda / 4;        % Arm Length (~82mm)
spacing = lambda / 8;   % Spacing (~41mm)

% Reflector Plate Size (350mm x 200mm)
Reflector_Width  = 0.200; 
Reflector_Height = 0.350; 

% 2. Create the Bi-Quad Element (The Exciter)
bq = biquad;
bq.ArmLength = L1;
% FIX 1: Reduce Wire Width to avoid center-point collision
% 1mm radius (2mm thick wire) is safer for simulation mesh
bq.Width = 0.001; 
% FIX 2: Ensure it is perfectly flat (Parallel to plate)
bq.Tilt = 0;   
bq.TiltAxis = [0 1 0];

% 3. Create the Reflector
r = reflector;
r.Exciter = bq;                   
r.Spacing = spacing;              
r.GroundPlaneLength = Reflector_Height; 
r.GroundPlaneWidth  = Reflector_Width;

% FIX 3: Manually Mesh first to catch errors early
% This forces MATLAB to calculate geometry before running the full physics
fprintf('Generating Mesh... \n');
mesh(r, 'MaxEdgeLength', lambda/10); 

% --- 4. VISUALIZATION & ANALYSIS ---

fprintf('Simulating Physics... (This takes about 30-60 seconds)\n');

% Plot 1: Geometry 
figure(1);
show(r);
title('Bi-Quad Geometry');
view(-45, 30);

% Plot 2: 3D Radiation Pattern
figure(2);
pattern(r, freq);
title('3D Radiation Pattern @ 915 MHz');
view(45, 20);

% Plot 3: SWR (Tuning Check)
figure(3);
freq_range = linspace(850e6, 950e6, 21); 
vswr(r, freq_range);
title('VSWR Tuning Check');
yline(2, 'r--', 'Limit');
grid on;

% --- 5. RESULTS ---
G = pattern(r, freq);
max_gain = max(G(:));
[min_swr, idx] = min(vswr(r, freq_range));
tuned_freq = freq_range(idx)/1e6;

fprintf('------------------------------------------------\n');
fprintf('BI-QUAD RESULTS:\n');
fprintf('Max Gain: %.2f dBi (Target > 10 dBi)\n', max_gain);
fprintf('Best SWR: %.2f at %.2f MHz\n', min_swr, tuned_freq);
fprintf('------------------------------------------------\n');