/*
  WifiRepeater — ESP8266 D1 Mini multi-network WiFi connector with web config.

  - Stores a list of WiFi networks (SSID + password) in EEPROM (survives reboot).
  - Every 10s, scans and connects to whichever stored network is in range
    (strongest first). Falls back automatically if a network disappears.
  - Hosts a config web page at http://192.168.4.1 (AP mode) and also on the
    station IP once connected.

  Web UI: add/remove networks, view status, reboot. All from your browser.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <esp8266_peri.h>   // RANDOM_REG32 hardware RNG

#define LED_PIN 2          // D4, active LOW
#define AP_SSID "WifiRepeater-Setup"
#define SCAN_INTERVAL_MS 10000
#define MAX_NETWORKS 8
#define SSID_LEN 33        // 32 chars + null
#define PASS_LEN 65        // 64 chars + null
#define MAGIC 0x57494653   // "WIFS" (v2: adds apPass field)

struct NetEntry {
  char ssid[SSID_LEN];
  char pass[PASS_LEN];
};

struct Config {
  uint32_t magic;
  uint8_t count;
  char apPass[PASS_LEN];   // WPA2 password for the setup AP AND the web UI (min 8 chars)
  NetEntry nets[MAX_NETWORKS];
};

const char DEFAULT_AP_PASS[] = "configure1";  // change it in the web UI after first login

static Config cfg;
static ESP8266WebServer server(80);
static unsigned long lastScan = 0;
static int8_t currentIndex = -1;  // index into cfg.nets we're connected to

// ---------- persistence ----------

void configLoad() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, cfg);
  if (cfg.magic != MAGIC || cfg.count > MAX_NETWORKS) {
    cfg.magic = MAGIC;
    cfg.count = 0;
    memset(cfg.nets, 0, sizeof(cfg.nets));
    strlcpy(cfg.apPass, DEFAULT_AP_PASS, PASS_LEN);
  }
  if (cfg.apPass[0] == '\0' || strlen(cfg.apPass) < 8)  // WPA2 needs >= 8
    strlcpy(cfg.apPass, DEFAULT_AP_PASS, PASS_LEN);
}

void configSave() {
  cfg.magic = MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// ---------- wifi logic ----------

bool connectToStored() {
  if (cfg.count == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true /* async */);
  unsigned long start = millis();
  while (WiFi.scanComplete() < 0 && millis() - start < 12000) {
    delay(50);
    yield();
  }
  int n = WiFi.scanComplete();
  if (n < 0) return false;

  // Rank stored networks by signal strength (strongest first)
  int8_t bestOrder[MAX_NETWORKS];
  int bestCount = 0;
  for (int s = 0; s < n; s++) {
    for (int c = 0; c < cfg.count; c++) {
      if (WiFi.SSID(s) == String(cfg.nets[c].ssid)) {
        // insertion sort by RSSI desc
        int pos = bestCount;
        while (pos > 0 && WiFi.RSSI(bestOrder[pos - 1]) < WiFi.RSSI(s)) pos--;
        if (pos < MAX_NETWORKS) {
          for (int m = min(bestCount, MAX_NETWORKS - 1); m > pos; m--)
            bestOrder[m] = bestOrder[m - 1];
          bestOrder[pos] = c;
          bestCount = min(bestCount + 1, MAX_NETWORKS);
        }
        break;
      }
    }
  }
  WiFi.scanDelete();

  for (int i = 0; i < bestCount; i++) {
    int c = bestOrder[i];
    Serial.printf("Connecting to %s (RSSI %d)...\n", cfg.nets[c].ssid, (int)WiFi.RSSI(i));
    WiFi.begin(cfg.nets[c].ssid, cfg.nets[c].pass);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
      digitalWrite(LED_PIN, LOW);
      delay(50);
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
      currentIndex = c;
      Serial.printf("Connected: %s  IP: %s\n", cfg.nets[c].ssid, WiFi.localIP().toString().c_str());
      return true;
    }
    WiFi.disconnect();
  }
  return false;
}

// ---------- web UI ----------

