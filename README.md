# RougeAP

**Headless ESP32 Rogue AP / Captive Portal**

An ESP32 firmware that boots into **Station (STA) mode** when WiFi credentials are saved, or starts an **open Access Point** with a captive portal when they are not. Features a hacker-themed landing page, optional public message wall, and a full admin panel for configuration.

---

## Features

- **Dual boot mode**
  - **STA mode**: Connects to your home/office WiFi when credentials exist in SPIFFS
  - **AP mode**: Starts an open AP with captive portal when no credentials or connection fails

- **Public landing page**
  - Customizable HTML (editable via admin panel)
  - Optional message wall (social hub) or admin-only log (stealth mode)
  - Hacker-themed ASCII art splash with scanline effects

- **Message submission**
  - **Public wall**: Messages visible to everyone on the landing page
  - **Stealth mode**: Messages stored only in admin log (not visible publicly)

- **Admin panel** (`/admin`)
  - Settings: AP name, site name, public wall toggle
  - WiFi credentials: Configure target SSID and passphrase, deploy & reboot
  - Landing page editor: Edit HTML directly
  - Log viewer: Connection events, MAC addresses, messages
  - Clear log, clear wall, reboot

---

## Hardware

- **ESP32** (any variant with WiFi)

---

## Dependencies

Install via Arduino Library Manager or PlatformIO:

| Library | Purpose |
|---------|---------|
| [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | Async HTTP server |
| [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) | Required by ESPAsyncWebServer |
| Built-in: `WiFi`, `FS`, `SPIFFS`, `DNSServer` | ESP32 Arduino Core |

---

## Setup

1. **Open** `rougeap.ino` in Arduino IDE or PlatformIO
2. **Select** your ESP32 board and port
3. **Upload** the sketch

### First run

1. Power the ESP32 — it will start an open AP (no password)
2. Connect your phone/computer to the AP (default SSID: `SetupWiFi`)
3. A captive portal should open automatically; if not, go to `http://192.168.4.1/portal`
4. Open `http://192.168.4.1/admin` and log in
5. Enter your WiFi credentials in the admin panel and click **Deploy & Reboot**
6. On next boot, the ESP32 will connect to your WiFi (STA mode)

---

## Admin Panel

- **URL**: `http://192.168.4.1/admin` (when in AP mode)
- **Default credentials**: `admin` / `esp32admin`

> **Security note**: Admin credentials are hardcoded. This is intentional for a demo/portfolio project and is **not suitable for production**. Anyone on the unencrypted AP can sniff traffic.

---

## Configuration

Settings are stored in SPIFFS (`/config.txt`):

| Setting | Description |
|---------|-------------|
| `ap_ssid` | Broadcast SSID when in AP mode (default: `SetupWiFi`) |
| `site_name` | Title shown on landing page (default: `funnyportal`) |
| `public_wall` | `1` = messages visible on landing page, `0` = admin log only |

---

## Captive Portal Detection

The firmware redirects common captive portal probe URLs to the landing page:

- `/generate_204`, `/gen_204` (Android)
- `/hotspot-detect.html` (Apple)
- `/connecttest.txt`, `/ncsi.txt`, `/redirect`, `/success.txt`

---

## File Structure (SPIFFS)

| File | Purpose |
|------|---------|
| `/config.txt` | WiFi credentials + settings |
| `/landing.html` | Custom landing page (optional; falls back to built-in) |
| `/log.txt` | Connection log (MAC addresses, messages) |
| `/wall.txt` | Public message wall entries |

---

## Custom Landing Page

Use the admin panel → **Edit HTML** to customize the landing page. Placeholders:

- `%SITE_NAME%` — Replaced with the configured site name
- `%MESSAGE_SECTION%` — Message input form + wall (or omit to inject before `</body>`)

Max size: 25 KB.

---

## License

Use at your own risk. For educational and portfolio purposes only.
