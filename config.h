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
