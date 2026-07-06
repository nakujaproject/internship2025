# N4 Base Station — Docker Setup

## Architecture

Two Docker images run as separate containers, connected via a shared Docker network:

```
┌─────────────────────┐      ┌──────────────────────────┐
│   n4-frontend       │      │   n4-backend              │
│   (port 5173)       │      │   (port 3000, 1883, 1783) │
│                     │      │                           │
│   serve (static)    │ ──── │   Node API (Express)      │
│   React dist/       │ HTTP │   /api/telemetry          │
│                     │      │   /api/logs               │
│                     │      │   /tiles/* (MBTiles)      │
│                     │      │                           │
│                     │ ──── │   Mosquitto MQTT          │
│                     │  WS  │   1883 TCP / 1783 WS      │
│                     │      │                           │
│                     │      │   Python telemetry daemon │
│                     │      │   (USB monitor, CSV log)  │
└─────────────────────┘      └──────────────────────────┘
```

---

## Image Names

| Image | Tag |
|-------|-----|
| Backend | `abelnyaosi/n4-v4-backend` |
| Frontend | `abelnyaosi/n4-v4-frontend` |

## Quick Start

```bash
# Start both containers (builds if images don't exist)
docker compose up -d

# View logs
docker logs n4-backend
docker logs n4-frontend

# Stop everything
docker compose down
```

Open **http://localhost:5173** in your browser.

## Sharing Images on Docker Hub

Images are tagged as `abelnyaosi/n4-v4-backend` and `abelnyaosi/n4-v4-frontend`, ready to push to Docker Hub.

### Maintainer — Push

```bash
# Log in (one-time)
docker login

# Build fresh images
docker compose build --no-cache

# Push both images to Docker Hub
docker compose push
```

### Users — Pull & Run

```bash
# Pull the images
docker pull abelnyaosi/n4-v4-backend:latest
docker pull abelnyaosi/n4-v4-frontend:latest
```

Then either use Docker Compose (recommended):

```bash
# Create a docker-compose.yml on your machine:
cat > docker-compose.yml << 'EOF'
name: n4-basestation

services:
  backend:
    image: abelnyaosi/n4-v4-backend:latest
    container_name: n4-backend
    ports:
      - "3000:3000"
      - "1883:1883"
      - "1783:1783"
    restart: unless-stopped

  frontend:
    image: abelnyaosi/n4-v4-frontend:latest
    container_name: n4-frontend
    ports:
      - "5173:5173"
    depends_on:
      - backend
    restart: unless-stopped
EOF

docker compose up -d
```

Or run containers individually:

```bash
# Create a shared network first
docker network create n4-network

# Start backend
docker run -d --network n4-network --name n4-backend \
  -p 3000:3000 -p 1883:1883 -p 1783:1783 \
  abelnyaosi/n4-v4-backend:latest

# Start frontend
docker run -d --network n4-network --name n4-frontend \
  -p 5173:5173 \
  abelnyaosi/n4-v4-frontend:latest
```

Open **http://localhost:5173** in your browser.

---

## Services

### Backend (`n4-backend`)
| Port | Protocol | Service |
|------|----------|---------|
| 3000 | HTTP | Node API (telemetry, logs, map tiles from MBTiles) |
| 1883 | TCP | MQTT broker (Mosquitto) |
| 1783 | WebSocket | MQTT for browser clients |

### Frontend (`n4-frontend`)
| Port | Protocol | Service |
|------|----------|---------|
| 5173 | HTTP | React dashboard (static files via `serve`) |

---

## Building

```bash
# Build both images (no cache)
docker compose build --no-cache

# Build a single image
docker compose build --no-cache backend
docker compose build --no-cache frontend
```

---

## Image Details & Sizes

### `n4-basestation-backend`
- **Base**: `node:20-alpine`
- **OS packages**: Python 3, pip, Mosquitto
- **npm**: production deps only (`npm install --omit=dev`), ~112 packages
- **Python**: `pyserial`, `paho-mqtt`
- **Includes**: 270 MB Kenya OSM MBTiles file for vector tile serving
- Files copied from build stage: `server.js`, `src/routes/`, `mosquitto.conf`, `start_basestation.py`, `research/scripts/server.py`

### `n4-basestation-frontend`
- **Base**: `node:20-alpine` (multi-stage build)
- **Build stage**: full `npm install` → `npm run build` (includes Vite, ESLint, Tailwind, PostCSS)
- **Runtime**: only `serve` (~85 packages) + the built `dist/`
- No dev dependencies leak into the final image

---

## Methodology & History

This section documents the journey from a single monolithic image to the current two-image setup. Understanding this history explains why certain decisions were made and may help future maintainers avoid repeating the same trial-and-error.

