#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ESP-01 Access Point settings
const char* AP_SSID = "N4_BaseStation_AP";
const char* AP_PASS = "n4flight2026";

// Default laptop server endpoint reachable via AP client network.
// Update from dashboard if your laptop gets a different IP.
String serverUrl = "http://192.168.4.2:3001/api/telemetry-csv";

ESP8266WebServer server(80);

String latestCsv = "";
String latestJson = "";
String latestLog = "";
String latestStatus = "";
String latestHeartbeat = "";
unsigned long lastLineMs = 0;
unsigned long totalLines = 0;
unsigned long totalCsvForwardOk = 0;
unsigned long totalCsvForwardFail = 0;

String serialLineBuffer;

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>N4 ESP-01 Dashboard</title>
  <style>
    :root { --bg:#0e1116; --panel:#171b22; --card:#202734; --text:#d9e1ef; --muted:#90a0b8; --ok:#35c47c; --warn:#ffb64d; --bad:#ff6a6a; --accent:#46a0ff; }
    body { margin:0; font-family:Consolas, Monaco, monospace; background:linear-gradient(135deg,#0b0f15,#111826 60%,#151f33); color:var(--text); }
    .wrap { max-width:1100px; margin:0 auto; padding:16px; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(320px,1fr)); gap:12px; }
    .card { background:var(--card); border:1px solid #2a3448; border-radius:12px; padding:12px; }
    h1 { margin:0 0 12px 0; font-size:20px; color:#eef5ff; }
    h2 { margin:0 0 8px 0; font-size:14px; color:#cfe0ff; }
    .kv { display:grid; grid-template-columns:140px 1fr; gap:6px; font-size:12px; }
    .k { color:var(--muted); }
    .v { color:var(--text); word-break:break-all; }
    .row { display:flex; gap:8px; flex-wrap:wrap; margin-top:8px; }
    button { background:#2c3a52; color:#e5efff; border:1px solid #42587d; border-radius:8px; padding:8px 10px; cursor:pointer; }
    button:hover { background:#35507a; }
    input { width:100%; padding:8px; border-radius:8px; border:1px solid #40506d; background:#121925; color:#e5efff; }
    pre { margin:0; white-space:pre-wrap; word-break:break-word; font-size:11px; color:#d5def0; }
    .ok { color:var(--ok); }
    .warn { color:var(--warn); }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>N4 ESP-01 AP Dashboard</h1>
    <div class="grid">
      <div class="card">
        <h2>Link Status</h2>
        <div class="kv">
          <div class="k">AP IP</div><div class="v" id="apIp">-</div>
          <div class="k">Last RX (ms ago)</div><div class="v" id="ageMs">-</div>
          <div class="k">RX Lines</div><div class="v" id="rxLines">-</div>
          <div class="k">CSV Forward OK</div><div class="v ok" id="fwdOk">-</div>
          <div class="k">CSV Forward Fail</div><div class="v warn" id="fwdFail">-</div>
        </div>
      </div>

      <div class="card">
        <h2>Command Controls</h2>
        <div class="row">
          <button onclick="sendCmd('ARM')">ARM</button>
          <button onclick="sendCmd('DISARM')">DISARM</button>
          <button onclick="sendCmd('RESET')">RESET</button>
          <button onclick="sendCmd('CMD_BEACON_MODE')">BEACON MODE</button>
          <button onclick="sendCmd('CMD_XBEE_MODE')">XBEE MODE</button>
          <button onclick="sendCmd('CMD_MQTT_MODE')">MQTT MODE</button>
        </div>
        <div class="row">
          <button onclick="sendCmd('MAIN_ON')">MAIN ON</button>
          <button onclick="sendCmd('MAIN_OFF')">MAIN OFF</button>
          <button onclick="sendCmd('DROGUE_ON')">DROGUE ON</button>
          <button onclick="sendCmd('DROGUE_OFF')">DROGUE OFF</button>
        </div>
        <div class="row">
          <input id="customCmd" placeholder="Custom command" />
          <button onclick="sendCustom()">SEND</button>
        </div>
        <div class="v" id="cmdResult"></div>
      </div>

      <div class="card">
        <h2>Server Forwarding</h2>
        <input id="serverUrl" placeholder="http://192.168.4.2:3001/api/telemetry-csv" />
        <div class="row"><button onclick="saveConfig()">Save URL</button></div>
        <div class="v" id="cfgResult"></div>
      </div>

      <div class="card">
        <h2>Latest Telemetry JSON</h2>
        <pre id="telemetryJson">-</pre>
      </div>

      <div class="card">
        <h2>Latest CSV</h2>
        <pre id="csvLine">-</pre>
      </div>

      <div class="card">
        <h2>Latest Log / Status</h2>
        <pre id="logLine">-</pre>
        <pre id="statusLine">-</pre>
      </div>
    </div>
  </div>

<script>
async function api(path, opts) {
  const r = await fetch(path, opts || {});
  return await r.json();
}

async function refresh() {
  try {
    const d = await api('/api/latest');
    document.getElementById('apIp').textContent = d.ap_ip;
    document.getElementById('ageMs').textContent = d.age_ms;
    document.getElementById('rxLines').textContent = d.rx_lines;
    document.getElementById('fwdOk').textContent = d.forward_ok;
    document.getElementById('fwdFail').textContent = d.forward_fail;
    document.getElementById('telemetryJson').textContent = d.latest_json || '-';
    document.getElementById('csvLine').textContent = d.latest_csv || '-';
    document.getElementById('logLine').textContent = d.latest_log || '-';
    document.getElementById('statusLine').textContent = d.latest_status || '-';
    document.getElementById('serverUrl').value = d.server_url || '';
  } catch (_) {}
}

async function sendCmd(cmd) {
  const d = await api('/api/cmd', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'cmd=' + encodeURIComponent(cmd)
  });
  document.getElementById('cmdResult').textContent = d.ok ? 'Sent: ' + cmd : ('Failed: ' + (d.error || 'unknown'));
}

function sendCustom() {
  const c = document.getElementById('customCmd').value.trim();
  if (c.length > 0) sendCmd(c);
}

async function saveConfig() {
  const u = document.getElementById('serverUrl').value.trim();
  const d = await api('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'server_url=' + encodeURIComponent(u)
  });
  document.getElementById('cfgResult').textContent = d.ok ? 'Saved' : ('Failed: ' + (d.error || 'unknown'));
}

setInterval(refresh, 500);
refresh();
</script>
</body>
</html>
)HTML";

void forwardCsvToServer(const String& csv) {
  if (serverUrl.length() < 8) return;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, serverUrl)) {
    totalCsvForwardFail++;
    return;
  }
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(csv);
  if (code > 0 && code < 400) {
    totalCsvForwardOk++;
  } else {
    totalCsvForwardFail++;
  }
  http.end();
}

void processLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  totalLines++;
  lastLineMs = millis();

  if (line.startsWith("CSV:")) {
    latestCsv = line.substring(4);
    forwardCsvToServer(latestCsv);
    return;
  }

  if (line.startsWith("LOG:")) {
    latestLog = line;
    return;
  }

  if (line.startsWith("STATUS:")) {
    latestStatus = line;
    return;
  }

  // Receiver appends |DEVICE_ID, keep pure JSON before display.
  int pipe = line.indexOf('|');
  String jsonCandidate = (pipe >= 0) ? line.substring(0, pipe) : line;

  if (jsonCandidate.startsWith("{") && jsonCandidate.endsWith("}")) {
    if (jsonCandidate.indexOf("\"type\":\"heartbeat\"") >= 0) {
      latestHeartbeat = jsonCandidate;
    }
    latestJson = jsonCandidate;
  }
}

void readUartLines() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      processLine(serialLineBuffer);
      serialLineBuffer = "";
    } else if (c != '\r') {
      serialLineBuffer += c;
      if (serialLineBuffer.length() > 700) {
        serialLineBuffer = "";
      }
    }
  }
}

