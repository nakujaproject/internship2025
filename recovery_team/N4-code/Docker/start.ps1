# Nakuja N4 Basestation - startup script for Windows (PowerShell)
# Run with: .\start.ps1
# If execution policy blocks it: Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

Write-Host "=== Nakuja N4 Basestation ===" -ForegroundColor Cyan
Write-Host ""

# ── Prerequisite checks ───────────────────────────────────────────────────────
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] Docker is not installed." -ForegroundColor Red
    Write-Host "  Install from: https://docs.docker.com/desktop/install/windows-install/"
    exit 1
}

$dockerInfo = docker info 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Docker daemon is not running. Please start Docker Desktop." -ForegroundColor Red
    exit 1
}

# ── Create .env if missing ────────────────────────────────────────────────────
if (-not (Test-Path ".env")) {
    Write-Host "Creating default .env ..."
    @"
# ── Network addresses seen by the BROWSER (not inside Docker) ──────────────
# If running on a remote server, change "localhost" to the server's IP/hostname.
VITE_MQTT_HOST=localhost
VITE_WS_PORT=1783
VITE_BACKEND_URL=http://localhost:5001
VITE_VIDEO_URL=

# ── Host port mappings ──────────────────────────────────────────────────────
FRONTEND_PORT=80
BACKEND_PORT=5001
MQTT_TCP_PORT=1883
MQTT_WS_PORT=1783

# ── Serial port (hardware only) ─────────────────────────────────────────────
# Windows example: SERIAL_PORT=COM3
SERIAL_PORT=
"@ | Set-Content ".env" -Encoding UTF8
    Write-Host "  .env created. Edit it before rebuilding if deploying to a remote server."
    Write-Host ""
}

# ── Build and start ───────────────────────────────────────────────────────────
Write-Host "Building and starting containers ..."
docker compose up --build -d
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] docker compose failed." -ForegroundColor Red
    exit 1
}

# Read ports from .env for display
$envVars = @{}
Get-Content ".env" | Where-Object { $_ -match '^\s*[^#].*=' } | ForEach-Object {
    $parts = $_ -split '=', 2
    $envVars[$parts[0].Trim()] = $parts[1].Trim()
}
$frontendPort = if ($envVars['FRONTEND_PORT']) { $envVars['FRONTEND_PORT'] } else { "80" }
$backendPort  = if ($envVars['BACKEND_PORT'])  { $envVars['BACKEND_PORT'] }  else { "5001" }
$mqttTcp      = if ($envVars['MQTT_TCP_PORT']) { $envVars['MQTT_TCP_PORT'] } else { "1883" }
$mqttWs       = if ($envVars['MQTT_WS_PORT'])  { $envVars['MQTT_WS_PORT'] }  else { "1783" }

Write-Host ""
Write-Host "All services started:" -ForegroundColor Green
Write-Host "  Dashboard : http://localhost:$frontendPort"
Write-Host "  Backend   : http://localhost:$backendPort/debug"
Write-Host "  MQTT TCP  : localhost:$mqttTcp"
Write-Host "  MQTT WS   : localhost:$mqttWs"
Write-Host ""
Write-Host "Useful commands:"
Write-Host "  docker compose logs -f          # stream logs"
Write-Host "  docker compose down             # stop all services"
Write-Host "  docker compose up --build -d    # rebuild and restart"
