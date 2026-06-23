# Nakuja N4 コード概要

Nakuja N4ロケットのソフトウェア一式です。地上局（ベースステーション）と飛行コンピュータ（フライトソフトウェア）の2つのコンポーネントで構成されています。

---

## ディレクトリ構成

```
N4-code/
├── N4-Basestation/        # 地上局ダッシュボード
│   ├── src/               # React フロントエンド
│   ├── server.py          # Python Flask バックエンド
│   ├── mosquitto.conf     # MQTT ブローカー設定
│   ├── docker-compose.yml # Docker Compose 設定
│   ├── Dockerfile.backend
│   ├── Dockerfile.frontend
│   ├── start.sh           # 起動スクリプト (macOS/Linux)
│   ├── start.bat          # 起動スクリプト (Windows CMD)
│   └── start.ps1          # 起動スクリプト (Windows PowerShell)
└── N4-Flight-Software/    # 飛行コンピュータソフトウェア
    └── n4-flight-software/ # PlatformIO プロジェクト
```

---

## N4-Basestation（地上局）

### 概要

ロケットのテレメトリデータをリアルタイムで可視化し、離陸前の設定操作を行うウェブダッシュボードです。

### システム構成

```
ブラウザ
  │
  ├─── HTTP ──► Nginx (ポート 80)       ← React ダッシュボード
  ├─── HTTP ──► Flask (ポート 5001)     ← シリアルデータ取得 API
  └─── WS ────► Mosquitto (ポート 1783) ← MQTT テレメトリ受信
                    │
                    └─── TCP (ポート 1883) ← 飛行コンピュータ接続
```

### 各コンポーネントの役割

| コンポーネント | 技術 | 役割 |
|---|---|---|
| **フロントエンド** | React + Vite + Tailwind CSS | テレメトリ表示、チャート、地図、アーミング操作 |
| **バックエンド** | Python Flask | ESP/Arduino シリアルポートからデータ取得 |
| **MQTT ブローカー** | Mosquitto | 飛行コンピュータとダッシュボード間のメッセージング |

### MQTT トピック

| トピック | 方向 | 内容 |
|---|---|---|
| `n4/flight-computer-1` | Subscribe | テレメトリデータ（JSON または CSV） |
| `n4/logs` | Subscribe | ログメッセージ |
| `n4/commands` | Publish | `ARM` / `DISARM` コマンド |

### ポート一覧

| サービス | ポート | プロトコル |
|---|---|---|
| ダッシュボード (Nginx) | 80 | HTTP |
| Flask バックエンド | 5001 | HTTP |
| MQTT TCP | 1883 | TCP |
| MQTT WebSocket | 1783 | WebSocket |

---

## Docker を使った起動方法（推奨）

Docker Compose を使うことで、フロントエンド・バックエンド・MQTT ブローカーを1コマンドで起動できます。

### 前提条件

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) がインストール・起動済みであること

### 手順

1. `N4-Basestation/` ディレクトリに移動します。

2. 起動スクリプトを実行します。

   **macOS / Linux:**
   ```bash
   ./start.sh
   ```

   **Windows (コマンドプロンプト):**
   ```bat
   start.bat
   ```

   **Windows (PowerShell):**
   ```powershell
   .\start.ps1
   ```

   初回実行時に `.env` ファイルが自動生成され、Dockerイメージのビルドが開始されます。

3. ビルド完了後、ブラウザで `http://localhost:80` を開きます。

### .env の設定

`.env` ファイルで環境をカスタマイズできます。

```env
# ブラウザからアクセスする MQTT ホスト（リモートサーバーの場合はIPアドレスに変更）
VITE_MQTT_HOST=localhost
VITE_WS_PORT=1783

# Flask バックエンドの URL（ブラウザからのアクセス先）
VITE_BACKEND_URL=http://localhost:5001

# ビデオストリームの URL（例: 192.168.1.10:8554）
VITE_VIDEO_URL=

# ホストへの公開ポート
FRONTEND_PORT=80
BACKEND_PORT=5001
MQTT_TCP_PORT=1883
MQTT_WS_PORT=1783

# シリアルポート（ハードウェア接続時のみ設定）
# Linux 例: SERIAL_PORT=/dev/ttyUSB0
# macOS 例: SERIAL_PORT=/dev/cu.usbserial-XXXX
SERIAL_PORT=
```

