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
FRONTEND_PORT=8080
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

# ── Read .env for port values ─────────────────────────────────────────────────
$envVars = @{}
Get-Content ".env" | Where-Object { $_ -match '^\s*[^#].*=' } | ForEach-Object {
    $parts = $_ -split '=', 2
    $envVars[$parts[0].Trim()] = $parts[1].Trim()
}
$mqttTcpPort  = if ($envVars['MQTT_TCP_PORT']) { [int]$envVars['MQTT_TCP_PORT'] } else { 1883 }
$mqttWsPort   = if ($envVars['MQTT_WS_PORT'])  { [int]$envVars['MQTT_WS_PORT'] }  else { 1783 }
$frontendPort = if ($envVars['FRONTEND_PORT']) { $envVars['FRONTEND_PORT'] } else { "8080" }
$backendPort  = if ($envVars['BACKEND_PORT'])  { $envVars['BACKEND_PORT'] }  else { "5001" }

# ── Stop existing containers ──────────────────────────────────────────────────
docker compose down --remove-orphans 2>$null

# ── Port conflict check ───────────────────────────────────────────────────────
foreach ($port in @($mqttTcpPort, $mqttWsPort)) {
    $listener = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
    if ($listener) {
        $proc = Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
        if ($proc -and $proc.Name -like '*mosquitto*') {
            Write-Host "Stopping native mosquitto process (PID $($proc.Id)) on port $port ..."
            Stop-Process -Id $proc.Id -Force
        } else {
            $procName = if ($proc) { $proc.Name } else { "unknown" }
            $procId   = if ($proc) { $proc.Id }   else { $listener.OwningProcess }
            Write-Host "[ERROR] Port $port is already in use by '$procName' (PID $procId)." -ForegroundColor Red
            Write-Host "  Free the port before running this script."
            exit 1
        }
    }
}

# ── Build and start ───────────────────────────────────────────────────────────
Write-Host "Building and starting containers ..."
docker compose up --build -d
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] docker compose failed." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "All services started:" -ForegroundColor Green
Write-Host "  Dashboard : http://localhost:$frontendPort"
Write-Host "  Backend   : http://localhost:$backendPort/debug"
Write-Host "  MQTT TCP  : localhost:$mqttTcpPort"
Write-Host "  MQTT WS   : localhost:$mqttWsPort"
Write-Host ""
Write-Host "Useful commands:"
Write-Host "  docker compose logs -f          # stream logs"
Write-Host "  docker compose down             # stop all services"
Write-Host "  docker compose up --build -d    # rebuild and restart"
