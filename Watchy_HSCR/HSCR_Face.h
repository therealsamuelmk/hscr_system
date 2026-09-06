#ifndef HSCR_FACE_H
#define HSCR_FACE_H

// ----------------------------------------------------------------------------
// HSCR_Face.h
// ----------------------------------------------------------------------------
// Display + network logic for the HSCR table watch. Deliberately NOT built on
// the Watchy library's `Watchy` base class: that class deep-sleeps at the end
// of init() (its whole design point for e-paper battery life), which is
// incompatible with the "stay connected, poll every 10-15s" behaviour this
// system needs. Instead this drives the same GxEPD2 panel directly, the way
// HSCR_PinTest.ino already does on this exact hardware.
//
// Trade-off accepted for this phase: WiFi stays associated continuously, so
// battery life will be much shorter than a stock Watchy. Step count and
// battery percentage from the original watchface are NOT reproduced here —
// those came from the Watchy library's internal accelerometer/ADC handling,
// which isn't vendored in this repo, so rather than guess at pin numbers /
// drivers this build can't verify, they're left out. Wire them back in if you
// can share the Watchy library's header (battery ADC pin + accelerometer
// driver), or with real hardware to test against.
// ----------------------------------------------------------------------------

#include "settings.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ---- pin map (mirrors HSCR_PinTest.ino) ------------------------------------
#if WATCHY_HW == 3
  #define PIN_CS    33
  #define PIN_DC    34
  #define PIN_RES   35
  #define PIN_BUSY  36
  #define PIN_SCK   47
  #define PIN_MOSI  48
  #define PIN_MISO  46
  #define BTN_MENU   7
  #define BTN_BACK   6
  #define BTN_UP     0
  #define BTN_DOWN   8
  #define PIN_VIB   17
#else
  #define PIN_CS     5
  #define PIN_DC    10
  #define PIN_RES    9
  #define PIN_BUSY  19
  #define PIN_SCK   18
  #define PIN_MOSI  23
  #define PIN_MISO  -1
  #define BTN_MENU  26
  #define BTN_BACK  25
  #define BTN_UP    35
  #define BTN_DOWN   4
  #define PIN_VIB   13
#endif

#define SCR_W 200
#define SCR_H 200

static GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(PIN_CS, PIN_DC, PIN_RES, PIN_BUSY));

// SERVICES: wording mirrors the SERVICES map in hscr-portal.html so a guest
// reads the same phrasing on the portal and on the watch.
struct ServiceLabel { const char *key; const char *watchText; };
static const ServiceLabel SERVICES[] = {
  { "payment", "PAYMENT NEEDED AT TABLE " },
  { "waiter",  "WAITER NEEDED AT TABLE "  },
  { "order",   "ORDER REQUEST AT TABLE "  },
};
static const int SERVICES_COUNT = sizeof(SERVICES) / sizeof(SERVICES[0]);

static const char *serviceWatchText(const String &key) {
  for (int i = 0; i < SERVICES_COUNT; i++) {
    if (key == SERVICES[i].key) return SERVICES[i].watchText;
  }
  return "SERVICE NEEDED AT TABLE ";
}

enum FaceState { FACE_IDLE, FACE_ALERT };

class HSCRFace {
public:
  void begin();
  void loop();

private:
  FaceState state_ = FACE_IDLE;
  String activeService_ = "";
  int lastDrawnMinute_ = -1;
  unsigned long lastPollMs_ = 0;
  bool btnWasDown_ = false;

  void connectWiFi();
  void syncTime();
  bool pollState();     // true if it changed activeService_/state_
  void sendResolve();
  void buzz(uint16_t ms);

  void drawIdle();
  void drawAlert();
  void printCentered(const String &txt, int16_t y);
  String two(int v);
};

// ---------------------------------------------------------------------------

inline String HSCRFace::two(int v) {
  return (v < 10) ? "0" + String(v) : String(v);
}

inline void HSCRFace::printCentered(const String &txt, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCR_W - (int16_t)w) / 2, y);
  display.print(txt);
}

inline void HSCRFace::buzz(uint16_t ms) {
  digitalWrite(PIN_VIB, HIGH);
  delay(ms);
  digitalWrite(PIN_VIB, LOW);
}

inline void HSCRFace::connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);        // clear any half-open association first
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to \"%s\"", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected, IP=" + WiFi.localIP().toString());
  } else {
    Serial.printf(" not yet (status=%d, will retry)\n", WiFi.status());
  }
}

inline void HSCRFace::syncTime() {
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
  struct tm t;
  getLocalTime(&t, 8000); // best-effort; idle face just shows 00:00 until this succeeds
}

