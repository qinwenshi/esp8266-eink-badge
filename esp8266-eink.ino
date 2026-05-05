/*
 * Anki card reviewer on ESP8266 + Waveshare 4.2" e-ink (WFT0420CZ15)
 *
 * Button (GPIO0 = on-board FLASH button, active LOW):
 *   Front side  -- short press: flip   |  long press: skip (no answer)
 *   Back  side  -- short press: Good   |  long press: Again
 *
 * Requires AnkiConnect add-on running on the same network:
 *   https://ankiweb.net/shared/info/2055492159
 */
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_420_M01.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "config.h"

// -- Display ------------------------------------------------------------------
#define EPD_CS   15
#define EPD_DC    4
#define EPD_RST   2
#define EPD_BUSY  5

GxEPD2_BW<GxEPD2_420_M01, GxEPD2_420_M01::HEIGHT> display(
    GxEPD2_420_M01(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// -- State machine ------------------------------------------------------------
enum State    { ST_INIT, ST_FRONT, ST_BACK, ST_DONE, ST_NO_CARDS, ST_ERROR };
enum BtnEvent { BTN_NONE, BTN_SHORT, BTN_LONG };
State gState = ST_INIT;

// -- Card data ----------------------------------------------------------------
#define MAX_CARDS 300
#define MAX_TEXT  700

uint32_t gDueCards[MAX_CARDS];
int      gCardCount  = 0;
int      gCardIndex  = 0;
uint32_t gCurrentId  = 0;
char     gFront[MAX_TEXT];
char     gBack[MAX_TEXT];

// -- HTML to plain text -------------------------------------------------------
static void stripHtml(const char* src, char* dst, size_t dstLen) {
    size_t j = 0;
    bool inTag = false;

    for (size_t i = 0; src[i] && j < dstLen - 1; i++) {
        // Remove [sound:...] Anki audio markers
        if (src[i] == '[' && strncmp(&src[i], "[sound:", 7) == 0) {
            while (src[i] && src[i] != ']') i++;
            continue;
        }
        if (src[i] == '<') { inTag = true; continue; }
        if (inTag) {
            if (src[i] == '>') {
                inTag = false;
                if (j > 0 && dst[j - 1] != '\n') dst[j++] = '\n';
            }
            continue;
        }
        if (src[i] == '&') {
            if      (strncmp(&src[i], "&nbsp;", 6) == 0) { dst[j++] = ' ';  i += 5; }
            else if (strncmp(&src[i], "&lt;",   4) == 0) { dst[j++] = '<';  i += 3; }
            else if (strncmp(&src[i], "&gt;",   4) == 0) { dst[j++] = '>';  i += 3; }
            else if (strncmp(&src[i], "&amp;",  5) == 0) { dst[j++] = '&';  i += 4; }
            else if (strncmp(&src[i], "&quot;", 6) == 0) { dst[j++] = '"';  i += 5; }
            else dst[j++] = src[i];
            continue;
        }
        if (src[i] == '\r') continue;
        dst[j++] = src[i];
    }
    dst[j] = '\0';

    // Trim leading whitespace/newlines
    char* p = dst;
    while (*p == ' ' || *p == '\n') p++;
    if (p != dst) memmove(dst, p, strlen(p) + 1);

    // Trim trailing
    j = strlen(dst);
    while (j > 0 && (dst[j - 1] == ' ' || dst[j - 1] == '\n')) j--;
    dst[j] = '\0';
}

// -- AnkiConnect HTTP ---------------------------------------------------------
static String ankiPost(const String& body) {
    WiFiClient client;
    HTTPClient http;
    String url = "http://" ANKI_HOST ":" + String(ANKI_PORT);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    int code = http.POST(body);
    Serial.printf("[HTTP] POST %s -> %d\n", url.c_str(), code);
    String resp = (code == 200) ? http.getString() : "";
    if (code != 200) Serial.printf("[HTTP] error: %s\n", http.errorToString(code).c_str());
    http.end();
    return resp;
}

static bool fetchDueCards() {
    String resp = ankiPost(
        "{\"action\":\"findCards\",\"version\":6,\"params\":{\"query\":\"" ANKI_QUERY "\"}}");
    if (resp.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

    gCardCount = 0;
    for (JsonVariant v : doc["result"].as<JsonArray>()) {
        if (gCardCount >= MAX_CARDS) break;
        gDueCards[gCardCount++] = v.as<uint32_t>();
    }
    return true;
}

static bool fetchCard(uint32_t id) {
    String resp = ankiPost(
        "{\"action\":\"cardsInfo\",\"version\":6,\"params\":{\"cards\":[" + String(id) + "]}}");
    if (resp.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

    JsonObject card = doc["result"][0].as<JsonObject>();
    stripHtml(card["question"] | "", gFront, MAX_TEXT);
    stripHtml(card["answer"]   | "", gBack,  MAX_TEXT);
    gCurrentId = id;
    return true;
}

static void answerCard(uint32_t id, int ease) {
    ankiPost("{\"action\":\"answerCards\",\"version\":6,\"params\":{\"answers\":"
             "[{\"cardId\":" + String(id) + ",\"ease\":" + String(ease) + "}]}}");
}

// -- Text rendering with word wrap --------------------------------------------
static void drawWrapped(const char* text,
                        int x, int y, int maxX, int maxY,
                        const GFXfont* font, uint16_t color) {
    display.setFont(font);
    display.setTextColor(color);
    display.setTextWrap(false);

    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds("Ag", 0, 50, &bx, &by, &bw, &bh);
    int lineH = (int)bh + 5;

    int curX = x;
    int curY = y + lineH;  // GFX cursor Y is baseline
    char word[128];
    const char* p = text;

    while (*p && curY <= maxY) {
        if (*p == '\n') {
            curX = x; curY += lineH; p++;
            while (*p == '\n') { curY += lineH / 2; p++; }
            continue;
        }
        if (curX == x) { while (*p == ' ') p++; }
        if (!*p) break;

        int wLen = 0;
        while (p[wLen] && p[wLen] != ' ' && p[wLen] != '\n') wLen++;
        wLen = min(wLen, 127);
        strncpy(word, p, wLen);
        word[wLen] = '\0';

        display.getTextBounds(word, 0, 50, &bx, &by, &bw, &bh);

        if (curX > x && (int)(curX + bw) > maxX) {
            curX = x; curY += lineH;
            if (curY > maxY) break;
        }
        display.setCursor(curX, curY);
        display.print(word);
        curX += (int)bw + 4;
        p += wLen;
        if (*p == ' ') p++;
    }
}

// -- Screen layouts -----------------------------------------------------------
static void drawStatusBar() {
    display.fillRect(0, 0, 400, 20, GxEPD_WHITE);
    display.drawLine(0, 20, 400, 20, GxEPD_BLACK);
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(4, 14);
    display.print("Anki");

    char buf[24];
    snprintf(buf, sizeof(buf), "%d / %d", gCardIndex + 1, gCardCount);
    display.setCursor(55, 14);
    display.print(buf);

    display.setCursor(320, 14);
    display.print(WiFi.RSSI() > -70 ? "WiFi" : "Weak");
}

static void drawHintBar(bool isFront) {
    display.fillRect(0, 276, 400, 24, GxEPD_WHITE);
    display.drawLine(0, 276, 400, 276, GxEPD_BLACK);
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);
    if (isFront) {
        display.setCursor(8,  293); display.print("[short] flip");
        display.setCursor(215, 293); display.print("[long] skip");
    } else {
        display.setCursor(8,  293); display.print("[short] Good");
        display.setCursor(215, 293); display.print("[long] Again");
    }
}

static void drawCard(bool partial) {
    bool isFront = (gState == ST_FRONT);

    if (partial) display.setPartialWindow(0, 0, 400, 300);
    else         display.setFullWindow();

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawStatusBar();
        drawHintBar(isFront);

        if (isFront) {
            drawWrapped(gFront, 10, 26, 390, 268, &FreeMonoBold12pt7b, GxEPD_BLACK);
        } else {
            // Front text small at top, full answer below divider
            drawWrapped(gFront, 10, 24, 390,  90, &FreeMonoBold9pt7b,  GxEPD_BLACK);
            display.drawLine(10, 100, 390, 100, GxEPD_BLACK);
            drawWrapped(gBack,  10, 104, 390, 268, &FreeMonoBold12pt7b, GxEPD_BLACK);
        }
    } while (display.nextPage());
}

static void drawMessage(const char* line1, const char* line2 = nullptr) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeMonoBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(20, 145);
        display.print(line1);
        if (line2) {
            display.setFont(&FreeMonoBold9pt7b);
            display.setCursor(20, 175);
            display.print(line2);
        }
    } while (display.nextPage());
}

