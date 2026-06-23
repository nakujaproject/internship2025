@echo off
:: Nakuja N4 Basestation - startup script for Windows (Command Prompt)
setlocal EnableDelayedExpansion
cd /d "%~dp0"

echo === Nakuja N4 Basestation ===
echo.

:: ── Prerequisite checks ──────────────────────────────────────────────────────
where docker >nul 2>&1
if %ERRORLEVEL% neq 0 (
  echo [ERROR] Docker is not installed.
  echo   Install from: https://docs.docker.com/desktop/install/windows-install/
  exit /b 1
)

docker info >nul 2>&1
if %ERRORLEVEL% neq 0 (
  echo [ERROR] Docker daemon is not running. Please start Docker Desktop.
  exit /b 1
)

:: ── Create .env if missing ────────────────────────────────────────────────────
if not exist .env (
  echo Creating default .env ...
  (
    echo # ── Network addresses seen by the BROWSER (not inside Docker) ──────────
    echo # If running on a remote server, change "localhost" to the server IP/hostname.
    echo VITE_MQTT_HOST=localhost
    echo VITE_WS_PORT=1783
    echo VITE_BACKEND_URL=http://localhost:5001
    echo VITE_VIDEO_URL=
    echo.
    echo # ── Host port mappings ────────────────────────────────────────────────
    echo FRONTEND_PORT=80
    echo BACKEND_PORT=5001
    echo MQTT_TCP_PORT=1883
    echo MQTT_WS_PORT=1783
    echo.
    echo # ── Serial port (hardware only) ───────────────────────────────────────
    echo # Windows example: SERIAL_PORT=COM3
    echo SERIAL_PORT=
  ) > .env
  echo   .env created.
  echo.
)

:: ── Build and start ───────────────────────────────────────────────────────────
echo Building and starting containers ...
docker compose up --build -d
if %ERRORLEVEL% neq 0 (
  echo [ERROR] docker compose failed.
  exit /b 1
)

echo.
echo [OK] All services started:
echo   Dashboard : http://localhost:80
echo   Backend   : http://localhost:5001/debug
echo   MQTT TCP  : localhost:1883
echo   MQTT WS   : localhost:1783
echo.
echo Useful commands:
echo   docker compose logs -f         ^(stream logs^)
echo   docker compose down            ^(stop all services^)
echo   docker compose up --build -d   ^(rebuild and restart^)
endlocal
