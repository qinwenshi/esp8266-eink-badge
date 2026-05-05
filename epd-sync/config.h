#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID     "ChinaNet-C2d2"
#define WIFI_PASSWORD "5g7k8e17"

// ── AnkiConnect (running on your PC/Mac, same WiFi) ──────────────────────────
#define ANKI_HOST  "192.168.1.23"   // IP of the machine running Anki
#define ANKI_PORT  8765
#define ANKI_QUERY "is:new"

// ── Button ───────────────────────────────────────────────────────────────────
#define BTN_PIN       0    // GPIO0 = FLASH button on board, active LOW
#define LONG_PRESS_MS 600  // ms threshold: short < 600ms < long

// ── EPD Sync ─────────────────────────────────────────────────────────────────
// SYNC_MODE 0 = Pull (device polls server)   1 = Push (device runs HTTP server)
#define SYNC_MODE         0
#define SYNC_HOST         "192.168.1.23"   // IP of the machine running epd-tool serve
#define SYNC_PORT         8080
#define PULL_INTERVAL_MS  60000            // poll every 60 s (Pull mode)
