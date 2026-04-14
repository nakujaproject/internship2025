%% ============================================================
%  FULL WORKING MICROSTRIP PATCH ANTENNA (Older MATLAB Version)
%  Uses FeedOffset instead of FeedLocation
%% ============================================================

clear; close all; clc;

%% -----------------------------
% DESIGN PARAMETERS
%% -----------------------------
f0  = 2.45e9;
c   = physconst('LightSpeed');
lambda = c/f0;

% Substrate (FR4)
er   = 4.4;
h    = 1.6e-3;
tand = 0.02;

substrate = dielectric('FR4');
substrate.EpsilonR    = er;
substrate.Thickness   = h;
substrate.LossTangent = tand;

%% -----------------------------
% CLOSED-FORM DIMENSIONS
%% -----------------------------
W = c/(2*f0)*sqrt(2/(er+1));

ereff = (er + 1)/2 + (er - 1)/2*(1/sqrt(1 + 12*h/W));

deltaL = h * 0.412*((ereff + 0.3)*(W/h + 0.264))/((ereff - 0.258)*(W/h + 0.8));

Leff = c/(2*f0*sqrt(ereff));

L = Leff - 2*deltaL;

gndL = L + lambda/2;
gndW = W + lambda/2;

%% -----------------------------
% FEED POSITION FOR OLD MATLAB
%% -----------------------------
% Feed placed at edge center on the –Y side
feed = [0  -L/2];  

%% -----------------------------
% ANTENNA OBJECT
%% -----------------------------
patch = patchMicrostrip( ...
    'Length', L, ...
    'Width', W, ...
    'GroundPlaneLength', gndL, ...
    'GroundPlaneWidth', gndW, ...
    'Substrate', substrate, ...
    'Conductor', metal('Copper'), ...
    'FeedOffset', feed );     % <-- Correct for older Antenna Toolbox

figure;
show(patch);
title('Microstrip Patch Antenna (FeedOffset version)');

%% -----------------------------
% FREQUENCY SWEEP
%% -----------------------------
freq = linspace(2e9, 3e9, 301);

%% -----------------------------
% INPUT IMPEDANCE
%% -----------------------------
zin = impedance(patch, freq);

figure;
plot(freq/1e9, real(zin),'LineWidth',1.5); hold on;
plot(freq/1e9, imag(zin),'LineWidth',1.5);
xlabel('Frequency (GHz)');
ylabel('Impedance (Ohms)');
legend('Real(Z)','Imag(Z)');
grid on;
title('Input Impedance');

%% -----------------------------
% RETURN LOSS (S11)
%% -----------------------------
S = sparameters(patch, freq);

figure;
rfplot(S);
title('S11 Return Loss');

%% -----------------------------
% PATTERN AND CURRENTS
%% -----------------------------
figure;
pattern(patch, f0);

figure;
current(patch, f0);

figure;
mesh(patch, 'MaxEdgeLength', lambda/40);

disp('Simulation complete — FeedOffset version is working.');