> **注意:** `VITE_*` の変数はビルド時にバイナリへ埋め込まれます。変更した場合は `docker compose up --build -d` で再ビルドが必要です。

### よく使うコマンド

```bash
# ログをリアルタイムで確認
docker compose logs -f

# 特定サービスのログのみ
docker compose logs -f frontend

# サービスの停止と削除
docker compose down

# 設定変更後の再ビルドと再起動
docker compose up --build -d
```

---

## ローカル（Docker なし）での起動方法

Docker を使わずに各サービスを個別に起動する場合です。

### 前提条件

- Node.js / npm
- Python 3.x
- Mosquitto

### 手順

**1. Mosquitto（MQTT ブローカー）の起動:**
```bash
mosquitto -c mosquitto.conf
```

**2. Python 仮想環境の作成と依存パッケージのインストール:**
```bash
python3 -m venv venv_local
source venv_local/bin/activate       # macOS/Linux
# venv_local\Scripts\activate        # Windows
pip install flask flask-cors pyserial
```

**3. Flask バックエンドの起動:**
```bash
python server.py
# → http://localhost:5001 で起動
```

**4. フロントエンドの起動:**
```bash
npm install
npm run dev
# → http://localhost:5173 で起動
```

---

## テレメトリデータの形式

### JSON 形式

```json
{
  "state": 0,
  "operation_mode": 0,
  "gps_data": {
    "latitude": -1.1,
    "longitude": 37.01,
    "gps_altitude": 0
  },
  "alt_data": {
    "pressure": 101325,
    "temperature": 25,
    "AGL": 0,
    "velocity": 0
  },
  "acc_data": {
    "ax": 0,
    "ay": 0,
    "az": 9.8
  },
  "chute_state": {
    "pyro1_state": 0,
    "pyro2_state": 0
  },
  "battery_voltage": 12.0
}
```

### フライトステート

| 値 | 状態 |
|---|---|
| 0 | Pre-Flight（飛行前） |
| 1 | Powered Flight（上昇中） |
| 2 | Apogee（頂点） |
| 3 | Drogue Deployed（ドローグパラシュート展開） |
| 4 | Main Deployed（メインパラシュート展開） |
| 5 | Rocket Descent（降下中） |
| 6 | Post Flight（着陸後） |

---

## N4-Flight-Software（飛行コンピュータ）

### 概要

ESP32 ベースのロケット搭載コンピュータのファームウェアです。PlatformIO でビルドします。

### 主な機能

- 加速度・速度の算出とフィルタリング
- 気圧センサーによる高度（AGL）計算
- フライトステートの自動遷移
- フラッシュメモリへのデータロギング
- GPS による位置情報の取得
- MQTT を介した地上局へのテレメトリ送信
- 地上局からの ARM/DISARM コマンド受信

### ビルド・書き込み

```bash
cd N4-Flight-Software/n4-flight-software
# PlatformIO CLI または VS Code PlatformIO 拡張機能でビルド・書き込み
pio run --target upload
```

詳細は [`N4-Flight-Software/README.md`](N4-Flight-Software/README.md) を参照してください。

---

## トラブルシューティング

| 症状 | 原因・対処 |
|---|---|
| ダッシュボードが開かない | Docker Desktop が起動しているか確認。ポート 80 が他のプロセスに使われている場合は `.env` の `FRONTEND_PORT` を変更して再ビルド。 |
| MQTT に接続できない | `.env` の `VITE_MQTT_HOST` が正しいか確認。リモートサーバーの場合はサーバーの IP アドレスを設定。 |
| シリアルデータが取得できない | `SERIAL_PORT` にデバイスのパスが設定されているか確認。Linux では `docker-compose.yml` の `devices` セクションのコメントを外す。 |
| macOS でポート 5000 が使えない | macOS の AirPlay がポート 5000 を占有している。このプロジェクトではポート 5001 を使用しています。 |
| フロントエンドのビルドが失敗する | `npm install` を実行して依存パッケージを再インストール後、`docker compose up --build -d` で再ビルド。 |