### Phase 1: Monolithic Image (1.87 GB)

**Goal**: A single `docker run` command that starts everything.

**Approach**: One `Dockerfile` that installed every dependency (Python, Mosquitto, all npm packages including dev deps, tileserver-gl) and copied the entire project including the 270 MB MBTiles file.

```dockerfile
FROM node:20-bookworm-slim
RUN apt-get install ... python3 mosquitto ...
COPY . .
RUN npm install          # includes devDependencies
RUN npm install -g tileserver-gl
CMD ["python3", "start_basestation.py"]
```

**Result**: 1.87 GB image.
- Root cause: `npm install` pulled in all dev dependencies (Vite, ESLint, Tailwind, PostCSS, `concurrently`, type definitions — ~350 packages) that were never used at runtime.
- The base image `node:20-bookworm-slim` (Debian-based) is already ~150 MB larger than Alpine.
- `tileserver-gl` pulls in a native `canvas` module with its own transitive dependencies.

### Phase 2: Alpine + Production Only (size drop)

**Switch to `node:20-alpine`**: Cut ~150 MB from the base image alone.

**Switch to `npm install --omit=dev`**: Dropped runtime npm packages from 467 to 111 — eliminated Vite, ESLint, Tailwind, PostCSS, `concurrently`, `@types/*`, `globals`, and dozens of their transitive deps.

**Result**: Image size dropped significantly (Dev deps removal alone eliminated hundreds of packages).

**Key insight**: Dev dependencies are needed only at build time (e.g., `npm run build`). By using a multi-stage build pattern, the final runtime image can exclude them entirely.

### Phase 3: Multi-Stage Backend

Introduced a build stage and a runtime stage in `Dockerfile.backend`:

```dockerfile
# Build stage (has all dev deps)
FROM node:20-alpine AS build
WORKDIR /app
COPY package*.json ./
RUN npm install          # includes devDependencies
COPY . .
# (npm run build not needed here — the API server runs from source)

# Runtime stage (production only)
FROM node:20-alpine
RUN apk add --no-cache python3 py3-pip mosquitto
WORKDIR /app
COPY --from=build /app/package*.json ./
RUN npm install --omit=dev
# Copy only what's needed at runtime
COPY --from=build /app/server.js ./
COPY --from=build /app/src/routes ./src/routes
COPY --from=build /app/mosquitto.conf ./
COPY --from=build /app/start_basestation.py ./
COPY --from=build /app/research/scripts/server.py ./research/scripts/
COPY --from=build /app/osm-2020-02-10-v3.11_africa_kenya.mbtiles ./
```

**Result**: The backend runtime layer contains only:
- 111 npm production packages (instead of 467)
- Python 3 + pip + Mosquitto (Alpine apk, minimal)
- `pyserial` + `paho-mqtt` (pip)
- The 270 MB MBTiles file (unavoidable — it is the map data)
- Source files for server.js and the Python daemon

Without the MBTiles file, the backend image would be ~200 MB. With it, ~500 MB.

### Phase 4: Splitting Frontend from Backend

The monolithic image ran five services in one container:
1. Mosquitto
2. Node API
3. Python telemetry daemon
4. Vite dev server (React frontend)
5. tileserver-gl (map tiles)

**Problem**: Too many concerns in one container. Hard to update, restart, or scale individually. Dev dependencies for the frontend (Vite) leaked into the final monolithic image even with `--omit=dev` if we wanted to serve the app.

**Solution**: Split into two images:

| Image | Services |
|-------|----------|
| `n4-basestation-backend` | Mosquitto + Node API + Python daemon |
| `n4-basestation-frontend` | Static file server for the built React app |

**Docker Compose** wires them on the same network. The frontend container can reach the backend via the hostname `backend`.

### Phase 5: The tileserver-gl Problem

tileserver-gl was originally included in the monolithic image to serve vector tiles from the Kenya OSM MBTiles file. When splitting off the frontend, tileserver-gl seemed like a natural fit for the frontend container.

**Problem**: `tileserver-gl` depends on `@maplibre/maplibre-gl-native` which requires:
- Native compilation of `canvas` (node-canvas) at install time
- Full OpenGL stack at runtime (`libGLX.so`, `libOpenGL.so`, Mesa DRI drivers)

On Alpine Linux (musl libc), `canvas` has no prebuilt binaries and must be compiled from source, requiring `python3`, `g++`, `make`, `cairo-dev`, `pango-dev`, etc. On Debian (glibc), prebuilt binaries exist but the runtime still needs `libgl1`, `libopengl0`, and Mesa — dozens of GPU-related libraries pulled in for a headless container.

