// epd-sync.ino — Standalone EPD sync sketch for ESP8266 + WFT0371CZ78 (3.7" BWR 240×416)
// Pull mode: polls HTTP server for new images.
// Push mode: runs an HTTP server that accepts POSTed .epd images.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "GxEPD2_370C_UC8253.h"
#include "config.h"

// ── Pin assignments ───────────────────────────────────────────────────────────
#define EPD_CS   15
#define EPD_DC    4
#define EPD_RST   2
#define EPD_BUSY  5

// ── Display instance ──────────────────────────────────────────────────────────
GxEPD2_3C<GxEPD2_370C_UC8253, GxEPD2_370C_UC8253::HEIGHT> display(
    GxEPD2_370C_UC8253(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ── Shared state ──────────────────────────────────────────────────────────────
uint32_t gLastVersion = 0;

// ── .epd format constants ─────────────────────────────────────────────────────
static const uint8_t EPD_MAGIC[4] = {0x45, 0x50, 0x44, 0x32};  // "EPD2"
static const size_t  PLANE_SIZE   = 12480;  // 240*416/8
static const size_t  EPD_TOTAL    = 8 + 2 * PLANE_SIZE;  // 24968

// ── Helpers ───────────────────────────────────────────────────────────────────
static void drawMessage(const char* line1, const char* line2 = nullptr) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(10, 200);
        display.print(line1);
        if (line2) {
            display.setFont(nullptr);
            display.setTextColor(GxEPD_RED);
            display.setCursor(10, 230);
            display.print(line2);
        }
    } while (display.nextPage());
}

// Show a small error banner at the top via partial refresh (non-destructive).
// Black background, white small text, height=18px.
static void drawErrorBar(const char* msg) {
    Serial.printf("[error] %s\n", msg);
    display.setPartialWindow(0, 0, 240, 18);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_BLACK);
        display.setFont(nullptr);          // tiny built-in font (6x8)
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(4, 5);
        display.print(msg);
    } while (display.nextPage());
}

// Parse and display a fully-loaded .epd buffer (Push mode helper).
bool parseAndDisplay(const uint8_t* buf, size_t len) {
    if (len < EPD_TOTAL) return false;
    if (buf[0] != EPD_MAGIC[0] || buf[1] != EPD_MAGIC[1] ||
        buf[2] != EPD_MAGIC[2] || buf[3] != EPD_MAGIC[3]) return false;
    uint16_t w = buf[4] | (buf[5] << 8);
    uint16_t h = buf[6] | (buf[7] << 8);
    if (w != 240 || h != 416) return false;

    const uint8_t* bw  = buf + 8;
    const uint8_t* red = buf + 8 + PLANE_SIZE;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());

    display.writeImage(bw, red, 0, 0, 240, 416);
    display.refresh(false);
    return true;
}

// ── WiFi connect ──────────────────────────────────────────────────────────────
static void connectWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 20000) {
            Serial.println(" failed!");
            return;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println(" OK");
}

// ── Pull mode ─────────────────────────────────────────────────────────────────
#if SYNC_MODE == 0

#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// Single plane buffer — reused for BW then Red (saves 12 KB vs two buffers)
static uint8_t planeBuf[PLANE_SIZE];

static bool readExact(WiFiClient& stream, uint8_t* dest, size_t n) {
    size_t got = 0;
    uint32_t t = millis();
    while (got < n) {
        if (millis() - t > 10000) return false;
        int avail = stream.available();
        if (avail <= 0) { delay(1); continue; }
        size_t chunk = min((size_t)avail, n - got);
        stream.readBytes(dest + got, chunk);
        got += chunk;
    }
    return true;
}

