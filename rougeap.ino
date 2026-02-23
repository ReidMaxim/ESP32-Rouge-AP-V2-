/*
 * rougeap.ino - Headless ESP32 Rogue AP / Captive Portal
 *
 * Boots into STA mode if saved credentials exist in SPIFFS,
 * otherwise starts an open AP with a captive portal.
 *
 * Features:
 *   - Public landing page: Customizable HTML, optional message wall
 *   - Message submission: Public wall (social hub) or admin-only log (stealth)
 *   - Admin panel (/admin): Settings, WiFi setup, log viewer, landing page editor
 *
 * Security note: Admin credentials are hardcoded ("admin" / "esp32admin").
 * This is intentional for a demo/portfolio project and is NOT suitable for
 * production use. Anyone sniffing the unencrypted AP traffic can read them.
 */

// ============================================================
// Includes
// ============================================================
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>

// ============================================================
// Configuration Defaults
// ============================================================
#define AP_SSID_DEFAULT "SetupWiFi"
#define STA_RETRIES 20
#define STA_RETRY_MS 500
#define MAX_MESSAGE_LEN 200
#define MAX_WALL_ENTRIES 50

// Admin credentials for the config panel (insecure -- demo only)
#define AUTH_USER "admin"
#define AUTH_PASS "esp32admin"

// ============================================================
// Global Objects
// ============================================================
AsyncWebServer server(80);
DNSServer dnsServer;

bool apMode = false;
String g_apSsid = AP_SSID_DEFAULT;
String g_siteName = "funnyportal";
bool g_publicWall = true;