// -- Button: short / long press -----------------------------------------------
static BtnEvent checkButton() {
    if (digitalRead(BTN_PIN) != LOW) return BTN_NONE;
    delay(20);  // debounce
    if (digitalRead(BTN_PIN) != LOW) return BTN_NONE;

    uint32_t t = millis();
    while (digitalRead(BTN_PIN) == LOW) {
        if (millis() - t > 2000) break;
        delay(10);
    }
    return (millis() - t >= LONG_PRESS_MS) ? BTN_LONG : BTN_SHORT;
}

// -- Load next card -----------------------------------------------------------
static void nextCard();

static void nextCard() {
    gCardIndex++;
    if (gCardIndex >= gCardCount) {
        gState = ST_DONE;
        char buf[24];
        snprintf(buf, sizeof(buf), "Reviewed %d cards", gCardCount);
        drawMessage("All done!", buf);
        return;
    }
    if (fetchCard(gDueCards[gCardIndex])) {
        gState = ST_FRONT;
        drawCard(false);  // full refresh on new card to avoid ghosting
    } else {
        nextCard();  // skip on fetch error
    }
}

// -- Setup & Loop -------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    pinMode(BTN_PIN, INPUT_PULLUP);

    display.init(115200, true, 10, false);

    drawMessage("Connecting...", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts++ < 40) delay(500);

    if (WiFi.status() != WL_CONNECTED) {
        gState = ST_ERROR;
        drawMessage("WiFi failed", "Check config.h");
        return;
    }
    Serial.println("[WiFi] " + WiFi.localIP().toString());

    drawMessage("Loading cards...", ANKI_HOST);
    if (!fetchDueCards()) {
        gState = ST_ERROR;
        drawMessage("AnkiConnect failed", "Anki open? IP ok?");
        Serial.println("[ERR] AnkiConnect unreachable: " ANKI_HOST);
        return;
    }
    if (gCardCount == 0) {
        gState = ST_NO_CARDS;
        drawMessage("No cards due", "Come back later!");
        return;
    }

    if (fetchCard(gDueCards[0])) {
        gState = ST_FRONT;
        drawCard(false);
    }
}

void loop() {
    if (gState != ST_FRONT && gState != ST_BACK) return;

    BtnEvent ev = checkButton();
    if (ev == BTN_NONE) return;

    if (gState == ST_FRONT) {
        if (ev == BTN_SHORT) {
            gState = ST_BACK;
            drawCard(true);  // fast partial refresh for flip
        } else {
            nextCard();      // long press: skip
        }
    } else {  // ST_BACK
        answerCard(gCurrentId, ev == BTN_SHORT ? 3 : 1);  // Good=3, Again=1
        nextCard();
    }
}
