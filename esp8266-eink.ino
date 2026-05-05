/*
 * Anki card reviewer on ESP8266 + GoodDisplay 3.7" BWR e-ink (GDEY037Z03)
 * Three-color display: Black / White / Red
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
#include <GxEPD2_3C.h>
#include "GxEPD2_371_Z03.h"
#include <Fonts/FreeMonoBold9pt7b.h>
#include "config.h"

// -- Display ------------------------------------------------------------------
#define EPD_CS   15
#define EPD_DC    4
#define EPD_RST   2
#define EPD_BUSY  5

// Screen: 240 x 416 (portrait)
#define SCR_W      240
#define SCR_H      416
#define STATUS_H    18   // status bar height (0 .. STATUS_H-1)
#define HINT_Y     398   // hint bar top (= SCR_H - 18)
#define CONTENT_Y  (STATUS_H + 1)
#define CONTENT_MAXY (HINT_Y - 2)

GxEPD2_3C<GxEPD2_371_Z03, GxEPD2_371_Z03::HEIGHT> display(
    GxEPD2_371_Z03(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

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
    display.fillRect(0, 0, SCR_W, STATUS_H, GxEPD_WHITE);
    display.drawLine(0, STATUS_H, SCR_W, STATUS_H, GxEPD_RED);
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(2, 13);
    display.print("Anki");

    char buf[20];
    snprintf(buf, sizeof(buf), "%d/%d", gCardIndex + 1, gCardCount);
    display.setCursor(38, 13);
    display.print(buf);

    display.setCursor(190, 13);
    display.print(WiFi.RSSI() > -70 ? "WiFi" : "Weak");
}

static void drawHintBar(bool isFront) {
    display.fillRect(0, HINT_Y, SCR_W, SCR_H - HINT_Y, GxEPD_WHITE);
    display.drawLine(0, HINT_Y, SCR_W, HINT_Y, GxEPD_RED);
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);
    if (isFront) {
        display.setCursor(4,  SCR_H - 5); display.print("[short]flip  [long]skip");
    } else {
        display.setCursor(4,  SCR_H - 5); display.print("[short]Good  [long]Again");
    }
}

// BWR display does not support partial refresh — always full window
static void drawCard() {
    bool isFront = (gState == ST_FRONT);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawStatusBar();
        drawHintBar(isFront);

        if (isFront) {
            drawWrapped(gFront, 4, CONTENT_Y, SCR_W - 4, CONTENT_MAXY,
                        &FreeMonoBold9pt7b, GxEPD_BLACK);
        } else {
            // Front text in red (small) at top of content area (~3 lines at 9pt)
            drawWrapped(gFront, 4, CONTENT_Y, SCR_W - 4, CONTENT_Y + 52,
                        &FreeMonoBold9pt7b, GxEPD_RED);
            display.drawLine(4, CONTENT_Y + 56, SCR_W - 4, CONTENT_Y + 56, GxEPD_RED);
            // Answer in black below divider
            drawWrapped(gBack,  4, CONTENT_Y + 60, SCR_W - 4, CONTENT_MAXY,
                        &FreeMonoBold9pt7b, GxEPD_BLACK);
        }
    } while (display.nextPage());
}

static void drawMessage(const char* line1, const char* line2 = nullptr) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(4, SCR_H / 2 - 10);
        display.print(line1);
        if (line2) {
            display.setFont(nullptr);
            display.setTextColor(GxEPD_RED);
            display.setCursor(4, SCR_H / 2 + 15);
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
        drawCard();
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
        drawCard();
    }
}

void loop() {
    if (gState != ST_FRONT && gState != ST_BACK) return;

    BtnEvent ev = checkButton();
    if (ev == BTN_NONE) return;

    if (gState == ST_FRONT) {
        if (ev == BTN_SHORT) {
            gState = ST_BACK;
            drawCard();      // full refresh (BWR has no partial update)
        } else {
            nextCard();      // long press: skip
        }
    } else {  // ST_BACK
        answerCard(gCurrentId, ev == BTN_SHORT ? 3 : 1);  // Good=3, Again=1
        nextCard();
    }
}
