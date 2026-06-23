#!/usr/bin/env bash
# Nakuja N4 Basestation - startup script for macOS / Linux
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GREEN='\033[0;32m'; CYAN='\033[0;36m'; RED='\033[0;31m'; NC='\033[0m'

echo -e "${CYAN}=== Nakuja N4 Basestation ===${NC}"
echo ""

# ── Prerequisite checks ───────────────────────────────────────────────────────
if ! command -v docker &>/dev/null; then
  echo -e "${RED}Error: Docker is not installed.${NC}"
  echo "  macOS:  https://docs.docker.com/desktop/install/mac-install/"
  echo "  Linux:  https://docs.docker.com/engine/install/"
  exit 1
fi

if ! docker info &>/dev/null 2>&1; then
  echo -e "${RED}Error: Docker daemon is not running. Please start Docker Desktop.${NC}"
  exit 1
fi

# ── Create .env if missing ────────────────────────────────────────────────────
if [ ! -f .env ]; then
  echo "Creating default .env ..."
  cat > .env <<'EOF'
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
# Linux example: SERIAL_PORT=/dev/ttyUSB0
# macOS example: SERIAL_PORT=/dev/cu.usbserial-XXXX
SERIAL_PORT=
EOF
  echo "  .env created. Edit it before rebuilding if deploying to a remote server."
  echo ""
fi

# ── Build and start ───────────────────────────────────────────────────────────
echo "Building and starting containers ..."
docker compose up --build -d

# Load env for display
# shellcheck disable=SC1091
source .env 2>/dev/null || true

echo ""
echo -e "${GREEN}All services started:${NC}"
echo "  Dashboard : http://localhost:${FRONTEND_PORT:-80}"
echo "  Backend   : http://localhost:${BACKEND_PORT:-5001}/debug"
echo "  MQTT TCP  : localhost:${MQTT_TCP_PORT:-1883}"
echo "  MQTT WS   : localhost:${MQTT_WS_PORT:-1783}"
echo ""
echo "Useful commands:"
echo "  docker compose logs -f          # stream logs"
echo "  docker compose logs frontend    # frontend logs only"
echo "  docker compose down             # stop all services"
echo "  docker compose up --build -d    # rebuild and restart"