**Decision**: Drop tileserver-gl. The Node API server (`server.js`) already serves tiles directly from the MBTiles file using `sql.js` — a pure JavaScript SQLite reader. No OpenGL, no native compilation, no GPU dependencies. The MBTiles file is referenced from `server.js`:

```javascript
const MBTILES_FILE = process.env.N4_MBTILES || path.join(__dirname, 'osm-2020-02-10-v3.11_africa_kenya.mbtiles');
```

Tile endpoints are served at `/tiles/:z/:x/:y.:ext` and `/metadata.json`.

**Why tileserver-gl was originally chosen**: It provides a convenient standalone tile server with style rendering. However, the integrated `server.js` approach using `sql.js` is simpler, avoids native dependencies, and produces smaller images.

### Phase 6: Current State

Two lightweight, purpose-specific Alpine-based images:

| Image | Base | Runtime Size (approx) |
|-------|------|----------------------|
| `n4-basestation-backend` | `node:20-alpine` | ~500 MB (270 MB is the MBTiles file) |
| `n4-basestation-frontend` | `node:20-alpine` | ~25 MB |

**Key optimisations applied**:
1. Alpine base image instead of Debian/Ubuntu
2. Multi-stage builds so dev dependencies stay in the build stage
3. `npm install --omit=dev` in runtime stages
4. Fine-grained `COPY` — only copy files needed at runtime, not the entire project
5. Split into two images so the frontend's toolchain (Vite) never touches the backend
6. Removed tileserver-gl in favour of the built-in Express-based tile serving

---

## Configuration

Environment variables (set in `docker-compose.yml` or `.env` file):

| Variable | Default | Description |
|----------|---------|-------------|
| `BACKEND_PORT` | `3000` | Host port for the Node API |
| `MQTT_TCP_PORT` | `1883` | Host port for MQTT TCP |
| `MQTT_WS_PORT` | `1783` | Host port for MQTT WebSocket |
| `FRONTEND_PORT` | `5173` | Host port for the React app |
| `SERIAL_PORT` | *(empty)* | COM port for USB serial (e.g. `COM12`) |

---

## Local Development (without Docker)

Run `start_basestation.py` directly on Windows/Linux:

```bash
python3 start_basestation.py
```

This starts Mosquitto, the Node API server, and the Python telemetry daemon on your host machine. For the frontend, run `npm run dev:client` separately.

---

## Files

| File | Purpose |
|------|---------|
| `Dockerfile.backend` | Backend image build (multi-stage, Alpine) |
| `Dockerfile.frontend` | Frontend image build (multi-stage, Alpine) |
| `docker-compose.yml` | Orchestrates both containers on a shared network |
| `start_basestation.py` | Entrypoint for backend; spawns Mosquitto + Node + Python telemetry. Cross-platform (Windows and Linux) |
| `mosquitto.conf` | MQTT broker config (1883 TCP, 1783 WS, anonymous) |
| `config.json` | Tile server config (referenced by tileserver-gl when running locally) |
| `server.js` | Express app — API routes + sql.js-based MBTiles tile serving |
| `research/scripts/server.py` | Python telemetry daemon (USB serial, MQTT, CSV logging) |
| `vite.config.js` | Vite dev server config (proxy for tiles, API host configuration) |
| `.dockerignore` | Prevents `node_modules`, `dist`, `telemetry_logs`, etc. from entering the build context |

---

## Lessons Learned

1. **Multi-stage builds are essential** for keeping final images small. Build dependencies (TypeScript compilers, linters, CSS preprocessors, test frameworks) should never appear in the runtime image.

2. **Alpine over Debian** saves ~150 MB before any application code. The `node:20-alpine` image is ~40 MB compared to `node:20-bookworm-slim` at ~190 MB.

3. **Check for prebuilt binaries** before installing npm packages that compile native modules. Packages like `canvas`, `sharp`, and `@maplibre/maplibre-gl-native` need native compilation on musl-based systems (Alpine) and may pull in heavy system dependencies on glibc systems (Debian).

4. **Prefer pure-JavaScript alternatives** when possible. `sql.js` serves vector tiles without any native compilation, while `tileserver-gl` requires canvas, OpenGL, and Mesa. The simpler solution is often more portable.

5. **`COPY . .` is convenient but lazy**. Explicitly copy only the files needed in each stage. This also helps with Docker layer caching — changing an unrelated file won't invalidate the layer that installs npm packages.

6. **Split services into separate containers** when they have different resource requirements, update cycles, or dependency profiles. The frontend's Vite/esbuild toolchain should not affect the backend's image size.