const char PAGE[] PROGMEM = R"rawlit(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WifiRepeater Setup</title>
<style>
 body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px;max-width:640px;margin:0 auto}
 h1{font-size:1.5rem} .card{background:#1e293b;border-radius:12px;padding:16px;margin-bottom:16px}
 .net{display:flex;justify-content:space-between;align-items:center;background:#334155;border-radius:8px;padding:10px 12px;margin-bottom:8px}
 input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #475569;background:#0f172a;color:#e2e8f0;margin-bottom:10px}
 button{padding:10px 16px;border-radius:8px;border:0;background:#3b82f6;color:#fff;font-weight:600;cursor:pointer}
 .del{background:#ef4444} .ok{color:#4ade80} .warn{color:#fbbf24}
 .status{font-size:.9rem;color:#94a3b8;margin-top:6px}
</style></head><body>
<h1>&#128246; WifiRepeater Setup</h1>
<div class="card"><b>Status</b>
<div class="status">%STATUS%</div></div>
<div class="card"><b>Saved networks (%COUNT%/%MAXN%)</b>%NETWORKS%
<div style="margin-top:12px"><b>Add network</b>
<form method="POST" action="/add">
<input name="ssid" placeholder="SSID" required maxlength="32">
<input name="pass" type="password" placeholder="Password" maxlength="64">
<button type="submit">Save &amp; Connect</button></form></div></div>
<div class="card" style="display:flex;justify-content:space-between;align-items:center">
<span>Forget all networks</span>
<form method="POST" action="/reset" onsubmit="return confirm('Erase all saved networks?')">
<button class="del">Factory reset</button></form></div>
<div class="card"><b>&#128274; Change admin password</b>
<div class="status">Also updates the WPA2 password of the setup AP (min 8 chars). Current: <code>****</code></div>
<form method="POST" action="/chpass">
<input name="cur" type="password" placeholder="Current password" required>
<input name="new" type="password" placeholder="New password (min 8 chars)" required minlength="8">
<button>Update password</button></form></div>
<div style="text-align:right"><form method="POST" action="/logout"><button class="del">Log out</button></form></div>
</body></html>
)rawlit";

void handleRoot() {
  if (!isAuthed()) {
    server.sendHeader("Location", "/login");
    server.send(303);
    return;
  }
  String nets = "";
  for (int i = 0; i < cfg.count; i++) {
    bool active = (i == currentIndex && WiFi.status() == WL_CONNECTED);
    nets += "<div class='net'><span>";
    nets += String(cfg.nets[i].ssid) + (active ? " <span class='ok'>● connected</span>" : "");
    nets += "</span><form method='POST' action='/remove' style='margin:0'>"
            "<input type='hidden' name='i' value='" + String(i) + "'>"
            "<button class='del' type='submit'>Remove</button></form></div>";
  }
  if (cfg.count == 0) nets = "<div class='status'>No networks saved yet.</div>";

  if (server.hasArg("passok"))
    nets += "<div class='ok'>Password updated — reconnect to the AP with the new password.</div>";
  if (server.hasArg("passerr"))
    nets += "<div class='warn'>Password change failed (wrong current password or too short).</div>";

  String status;
  if (WiFi.status() == WL_CONNECTED)
    status = "Connected to <b>" + String(cfg.nets[currentIndex].ssid) + "</b> — IP: " + WiFi.localIP().toString() +
             " | RSSI: " + String(WiFi.RSSI()) + " dBm";
  else if (cfg.count > 0)
    status = "<span class='warn'>Not connected — scanning every 10s (AP mode active)</span>";
  else
    status = "<span class='warn'>Setup mode — connect to AP <b>" AP_SSID "</b> and add a network</span>";

  String html = FPSTR(PAGE);
  html.replace("%STATUS%", status);
  html.replace("%COUNT%", String(cfg.count));
  html.replace("%MAXN%", String(MAX_NETWORKS));
  html.replace("%NETWORKS%", nets);
  server.send(200, "text/html", html);
}

void handleAdd() {
  if (!isAuthed()) {
    requireAuth();
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || cfg.count >= MAX_NETWORKS) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  // replace existing SSID if already saved
  for (int i = 0; i < cfg.count; i++) {
    if (ssid == String(cfg.nets[i].ssid)) {
      pass.toCharArray(cfg.nets[i].pass, PASS_LEN);
      configSave();
      currentIndex = -1;
      WiFi.disconnect();
      server.sendHeader("Location", "/");
      server.send(303);
      return;
    }
  }
  NetEntry &e = cfg.nets[cfg.count];
  ssid.toCharArray(e.ssid, SSID_LEN);
  pass.toCharArray(e.pass, PASS_LEN);
  cfg.count++;
  configSave();
  currentIndex = -1;
  WiFi.disconnect();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRemove() {
  if (!isAuthed()) {
    requireAuth();
    return;
  }
  int i = server.arg("i").toInt();
  if (i >= 0 && i < cfg.count) {
    if (i == currentIndex && WiFi.status() == WL_CONNECTED) WiFi.disconnect();
    for (int m = i; m < cfg.count - 1; m++) cfg.nets[m] = cfg.nets[m + 1];
    cfg.count--;
    memset(&cfg.nets[cfg.count], 0, sizeof(NetEntry));
    configSave();
    currentIndex = -1;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  if (!isAuthed()) {
    requireAuth();
    return;
  }
  cfg.count = 0;
  memset(cfg.nets, 0, sizeof(cfg.nets));
  configSave();
  currentIndex = -1;
  WiFi.disconnect();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleLogin();
void handleLoginPost();
void handleLogout();
void handleChpass();

void serverStart() {
  server.collectHeaders("Cookie");
  server.on("/", handleRoot);
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/remove", HTTP_POST, handleRemove);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/login", HTTP_GET, handleLogin);          // GET shows form (post-logout)
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", HTTP_POST, handleLogout);
  server.on("/chpass", HTTP_POST, handleChpass);
  server.begin();
}

// ---------- auth ----------

static const char *SESSION_COOKIE = "WIFR_SESSION";
static char sessionToken[17] = "";   // random per boot; invalidates on reboot

bool isAuthed() {
  if (sessionToken[0] == '\0') return false;
  return server.hasHeader("Cookie") &&
         server.header("Cookie").indexOf(String(SESSION_COOKIE) + "=" + sessionToken) >= 0;
}

void sendAuthHeaders() {
  server.sendHeader("Cache-Control", "no-store");
}

void requireAuth() {
  if (isAuthed()) return;
  server.sendHeader("Location", "/login");
  server.send(303);
}

void handleLogin() {
  if (isAuthed()) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  sendAuthHeaders();
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WifiRepeater Login</title></head>"
    "<body style='font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;"
    "display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0'>"
    "<form method='POST' action='/login' style='background:#1e293b;padding:32px;border-radius:12px;width:280px'>"
    "<h1 style='font-size:1.2rem;margin-top:0'>&#128274; WifiRepeater</h1>"
    "<input name='pass' type='password' placeholder='Password' required style='width:100%;box-sizing:border-box;padding:10px;margin-bottom:12px;border-radius:8px;border:1px solid #475569;background:#0f172a;color:#e2e8f0'>"
    "<button style='width:100%;padding:10px;border-radius:8px;border:0;background:#3b82f6;color:#fff;font-weight:600'>Sign in</button>"
    "</form></body></html>");
}

void handleLoginPost() {
  if (server.arg("pass") == String(cfg.apPass)) {
    // fresh random session token
    for (int i = 0; i < 16; i++)
      sessionToken[i] = "0123456789abcdef"[(uint8_t)(RANDOM_REG32 & 0xF)];
    sessionToken[16] = '\0';
    server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + "=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=604800");
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    delay(500);  // slow down brute force
    server.sendHeader("Location", "/login?e=1");
    server.send(303);
  }
}

void handleLogout() {
  sessionToken[0] = '\0';
  server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + "=; Path=/; Max-Age=0");
  server.sendHeader("Location", "/login");
  server.send(303);
}

void handleChpass() {
  if (!isAuthed()) {
    requireAuth();
    return;
  }
  String cur = server.arg("cur");
  String newp = server.arg("new");
  if (cur != String(cfg.apPass) || newp.length() < 8) {
    server.sendHeader("Location", "/?passerr=1");
    server.send(303);
    return;
  }
  strlcpy(cfg.apPass, newp.c_str(), PASS_LEN);
  configSave();
  // restart the AP so the new WPA2 password takes effect immediately
  WiFi.softAP(AP_SSID, cfg.apPass);
  server.sendHeader("Location", "/?passok=1");
  server.send(303);
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  configLoad();

  // Always start the AP so the config page is reachable at 192.168.4.1
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, cfg.apPass);   // WPA2-protected
  Serial.printf("AP up: %s at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  if (!connectToStored())
    Serial.println("No stored network reachable; staying in AP mode.");

  serverStart();
}

void loop() {
  server.handleClient();

  // Re-scan/reconnect periodically or whenever we drop off
  bool disconnected = (WiFi.status() != WL_CONNECTED);
  if (disconnected || millis() - lastScan > SCAN_INTERVAL_MS) {
    lastScan = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Link lost — re-scanning stored networks...");
      connectToStored();
    }
  }

  // Solid LED when connected, non-blocking blink in setup/retry mode
  static unsigned long blinkT = 0;
  static bool blinkOn = false;
  if (WiFi.status() == WL_CONNECTED) digitalWrite(LED_PIN, LOW);
  else if (millis() - blinkT > 400) {
    blinkT = millis();
    blinkOn = !blinkOn;
    digitalWrite(LED_PIN, blinkOn ? LOW : HIGH);
  }
}