static void doPull() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) return;
    }

    WiFiClient client;
    HTTPClient http;

    // ── Step 1: fetch /version ────────────────────────────────────────────────
    String versionUrl = String("http://") + SYNC_HOST + ":" + SYNC_PORT + "/version";
    http.begin(client, versionUrl);
    int code = http.GET();
    if (code != 200) {
        http.end();
        char buf[40];
        snprintf(buf, sizeof(buf), "Version err: HTTP %d", code);
        drawErrorBar(buf);
        return;
    }
    uint32_t remoteVersion = (uint32_t)http.getString().toInt();
    http.end();

    if (remoteVersion <= gLastVersion) return;  // nothing new

    // ── Step 2: stream /current.epd ──────────────────────────────────────────
    // Note: skip drawMessage("Syncing...") here — it triggers a full tri-color
    // refresh (~20s), causing a second full refresh when the image arrives.
    // Instead show a lightweight serial log only.
    Serial.println("Syncing...");

    String epdUrl = String("http://") + SYNC_HOST + ":" + SYNC_PORT + "/current.epd";
    http.begin(client, epdUrl);
    http.setTimeout(30000);
    code = http.GET();
    if (code != 200) {
        http.end();
        char buf[40];
        snprintf(buf, sizeof(buf), "Fetch err: HTTP %d", code);
        drawErrorBar(buf);
        return;
    }

    WiFiClient* stream = http.getStreamPtr();

    // Read and validate 8-byte header
    uint8_t header[8];
    if (!readExact(*stream, header, 8)) {
        http.end();
        drawErrorBar("Header read timeout");
        return;
    }
    if (header[0] != EPD_MAGIC[0] || header[1] != EPD_MAGIC[1] ||
        header[2] != EPD_MAGIC[2] || header[3] != EPD_MAGIC[3]) {
        http.end();
        drawErrorBar("Bad magic bytes");
        return;
    }
    uint16_t w = header[4] | (header[5] << 8);
    uint16_t h = header[6] | (header[7] << 8);
    if (w != 240 || h != 416) {
        http.end();
        char buf[40];
        snprintf(buf, sizeof(buf), "Wrong size %dx%d", w, h);
        drawErrorBar(buf);
        return;
    }

    // Write directly to the UC8253 driver (bypass GxEPD2_3C page buffer) so we
    // can route BW and Red planes to the correct hardware commands (0x10 / 0x13)
    // using the single reusable planeBuf instead of two full-size buffers.

    // BW plane → cmd 0x10 (black plane; must use GxEPD_BLACK overload)
    if (!readExact(*stream, planeBuf, PLANE_SIZE)) {
        http.end(); drawErrorBar("BW plane timeout"); return;
    }
    display.epd2.writeImage(planeBuf, 0, 0, 240, 416, (uint16_t)GxEPD_BLACK);

    // Red plane → cmd 0x13 (color plane)
    if (!readExact(*stream, planeBuf, PLANE_SIZE)) {
        http.end(); drawErrorBar("Red plane timeout"); return;
    }
    display.epd2.writeImage(planeBuf, 0, 0, 240, 416, (uint16_t)GxEPD_RED);
    http.end();

    display.epd2.refresh(false);

    gLastVersion = remoteVersion;
}

void setup() {
    Serial.begin(115200);
    display.init(115200, true, 10, false);
    connectWiFi();
    // Screen untouched — EPD retains its last image without power.
    // Only update when new content arrives via doPull().
    Serial.println("Ready, waiting for updates...");
}

void loop() {
    static uint32_t lastPull = 0;
    if (millis() - lastPull >= PULL_INTERVAL_MS) {
        lastPull = millis();
        doPull();
    }
}

// ── Push mode ─────────────────────────────────────────────────────────────────
#else  // SYNC_MODE == 1

#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>

ESP8266WebServer server(SYNC_PORT);
static uint32_t gPushVersion = 0;

// Push mode body is at most 24968 bytes — allocate statically to avoid stack overflow.
static uint8_t pushBuf[EPD_TOTAL];

static void handleStatus() {
    StaticJsonDocument<64> doc;
    doc["version"] = gPushVersion;
    doc["rssi"]    = WiFi.RSSI();
    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}

static void handlePush() {
    size_t len = server.arg("plain").length();
    // ESP8266WebServer stores body in "plain" for raw POST
    if (len < EPD_TOTAL) {
        // Try reading from the raw body if available
        if ((size_t)server.client().available() >= EPD_TOTAL) {
            server.client().readBytes((char*)pushBuf, EPD_TOTAL);
            len = EPD_TOTAL;
        } else {
            server.send(400, "text/plain", "Body too short");
            return;
        }
    } else {
        const String& body = server.arg("plain");
        memcpy(pushBuf, body.c_str(), min(len, EPD_TOTAL));
    }

    if (!parseAndDisplay(pushBuf, min(len, EPD_TOTAL))) {
        server.send(400, "text/plain", "Invalid .epd data");
        return;
    }
    gPushVersion++;
    server.send(200, "text/plain", "OK");
}

void setup() {
    Serial.begin(115200);
    display.init(115200, true, 10, false);
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    MDNS.begin("epd-display");

    server.on("/push",   HTTP_POST, handlePush);
    server.on("/status", HTTP_GET,  handleStatus);
    server.begin();

    drawMessage("Push ready", WiFi.localIP().toString().c_str());
}

void loop() {
    MDNS.update();
    server.handleClient();
}

#endif  // SYNC_MODE