inline bool HSCRFace::pollState() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // see settings.h note: no cert pinning in this phase

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = String(API_BASE_URL) + "/state.php?table=" + String(TABLE_NUMBER);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    Serial.printf("state.php GET failed: %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
  if (!doc["ok"].as<bool>()) return false;

  int newState = doc["state"] | 0;
  String newService = doc["service"].isNull() ? String("") : doc["service"].as<String>();

  bool changed = (newState == 1 && state_ == FACE_IDLE) ||
                 (newState == 0 && state_ == FACE_ALERT) ||
                 (newState == 1 && newService != activeService_);

  state_ = (newState == 1) ? FACE_ALERT : FACE_IDLE;
  activeService_ = newService;
  return changed;
}

inline void HSCRFace::sendResolve() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = String(API_BASE_URL) + "/resolve.php";
  if (!http.begin(client, url)) return;
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"table\":" + String(TABLE_NUMBER) + "}";
  http.POST(payload);
  http.end();

  state_ = FACE_IDLE;
  activeService_ = "";
}

inline void HSCRFace::drawIdle() {
  struct tm t;
  bool haveTime = getLocalTime(&t, 200);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_BLACK);
    display.setTextWrap(false);

    display.fillRect(0, 0, SCR_W, 28, GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    printCentered("HSCR TABLE " + String(TABLE_NUMBER), 20);

    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeSansBold24pt7b);
    if (haveTime) {
      printCentered(two(t.tm_hour) + ":" + two(t.tm_min), 88);
    } else {
      printCentered("--:--", 88);
    }

    display.drawLine(12, 116, SCR_W - 12, 116, GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    if (haveTime) {
      char dateBuf[24];
      strftime(dateBuf, sizeof(dateBuf), "%a %d %b %Y", &t);
      String date = String(dateBuf);
      date.toUpperCase();
      printCentered(date, 145);
    }

    display.setCursor(14, 175);
    display.print(WiFi.status() == WL_CONNECTED ? "> WATCHING TABLE" : "> WIFI RECONNECTING");
    display.setCursor(14, 196);
    display.print("> SYS OK");
  } while (display.nextPage());

  if (haveTime) lastDrawnMinute_ = t.tm_min;
}

inline void HSCRFace::drawAlert() {
  const char *label = serviceWatchText(activeService_);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextWrap(true);

    display.fillRect(0, 0, SCR_W, 28, GxEPD_BLACK);
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_WHITE);
    printCentered("SERVICE NEEDED", 20);

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold24pt7b);
    printCentered("TABLE " + String(TABLE_NUMBER), 78);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 115);
    display.print(String(label) + String(TABLE_NUMBER));

    display.drawLine(12, 150, SCR_W - 12, 150, GxEPD_BLACK);
    display.setCursor(14, 175);
    display.print("> PRESS MENU");
    display.setCursor(14, 196);
    display.print("  WHEN DONE");
  } while (display.nextPage());
}

inline void HSCRFace::begin() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_VIB, OUTPUT);
  digitalWrite(PIN_VIB, LOW);
  // Watchy buttons are active-HIGH (external pull-downs, button to 3V3) on
  // every revision — same wiring the library's ext1 ANY_HIGH wake relies on.
  pinMode(BTN_MENU, INPUT);

#if WATCHY_HW == 3
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
#endif
  display.init(115200);
  display.setRotation(0);

  // Scan so the serial log shows whether the target SSID is even visible
  // (this chip is 2.4GHz-only). Retry a few times — the first scan right
  // after radio init often comes back empty.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  delay(200);
  for (int attempt = 1; attempt <= 4; attempt++) {
    int n = WiFi.scanNetworks();
    Serial.printf("WiFi scan #%d: %d networks\n", attempt, n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %2d) %-32s  rssi=%d  ch=%d\n",
                    i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    }
    if (n > 0) break;
    delay(1500);
  }

  connectWiFi();
  syncTime();

  drawIdle();
  lastPollMs_ = millis();
}

inline void HSCRFace::loop() {
  // Resolve button: only acts while an alert is showing.
  bool btnDown = (digitalRead(BTN_MENU) == HIGH);
  if (btnDown && !btnWasDown_ && state_ == FACE_ALERT) {
    sendResolve();
    buzz(60);
    drawIdle();
  }
  btnWasDown_ = btnDown;

  unsigned long now = millis();
  if (now - lastPollMs_ >= POLL_INTERVAL_MS) {
    lastPollMs_ = now;

    FaceState before = state_;
    bool changed = pollState();

    if (changed && state_ == FACE_ALERT) {
      buzz(400);
      drawAlert();
    } else if (changed && state_ == FACE_IDLE) {
      drawIdle();
    } else if (!changed && state_ == FACE_IDLE) {
      // Redraw the clock roughly once a minute so it stays accurate,
      // without a full e-paper refresh on every 12s poll.
      struct tm t;
      if (getLocalTime(&t, 200) && t.tm_min != lastDrawnMinute_) {
        drawIdle();
      }
    }
    (void) before;
  }
}

#endif