void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleLatest() {
  DynamicJsonDocument doc(1536);
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["age_ms"] = (lastLineMs == 0) ? 0 : (millis() - lastLineMs);
  doc["rx_lines"] = totalLines;
  doc["forward_ok"] = totalCsvForwardOk;
  doc["forward_fail"] = totalCsvForwardFail;
  doc["latest_csv"] = latestCsv;
  doc["latest_json"] = latestJson;
  doc["latest_log"] = latestLog;
  doc["latest_status"] = latestStatus;
  doc["latest_heartbeat"] = latestHeartbeat;
  doc["server_url"] = serverUrl;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleCommand() {
  String cmd = server.arg("cmd");
  cmd.trim();

  DynamicJsonDocument doc(256);
  if (cmd.length() == 0) {
    doc["ok"] = false;
    doc["error"] = "Missing cmd";
  } else {
    Serial.println(cmd);
    doc["ok"] = true;
    doc["sent"] = cmd;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleConfig() {
  String url = server.arg("server_url");
  url.trim();

  DynamicJsonDocument doc(256);
  if (url.length() < 8 || (!url.startsWith("http://") && !url.startsWith("https://"))) {
    doc["ok"] = false;
    doc["error"] = "Invalid URL";
  } else {
    serverUrl = url;
    doc["ok"] = true;
    doc["server_url"] = serverUrl;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void setup() {
  // UART to ESP32 receiver module.
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/latest", HTTP_GET, handleLatest);
  server.on("/api/cmd", HTTP_POST, handleCommand);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.begin();
}

void loop() {
  readUartLines();
  server.handleClient();
}