// ============================================================
// Shared CSS used across all admin pages (hacker theme)
// ============================================================
const char ADMIN_CSS[] PROGMEM = R"rawliteral(
*{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:'Courier New',monospace;
  background:#0a0a0a;color:#00ff41;
  display:flex;justify-content:center;align-items:flex-start;
  min-height:100vh;padding:1.5rem;
}
.card{
  border:1px solid #00ff41;border-radius:8px;padding:1.5rem;
  width:100%;max-width:440px;background:rgba(0,255,65,0.03);
  position:relative;margin-top:1rem;
}
h1{font-size:1.1rem;margin-bottom:1.2rem;text-align:center;
   text-shadow:0 0 5px #00ff41;}
h2{font-size:.95rem;margin:1.2rem 0 .6rem;color:#7dffaa;
   border-bottom:1px solid #1a4d1a;padding-bottom:.3rem;}
label{display:block;font-size:.8rem;margin-bottom:.3rem;color:#0a8f2a}
input{
  width:100%;padding:.6rem .75rem;margin-bottom:1rem;
  border:1px solid #00ff41;border-radius:4px;
  background:#0a0a0a;color:#00ff41;font-family:inherit;font-size:.95rem;
}
input:focus{outline:none;box-shadow:0 0 8px rgba(0,255,65,0.4)}
.btn{
  display:inline-block;padding:.6rem 1rem;border:1px solid #00ff41;border-radius:4px;
  background:rgba(0,255,65,0.1);color:#00ff41;font-family:inherit;
  font-size:.9rem;cursor:pointer;font-weight:700;text-decoration:none;
  text-shadow:0 0 5px #00ff41;transition:background .2s;
  text-align:center;
}
.btn:hover{background:rgba(0,255,65,0.25)}
.btn-full{width:100%;display:block}
.btn-danger{border-color:#ff4141;color:#ff4141;text-shadow:0 0 5px #ff4141;
            background:rgba(255,65,65,0.08)}
.btn-danger:hover{background:rgba(255,65,65,0.2)}
.row{display:flex;gap:.6rem;margin-top:.8rem}
.row form,.row a{flex:1}
.row .btn{width:100%}
.info{background:rgba(0,255,65,0.05);border:1px solid #1a4d1a;
      border-radius:4px;padding:.6rem .8rem;margin-bottom:.4rem;font-size:.85rem}
.info span{color:#7dffaa}
.dim{color:#0a8f2a}
.err{color:#ff4141;text-align:center;margin-bottom:1rem;font-size:.85rem;
     text-shadow:0 0 5px #ff4141}
.logbox{
  background:#050505;border:1px solid #1a4d1a;border-radius:4px;
  padding:.8rem;font-size:.75rem;line-height:1.5;
  max-height:60vh;overflow-y:auto;white-space:pre-wrap;word-break:break-all;
}
)rawliteral";

// ============================================================
// Public Landing Page -- hacker-themed ASCII art splash
// Served to everyone who connects (no auth). This is what the
// phone's Captive Network Assistant renders automatically.
// ============================================================
const char LANDING_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>%SITE_NAME%</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{
      background:#0a0a0a;color:#00ff41;
      font-family:'Courier New',monospace;
      min-height:100vh;display:flex;flex-direction:column;
      align-items:center;justify-content:center;
      padding:1rem;overflow:hidden;
    }
    .scanline{
      position:fixed;top:0;left:0;width:100%;height:100%;
      background:repeating-linear-gradient(
        0deg,
        rgba(0,0,0,0) 0px,
        rgba(0,0,0,0) 2px,
        rgba(0,255,65,0.03) 2px,
        rgba(0,255,65,0.03) 4px
      );
      pointer-events:none;z-index:10;
    }
    .glow{
      text-shadow:0 0 5px #00ff41,0 0 10px #00ff41,0 0 20px #00ff41;
    }
    pre{
      font-size:clamp(0.45rem,1.8vw,0.85rem);
      line-height:1.2;text-align:center;
      white-space:pre;margin-bottom:1.5rem;
    }
    .terminal{
      border:1px solid #00ff41;border-radius:8px;
      padding:1.5rem;max-width:500px;width:100%;
      background:rgba(0,255,65,0.03);
      position:relative;
    }
    .terminal::before{
      content:'[ SYSTEM ACTIVE ]';
      position:absolute;top:-0.7rem;left:1rem;
      background:#0a0a0a;padding:0 0.5rem;
      font-size:0.7rem;color:#00ff41;
    }
    .line{margin:0.4rem 0;font-size:0.85rem;opacity:0;animation:typeIn 0.3s forwards}
    .line:nth-child(1){animation-delay:0.2s}
    .line:nth-child(2){animation-delay:0.6s}
    .line:nth-child(3){animation-delay:1.0s}
    .line:nth-child(4){animation-delay:1.4s}
    .line:nth-child(5){animation-delay:1.8s}
    .line:nth-child(6){animation-delay:2.2s}
    .line:nth-child(7){animation-delay:2.6s}
    .line:nth-child(8){animation-delay:3.0s}
    @keyframes typeIn{to{opacity:1}}
    .cursor{animation:blink 1s step-end infinite}
    @keyframes blink{50%{opacity:0}}
    .dim{color:#0a8f2a}
    .bright{color:#7dffaa}
    .warn{color:#ffae00;text-shadow:0 0 5px #ffae00}
  </style>
</head>
<body>
  <div class="scanline"></div>

  <pre class="glow">
 _  _   _   ___ _  __  _____ _  _ ___
| || | /_\ / __| |/ / |_   _| || | __|
| __ |/ _ \ (__| ' &lt;    | | | __ | _|
|_||_/_/ \_\___|_|\_\   |_| |_||_|___|
 ___ _      _   _  _ ___ _____
| _ \ |    /_\ | \| | __|_   _|
|  _/ |__ / _ \| .` | _|  | |
|_| |____/_/ \_\_|\_|___| |_|
  </pre>

  <div class="terminal">
    <div class="line"><span class="dim">$</span> connection established</div>
    <div class="line"><span class="dim">$</span> target: <span class="bright">192.168.4.1</span></div>
    <div class="line"><span class="dim">$</span> status: <span class="warn">INTERCEPTED</span></div>
    <div class="line"><span class="dim">$</span> scanning network interfaces...</div>
    <div class="line"><span class="dim">$</span> capturing handshake... <span class="bright">done</span></div>
    <div class="line"><span class="dim">$</span> decrypting payload...</div>
    <div class="line">&nbsp;</div>
    <div class="line"><span class="dim">$</span> <span class="warn">ACCESS GRANTED</span> <span class="cursor">_</span></div>
  </div>
  %MESSAGE_SECTION%
</body>
</html>
)rawliteral";

// ============================================================
// Message box + wall HTML fragment (injected before </body>)
// ============================================================
String buildMessageSection() {
  String html = "<div class=\"msg-section\" style=\"margin-top:1.5rem;max-width:500px;width:100%\">"
                "<form method=\"POST\" action=\"/submit-message\" style=\"display:flex;gap:0.5rem;margin-bottom:1rem\">"
                "<input type=\"text\" name=\"msg\" maxlength=\"" + String(MAX_MESSAGE_LEN) + "\" "
                "placeholder=\"Leave a message...\" style=\"flex:1;padding:0.5rem;border:1px solid #00ff41;"
                "border-radius:4px;background:#0a0a0a;color:#00ff41;font-family:inherit\">"
                "<button type=\"submit\" class=\"btn\" style=\"padding:0.5rem 1rem\">SEND</button>"
                "</form>";
  if (g_publicWall) {
    String wall = readWallFile();
    if (wall.length() > 0) {
      html += "<div class=\"wall\" style=\"border:1px solid #1a4d1a;border-radius:8px;padding:1rem;"
              "background:rgba(0,255,65,0.03);max-height:200px;overflow-y:auto;font-size:0.85rem\">";
      int start = 0;
      while (start < (int)wall.length()) {
        int nl = wall.indexOf('\n', start);
        if (nl < 0) nl = wall.length();
        String line = wall.substring(start, nl);
        start = nl + 1;
        if (line.length() == 0) continue;
        int pipe = line.indexOf('|');
        String ts = pipe > 0 ? line.substring(0, pipe) : "";
        String msg = pipe > 0 ? line.substring(pipe + 1) : line;
        html += "<div style=\"margin-bottom:0.5rem\"><span class=\"dim\" style=\"color:#0a8f2a\">[" + htmlEscape(ts) + "]</span> " + htmlEscape(msg) + "</div>";
      }
      html += "</div>";
    }
  }
  html += "</div>";
  return html;
}

// ============================================================
// Helper: Get landing page HTML and inject message section
// ============================================================
String getLandingPageHtml() {
  String html;
  if (SPIFFS.exists("/landing.html")) {
    File f = SPIFFS.open("/landing.html", "r");
    if (f && f.size() > 0 && f.size() < 30000) {
      html = f.readString();
      f.close();
    }
  }
  if (html.length() == 0) {
    html = FPSTR(LANDING_PAGE);
  }
  html.replace("%SITE_NAME%", htmlEscape(g_siteName));
  String msgSection = buildMessageSection();
  html.replace("%MESSAGE_SECTION%", msgSection);
  int bodyEnd = html.lastIndexOf("</body>");
  if (bodyEnd >= 0 && html.indexOf(msgSection) < 0) {
    html = html.substring(0, bodyEnd) + msgSection + html.substring(bodyEnd);
  }
  return html;
}

// ============================================================
// Admin Login Page -- form-based auth (no HTTP Basic Auth)
// ============================================================
const char ADMIN_LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Admin Login</title>
  <style>%CSS%
    .card::before{
      content:'[ AUTHENTICATION ]';
      position:absolute;top:-0.7rem;left:1rem;
      background:#0a0a0a;padding:0 0.5rem;font-size:0.7rem;
    }
  </style>
</head>
<body>
  <div class="card" style="max-width:360px;margin-top:20vh">
    <h1>// ADMIN LOGIN</h1>
    %ERROR%
    <form method="POST" action="/admin">
      <label>username</label>
      <input type="text" name="user" autocomplete="off" required>
      <label>password</label>
      <input type="password" name="pass" required>
      <button class="btn btn-full" type="submit">AUTHENTICATE</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

// ============================================================
// Helper: Check admin auth from request params
// ============================================================
bool checkAdminAuth(AsyncWebServerRequest *request) {
  String user = request->hasParam("user", true) ? request->getParam("user", true)->value() : "";
  String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
  return (user == AUTH_USER && pass == AUTH_PASS);
}

// ============================================================
// Helper: Read /config.txt -> ssid, password, and settings
// Returns true if SSID is non-empty after trimming.
// ============================================================
bool readConfig(String &ssid, String &password) {
  if (!SPIFFS.exists("/config.txt")) {
    Serial.println("[CONFIG] /config.txt not found.");
    return false;
  }

  File f = SPIFFS.open("/config.txt", "r");
  if (!f) {
    Serial.println("[CONFIG] Failed to open /config.txt.");
    return false;
  }

  ssid = f.readStringUntil('\n');
  password = f.readStringUntil('\n');
  ssid.trim();
  password.trim();

  // Parse key=value lines for extended settings
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq > 0) {
      String key = line.substring(0, eq);
      String val = line.substring(eq + 1);
      val.trim();
      if (key == "ap_ssid" && val.length() > 0) g_apSsid = val;
      else if (key == "site_name" && val.length() > 0) g_siteName = val;
      else if (key == "public_wall") g_publicWall = (val == "1" || val == "true");
    }
  }
  f.close();

  Serial.printf("[CONFIG] SSID: \"%s\"  AP: \"%s\"  Wall: %s\n",
                ssid.c_str(), g_apSsid.c_str(), g_publicWall ? "ON" : "OFF");
  return (ssid.length() > 0);
}

// ============================================================
// Helper: Load settings only (no WiFi creds) - call when in AP mode
// ============================================================
void loadSettings() {
  if (!SPIFFS.exists("/config.txt")) return;
  File f = SPIFFS.open("/config.txt", "r");
  if (!f) return;
  f.readStringUntil('\n');
  f.readStringUntil('\n');
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq > 0) {
      String key = line.substring(0, eq);
      String val = line.substring(eq + 1);
      val.trim();
      if (key == "ap_ssid" && val.length() > 0) g_apSsid = val;
      else if (key == "site_name" && val.length() > 0) g_siteName = val;
      else if (key == "public_wall") g_publicWall = (val == "1" || val == "true");
    }
  }
  f.close();
}

// ============================================================
// Helper: Read entire /log.txt into a String
// ============================================================
String readLogFile() {
  if (!SPIFFS.exists("/log.txt")) return "(no log entries yet)";

  File f = SPIFFS.open("/log.txt", "r");
  if (!f) return "(failed to read log)";

  String content = f.readString();
  f.close();

  if (content.length() == 0) return "(no log entries yet)";
  return content;
}

// ============================================================
// Helper: Append a line to /log.txt and echo to Serial
// ============================================================
void appendLog(const String &message) {
  Serial.println("[LOG] " + message);

  File f = SPIFFS.open("/log.txt", FILE_APPEND);
  if (f) {
    f.println(message);
    f.close();
  } else {
    Serial.println("[LOG] Failed to open /log.txt for writing.");
  }
}

// ============================================================
// Helper: Read /wall.txt for public wall display
// ============================================================
String readWallFile() {
  if (!SPIFFS.exists("/wall.txt")) return "";

  File f = SPIFFS.open("/wall.txt", "r");
  if (!f) return "";

  String content = f.readString();
  f.close();
  return content;
}

// ============================================================
// Helper: Append message to wall with FIFO limit
// ============================================================
void appendToWall(const String &timestamp, const String &msg) {
  String content = readWallFile();
  String newLine = timestamp + "|" + msg + "\n";

  int lineCount = 0;
  for (size_t i = 0; i < content.length(); i++) {
    if (content[i] == '\n') lineCount++;
  }
  if (content.length() > 0 && content[content.length() - 1] != '\n') lineCount++;

  content = newLine + content;
  lineCount++;

  if (lineCount > MAX_WALL_ENTRIES) {
    int lastNl = -1;
    int count = 0;
    for (size_t i = 0; i < content.length(); i++) {
      if (content[i] == '\n') {
        count++;
        if (count == MAX_WALL_ENTRIES) {
          lastNl = i;
          break;
        }
      }
    }
    if (lastNl >= 0) content = content.substring(0, lastNl + 1);
  }

  File f = SPIFFS.open("/wall.txt", "w");
  if (f) {
    f.print(content);
    f.close();
  }
}

// ============================================================
// Helper: HTML-escape a string
// ============================================================
String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

// ============================================================
// Helper: Get timestamp string for logs
// ============================================================
String getTimestamp() {
  unsigned long s = millis() / 1000;
  int m = s / 60, h = m / 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h % 24, m % 60, (int)(s % 60));
  return String(buf);
}

// ============================================================
// Helper: Serve the admin login page, optionally with an error
// ============================================================
void serveLoginPage(AsyncWebServerRequest *request, bool showError) {
  String html = FPSTR(ADMIN_LOGIN_PAGE);
  html.replace("%CSS%", FPSTR(ADMIN_CSS));
  if (showError) {
    html.replace("%ERROR%", "<p class=\"err\">// INVALID CREDENTIALS</p>");
  } else {
    html.replace("%ERROR%", "");
  }
  request->send(200, "text/html", html);
}

// ============================================================
// Helper: Hidden auth inputs for forms
// ============================================================
String authInputs() {
  return "<input type=\"hidden\" name=\"user\" value=\"" + String(AUTH_USER) + "\">"
         "<input type=\"hidden\" name=\"pass\" value=\"" + String(AUTH_PASS) + "\">";
}

// ============================================================
// Helper: Build and serve the admin dashboard
// ============================================================
void serveAdminDashboard(AsyncWebServerRequest *request) {
  String currentSSID, currentPass;
  bool hasCreds = readConfig(currentSSID, currentPass);
  loadSettings();

  String html = "<!DOCTYPE html><html lang=\"en\"><head>"
                "<meta charset=\"UTF-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
                "<title>Admin Dashboard</title>"
                "<style>"
                + String(FPSTR(ADMIN_CSS)) + ""
                ".card::before{content:'[ ADMIN DASHBOARD ]';"
                "position:absolute;top:-0.7rem;left:1rem;"
                "background:#0a0a0a;padding:0 0.5rem;font-size:0.7rem;}"
                "textarea{width:100%;min-height:120px;padding:.6rem;border:1px solid #00ff41;"
                "border-radius:4px;background:#0a0a0a;color:#00ff41;font-family:inherit;font-size:.85rem}"
                "input[type=checkbox]{width:auto;margin-right:.5rem}"
                ".chk{display:flex;align-items:center;margin-bottom:.5rem}"
                "</style></head><body><div class=\"card\">"
                "<h1>// ADMIN DASHBOARD</h1>";

  // --- Status ---
  html += "<h2>&gt; status</h2>";
  html += "<div class=\"info\">Mode: <span>AP</span></div>";
  html += "<div class=\"info\">AP SSID: <span>" + htmlEscape(g_apSsid) + "</span></div>";
  html += "<div class=\"info\">Clients: <span>" + String(WiFi.softAPgetStationNum()) + "</span></div>";
  html += "<div class=\"info\">Public wall: <span>" + String(g_publicWall ? "ON" : "OFF") + "</span></div>";

  // --- Settings form ---
  html += "<h2>&gt; settings</h2>"
          "<form method=\"POST\" action=\"/save-settings\">" + authInputs() +
          "<label>AP name (broadcast SSID)</label>"
          "<input type=\"text\" name=\"ap_ssid\" value=\"" + htmlEscape(g_apSsid) + "\" maxlength=\"32\">"
          "<label>Site name</label>"
          "<input type=\"text\" name=\"site_name\" value=\"" + htmlEscape(g_siteName) + "\">"
          "<div class=\"chk\"><input type=\"checkbox\" name=\"public_wall\" value=\"1\" id=\"pw\"" + String(g_publicWall ? " checked" : "") + ">"
          "<label for=\"pw\" style=\"margin:0\">Public wall ON (messages visible on landing page)</label></div>"
          "<button class=\"btn btn-full\" type=\"submit\">SAVE SETTINGS</button></form>";

  // --- WiFi config form ---
  html += "<h2>&gt; WiFi credentials</h2>"
          "<form method=\"POST\" action=\"/save\">" + authInputs() +
          "<label>target SSID</label>"
          "<input type=\"text\" name=\"ssid\" required autocomplete=\"off\"";
  if (hasCreds) html += " placeholder=\"" + htmlEscape(currentSSID) + "\"";
  html += ">"
          "<label>passphrase</label>"
          "<input type=\"password\" name=\"wifipass\">"
          "<button class=\"btn btn-full\" type=\"submit\">DEPLOY &amp; REBOOT</button></form>";

  // --- Landing page editor ---
  html += "<h2>&gt; landing page</h2>"
          "<div class=\"row\">"
          "<form method=\"POST\" action=\"/editor\">" + authInputs() +
          "<button class=\"btn\" type=\"submit\">EDIT HTML</button></form>"
          "</div>";

  // --- Log + wall ---
  html += "<h2>&gt; logs &amp; wall</h2><div class=\"row\">"
          "<form method=\"POST\" action=\"/log\">" + authInputs() +
          "<button class=\"btn\" type=\"submit\">VIEW LOG</button></form>"
          "<form method=\"POST\" action=\"/exportlog\">" + authInputs() +
          "<button class=\"btn\" type=\"submit\">DOWNLOAD LOG</button></form>"
          "<form method=\"POST\" action=\"/clearlog\">" + authInputs() +
          "<button class=\"btn btn-danger\" type=\"submit\">CLEAR LOG</button></form>";
  if (g_publicWall) {
    html += "<form method=\"POST\" action=\"/clearwall\">" + authInputs() +
            "<button class=\"btn btn-danger\" type=\"submit\">CLEAR WALL</button></form>";
  }
  html += "</div>";

  // --- Reboot ---
  html += "<h2>&gt; system</h2>"
          "<form method=\"POST\" action=\"/reboot\">" + authInputs() +
          "<button class=\"btn\" type=\"submit\">REBOOT</button></form>";

  html += "</div></body></html>";
  request->send(200, "text/html", html);
}

// ============================================================
// Helper: Build and serve the log viewer page
// ============================================================
void serveLogViewer(AsyncWebServerRequest *request) {
  String logContent = readLogFile();

  // Escape HTML entities in log content
  logContent.replace("&", "&amp;");
  logContent.replace("<", "&lt;");
  logContent.replace(">", "&gt;");

  String html = "<!DOCTYPE html><html lang=\"en\"><head>"
                "<meta charset=\"UTF-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
                "<title>Connection Log</title>"
                "<style>"
                + String(FPSTR(ADMIN_CSS)) + ""
                ".card::before{content:'[ CONNECTION LOG ]';"
                "position:absolute;top:-0.7rem;left:1rem;"
                "background:#0a0a0a;padding:0 0.5rem;font-size:0.7rem;}"
                "</style></head><body><div class=\"card\">"
                "<h1>// CONNECTION LOG</h1>"
                "<div class=\"logbox\">" + logContent + "</div>"
                "<div class=\"row\" style=\"margin-top:1rem\">"
                "<form method=\"POST\" action=\"/admin\">" + authInputs() +
                "<button class=\"btn\" type=\"submit\">&lt; BACK</button></form>"
                "<form method=\"POST\" action=\"/exportlog\">" + authInputs() +
                "<button class=\"btn\" type=\"submit\">DOWNLOAD</button></form>"
                "<form method=\"POST\" action=\"/clearlog\">" + authInputs() +
                "<button class=\"btn btn-danger\" type=\"submit\">CLEAR LOG</button></form>"
                "</div></div></body></html>";

  request->send(200, "text/html", html);
}

// ============================================================
// WiFi Event Handler (ESP32 Arduino Core)
// Logs MAC addresses of devices joining/leaving the soft-AP.
// ============================================================
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  uint8_t *mac;
  char macStr[18];

  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      mac = info.wifi_ap_staconnected.mac;
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      appendLog("CONNECTED: " + String(macStr));
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      mac = info.wifi_ap_stadisconnected.mac;
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      appendLog("DISCONNECTED: " + String(macStr));
      break;

    default:
      break;
  }
}

// ============================================================
// Start Captive Portal (AP + DNS + Web Server)
// ============================================================
void startCaptivePortal() {
  apMode = true;
  loadSettings();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(g_apSsid.c_str());
  delay(100);

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  Serial.printf("[AP] Started \"%s\"  IP: %s\n", g_apSsid.c_str(), WiFi.softAPIP().toString().c_str());

  WiFi.onEvent(onWiFiEvent);

  // DNS catch-all: resolve every domain to our AP IP
  dnsServer.start(53, "*", apIP);

  // ---- Web Server Routes ----

  const char *portalRedirect = "http://192.168.4.1/portal";

  server.on("/generate_204", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });
  server.on("/gen_204", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });
  server.on("/hotspot-detect.html", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });
  server.on("/connecttest.txt", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });
  server.on("/ncsi.txt", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });
  server.on("/redirect", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });
  server.on("/success.txt", HTTP_GET, [portalRedirect](AsyncWebServerRequest *request) {
    request->redirect(portalRedirect);
  });

  // /portal serves the public landing page
  server.on("/portal", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getLandingPageHtml());
  });

  // POST /submit-message - public message submission
  server.on("/submit-message", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("msg", true)) {
      request->redirect("/portal");
      return;
    }
    String msg = request->getParam("msg", true)->value();
    msg.trim();
    if (msg.length() == 0) {
      request->redirect("/portal");
      return;
    }
    if (msg.length() > MAX_MESSAGE_LEN) msg = msg.substring(0, MAX_MESSAGE_LEN);

    String ts = getTimestamp();
    String clientIP = request->client()->remoteIP().toString();

    if (g_publicWall) {
      appendToWall(ts, msg);
      appendLog("WALL: " + clientIP + " [" + ts + "] " + msg);
    } else {
      appendLog("MSG: " + clientIP + " [" + ts + "] " + msg);
    }
    request->redirect("/portal");
  });

  // GET /admin -> login form
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
    serveLoginPage(request, false);
  });

  // POST /admin -> validate credentials, show dashboard or error
  server.on("/admin", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (checkAdminAuth(request)) {
      serveAdminDashboard(request);
    } else {
      serveLoginPage(request, true);
    }
  });

  // POST /log -> view connection log
  server.on("/log", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    serveLogViewer(request);
  });

  // GET /exportlog -> download log file
  server.on("/exportlog", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    if (SPIFFS.exists("/log.txt")) {
      request->send(SPIFFS, "/log.txt", "text/plain", true);
    } else {
      request->send(200, "text/plain", "(no log entries yet)");
    }
  });

  // POST /clearlog
  server.on("/clearlog", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    SPIFFS.remove("/log.txt");
    Serial.println("[LOG] Log cleared by admin.");
    appendLog("LOG CLEARED BY ADMIN");
    serveAdminDashboard(request);
  });

  // POST /clearwall
  server.on("/clearwall", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    SPIFFS.remove("/wall.txt");
    Serial.println("[WALL] Cleared by admin.");
    appendLog("WALL CLEARED BY ADMIN");
    serveAdminDashboard(request);
  });

  // POST /save-settings
  server.on("/save-settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }

    String apSsid = request->hasParam("ap_ssid", true) ? request->getParam("ap_ssid", true)->value() : "";
    String siteName = request->hasParam("site_name", true) ? request->getParam("site_name", true)->value() : "";
    bool publicWall = request->hasParam("public_wall", true);

    apSsid.trim();
    siteName.trim();
    if (apSsid.length() == 0) apSsid = AP_SSID_DEFAULT;
    if (siteName.length() == 0) siteName = "funnyportal";

    g_apSsid = apSsid;
    g_siteName = siteName;
    g_publicWall = publicWall;

    String currentSSID, currentPass;
    bool hasCreds = readConfig(currentSSID, currentPass);

    File f = SPIFFS.open("/config.txt", "w");
    if (f) {
      f.println(hasCreds ? currentSSID : "");
      f.println(hasCreds ? currentPass : "");
      f.println("ap_ssid=" + g_apSsid);
      f.println("site_name=" + g_siteName);
      f.println("public_wall=" + String(g_publicWall ? "1" : "0"));
      f.close();
      Serial.printf("[SETTINGS] AP=%s site=%s wall=%s\n", g_apSsid.c_str(), g_siteName.c_str(), g_publicWall ? "ON" : "OFF");
    }

    request->send(200, "text/html",
                  "<html><body style='background:#0a0a0a;color:#00ff41;display:flex;justify-content:center;"
                  "align-items:center;height:100vh;font-family:Courier New,monospace;text-align:center'>"
                  "<div><pre style='font-size:1.2rem'>SETTINGS SAVED</pre>"
                  "<a href='/admin' style='color:#00ff41'>Back to dashboard</a> (re-login with admin)</div></body></html>");
  });

  // POST /editor -> show landing page HTML editor
  server.on("/editor", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }

    String content;
    if (SPIFFS.exists("/landing.html")) {
      File f = SPIFFS.open("/landing.html", "r");
      if (f) { content = f.readString(); f.close(); }
    }
    if (content.length() == 0) content = FPSTR(LANDING_PAGE);

    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
                  "<title>Edit Landing Page</title><style>" + String(FPSTR(ADMIN_CSS)) +
                  ".card::before{content:'[ HTML EDITOR ]';position:absolute;top:-0.7rem;left:1rem;"
                  "background:#0a0a0a;padding:0 0.5rem;font-size:0.7rem;}"
                  "textarea{min-height:300px;font-size:0.8rem}</style></head><body><div class=\"card\">"
                  "<h1>// LANDING PAGE EDITOR</h1>"
                  "<p class=\"dim\" style=\"margin-bottom:1rem\">Use %SITE_NAME% for site name, %MESSAGE_SECTION% for message box+wall (or omit to inject before &lt;/body&gt;)</p>"
                  "<form method=\"POST\" action=\"/save-landing\">" + authInputs() +
                  "<textarea name=\"html\" rows=\"20\">" + htmlEscape(content) + "</textarea>"
                  "<div class=\"row\" style=\"margin-top:1rem\">"
                  "<button class=\"btn\" type=\"submit\">SAVE</button>"
                  "</form>"
                  "<form method=\"POST\" action=\"/reset-landing\" style=\"display:inline\">" + authInputs() +
                  "<button class=\"btn btn-danger\" type=\"submit\">RESET TO DEFAULT</button></form>"
                  "</div></div></body></html>";

    request->send(200, "text/html", html);
  });

  // POST /save-landing
  server.on("/save-landing", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    if (!request->hasParam("html", true)) { request->send(400, "text/plain", "Missing html"); return; }

    String html = request->getParam("html", true)->value();
    if (html.length() > 25000) {
      request->send(400, "text/plain", "HTML too large (max 25KB)");
      return;
    }
    if (html.indexOf("<html") < 0 && html.indexOf("<HTML") < 0) {
      request->send(400, "text/plain", "Invalid HTML");
      return;
    }

    File f = SPIFFS.open("/landing.html", "w");
    if (f) {
      f.print(html);
      f.close();
      Serial.println("[EDITOR] Landing page saved.");
      appendLog("LANDING PAGE UPDATED BY ADMIN");
    }

    request->send(200, "text/html",
                  "<html><body style='background:#0a0a0a;color:#00ff41;display:flex;justify-content:center;"
                  "align-items:center;height:100vh;font-family:Courier New,monospace;text-align:center'>"
                  "<div><pre>SAVED</pre><a href='/admin' style='color:#00ff41'>Back</a> (re-login)</div></body></html>");
  });

  // POST /reset-landing
  server.on("/reset-landing", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    SPIFFS.remove("/landing.html");
    Serial.println("[EDITOR] Landing page reset to default.");
    appendLog("LANDING PAGE RESET BY ADMIN");
    request->send(200, "text/html",
                  "<html><body style='background:#0a0a0a;color:#00ff41;display:flex;justify-content:center;"
                  "align-items:center;height:100vh;font-family:Courier New,monospace;text-align:center'>"
                  "<div><pre>RESET TO DEFAULT</pre><a href='/admin' style='color:#00ff41'>Back</a></div></body></html>");
  });

  // POST /save -> WiFi credentials, restart
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }

    String newSSID = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
    String newWifiPass = request->hasParam("wifipass", true) ? request->getParam("wifipass", true)->value() : "";

    newSSID.trim();
    newWifiPass.trim();

    if (newSSID.length() == 0) {
      request->send(400, "text/plain", "SSID cannot be empty.");
      return;
    }

    String currentSSID, currentPass;
    readConfig(currentSSID, currentPass);

    File f = SPIFFS.open("/config.txt", "w");
    if (f) {
      f.println(newSSID);
      f.println(newWifiPass);
      f.println("ap_ssid=" + g_apSsid);
      f.println("site_name=" + g_siteName);
      f.println("public_wall=" + String(g_publicWall ? "1" : "0"));
      f.close();
      Serial.printf("[SAVE] New SSID: \"%s\"\n", newSSID.c_str());
      appendLog("CONFIG CHANGED: SSID=" + newSSID);
    } else {
      request->send(500, "text/plain", "File write error.");
      return;
    }

    request->send(200, "text/html",
                  "<html><body style='background:#0a0a0a;color:#00ff41;display:flex;justify-content:center;"
                  "align-items:center;height:100vh;font-family:Courier New,monospace;text-align:center'>"
                  "<div><pre style='font-size:1.2rem;text-shadow:0 0 10px #00ff41'>CREDENTIALS SAVED\n\nREBOOTING...</pre></div></body></html>");
    delay(1500);
    ESP.restart();
  });

  // POST /reboot
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAdminAuth(request)) { request->send(403, "text/plain", "Forbidden."); return; }
    request->send(200, "text/html",
                  "<html><body style='background:#0a0a0a;color:#00ff41;display:flex;justify-content:center;"
                  "align-items:center;height:100vh;font-family:Courier New,monospace;text-align:center'>"
                  "<div><pre>REBOOTING...</pre></div></body></html>");
    delay(500);
    ESP.restart();
  });

  // Catch-all: serve landing page
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getLandingPageHtml());
  });

  server.begin();
  Serial.println("[WEB] AsyncWebServer started on port 80.");
}

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n========== rougeap (headless) ==========");

  // --- SPIFFS ---
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed even after format. Halting.");
    while (true) { delay(1000); }
  }
  Serial.println("[SPIFFS] Mounted.");

  // --- Read saved WiFi credentials ---
  String savedSSID, savedPass;
  bool hasCreds = readConfig(savedSSID, savedPass);

  if (hasCreds) {
    Serial.printf("[WIFI] Connecting to \"%s\" ...\n", savedSSID.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < STA_RETRIES) {
      delay(STA_RETRY_MS);
      retries++;
      Serial.printf("[WIFI] Attempt %d/%d\n", retries, STA_RETRIES);
    }

    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      Serial.printf("[WIFI] Connected! IP: %s\n", ip.c_str());
      appendLog("STA CONNECTED: " + savedSSID + " IP=" + ip);
      return;
    }

    Serial.println("[WIFI] STA connection failed. Falling back to AP mode.");
  }

  startCaptivePortal();
}

// ============================================================
// loop()
// ============================================================
void loop() {
  if (!apMode) return;
  dnsServer.processNextRequest();
}
