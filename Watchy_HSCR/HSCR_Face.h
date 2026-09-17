#ifndef HSCR_FACE_H
#define HSCR_FACE_H

// ----------------------------------------------------------------------------
// HSCR_Face.h — self-contained, offline table-service watch.
// ----------------------------------------------------------------------------
// No venue WiFi, no internet, no backend server. The watch broadcasts its own
// WiFi network (AP mode — proven reliable on this hardware, unlike joining an
// external network in STA mode); a guest's phone joins it, opens a browser,
// and gets a tiny order page served directly by the watch. Submitting the
// form updates the display immediately and buzzes repeatedly until staff
// presses MENU to mark it delivered.
//
// A DNS server that answers every lookup with the watch's own IP is what
// makes most phones auto-pop the "Sign in to network" page after joining
// (the standard captive-portal trick); if that doesn't fire on a given
// phone, browsing to 192.168.4.1 manually works too.
// ----------------------------------------------------------------------------

#include "settings.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

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

// SERVICES: wording mirrors the SERVICES map in hscr-portal.html so the
// phrasing matches, even though this page is served entirely by the watch.
struct ServiceLabel { const char *key; const char *label; const char *watchText; };
static const ServiceLabel SERVICES[] = {
  { "payment", "Make payment",   "PAYMENT NEEDED AT TABLE " },
  { "waiter",  "Request waiter", "WAITER NEEDED AT TABLE "  },
  { "order",   "Make an order",  "ORDER REQUEST AT TABLE "  },
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
  String activeTable_ = "";
  bool btnWasDown_ = false;
  unsigned long vibeUntil_ = 0;
  bool vibeOn_ = false;

  DNSServer dns_;
  WebServer server_{80};
  String apSsid_;

  void buzz(uint16_t ms);
  void startAlert(const String &table, const String &service);
  void resolveAlert();
  void vibratePump();   // non-blocking on/off pulse while alerting

  void drawIdle();
  void drawAlert();
  void printCentered(const String &txt, int16_t y);

  void setupRoutes();
  String pageOrderForm();
  String pageThanks(const String &table, const String &service);
};

// ---------------------------------------------------------------------------

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

inline void HSCRFace::drawIdle() {
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
    display.setFont(&FreeMonoBold9pt7b);
    printCentered("JOIN WIFI TO ORDER", 55);

    display.drawLine(12, 68, SCR_W - 12, 68, GxEPD_WHITE);

    display.setFont(&FreeMonoBold12pt7b);
    printCentered(apSsid_, 95);

    display.setFont(&FreeMonoBold9pt7b);
    printCentered(strlen(AP_PASSWORD) ? "PASSWORD: " AP_PASSWORD : "(OPEN NETWORK)", 118);

    display.drawLine(12, 140, SCR_W - 12, 140, GxEPD_WHITE);
    display.setCursor(14, 162);
    display.print("THEN VISIT:");
    display.setCursor(14, 182);
    display.print(WiFi.softAPIP().toString());

    display.setCursor(14, 196);
    display.print("> SYS OK");
  } while (display.nextPage());
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
    printCentered("TABLE " + activeTable_, 78);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 115);
    display.print(String(label) + activeTable_);

    display.drawLine(12, 150, SCR_W - 12, 150, GxEPD_BLACK);
    display.setCursor(14, 175);
    display.print("> PRESS MENU");
    display.setCursor(14, 196);
    display.print("  WHEN DELIVERED");
  } while (display.nextPage());
}

// ---- web pages --------------------------------------------------------------

inline String HSCRFace::pageOrderForm() {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HSCR Table Service</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#EEEBF5;color:#241C3D;"
    "max-width:420px;margin:0 auto;padding:32px 24px}"
    "h1{font-size:26px;margin:0 0 8px}"
    "p{color:#6F6791;margin:0 0 24px}"
    "label{display:block;font-weight:600;margin:0 0 6px;font-size:14px}"
    "input[type=number]{width:100%;font-size:22px;padding:14px;border-radius:14px;"
    "border:2px solid #C6BFD9;margin-bottom:22px;box-sizing:border-box}"
    ".svc{display:block;width:100%;padding:16px;margin-bottom:12px;border-radius:16px;"
    "border:2px solid #C6BFD9;background:#fff;font-size:16px;font-weight:600;text-align:left}"
    "input[type=radio]{margin-right:10px;transform:scale(1.3)}"
    "button{width:100%;padding:16px;margin-top:10px;border:0;border-radius:14px;"
    "background:#6D3BE4;color:#fff;font-size:17px;font-weight:700}"
    "</style></head><body>"
    "<h1>HSCR Table Service</h1>"
    "<p>Enter your table number and choose what you need.</p>"
    "<form method='POST' action='/request'>"
    "<label for='table'>Table number</label>"
    "<input type='number' id='table' name='table' min='1' max='99' value='" + String(TABLE_NUMBER) + "' required>";

  for (int i = 0; i < SERVICES_COUNT; i++) {
    html += "<label class='svc'><input type='radio' name='service' value='" + String(SERVICES[i].key) + "'"
            + (i == 0 ? " checked" : "") + ">" + SERVICES[i].label + "</label>";
  }

  html += "<button type='submit'>Send request</button></form></body></html>";
  return html;
}

inline String HSCRFace::pageThanks(const String &table, const String &service) {
  const char *label = serviceWatchText(service);
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Request sent</title>"
    "<style>body{font-family:system-ui,sans-serif;background:#EEEBF5;color:#241C3D;"
    "max-width:420px;margin:0 auto;padding:48px 24px;text-align:center}"
    "h1{font-size:24px}p{color:#6F6791}"
    "a{display:inline-block;margin-top:24px;color:#6D3BE4;font-weight:700}</style>"
    "</head><body>"
    "<h1>Request sent</h1>"
    "<p>" + String(label) + table + "</p>"
    "<p>Your waiter's watch is buzzing now.</p>"
    "<a href='/'>&larr; Send another request</a>"
    "</body></html>";
  return html;
}

inline void HSCRFace::startAlert(const String &table, const String &service) {
  activeTable_ = table;
  activeService_ = service;
  state_ = FACE_ALERT;
  vibeUntil_ = 0;
  vibeOn_ = false;
  drawAlert();
}

inline void HSCRFace::resolveAlert() {
  state_ = FACE_IDLE;
  digitalWrite(PIN_VIB, LOW);
  drawIdle();
}

inline void HSCRFace::vibratePump() {
  if (state_ != FACE_ALERT) return;
  unsigned long now = millis();
  if (now >= vibeUntil_) {
    vibeOn_ = !vibeOn_;
    digitalWrite(PIN_VIB, vibeOn_ ? HIGH : LOW);
    vibeUntil_ = now + (vibeOn_ ? VIBRATE_ON_MS : VIBRATE_OFF_MS);
  }
}

// ---- routes -------------------------------------------------------------

inline void HSCRFace::setupRoutes() {
  server_.on("/", HTTP_GET, [this]() {
    server_.send(200, "text/html", pageOrderForm());
  });

  server_.on("/request", HTTP_POST, [this]() {
    String table = server_.arg("table");
    String service = server_.arg("service");
    table.trim();

    bool validTable = table.length() > 0 && table.length() <= 2;
    for (size_t i = 0; validTable && i < table.length(); i++) {
      if (!isDigit(table[i])) validTable = false;
    }
    bool validService = false;
    for (int i = 0; i < SERVICES_COUNT; i++) {
      if (service == SERVICES[i].key) validService = true;
    }

    if (!validTable || !validService) {
      server_.send(400, "text/plain", "Please choose a table number and a service.");
      return;
    }

    server_.send(200, "text/html", pageThanks(table, service));
    startAlert(table, service);
  });

  // Captive-portal probes: send everything to us so phones auto-pop the
  // "Sign in to network" page after joining.
  const char *captivePaths[] = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
    "/ncsi.txt", "/connecttest.txt", "/success.txt", "/fwlink"
  };
  for (const char *path : captivePaths) {
    server_.on(path, HTTP_GET, [this]() {
      server_.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
      server_.send(302, "text/plain", "");
    });
  }
  server_.onNotFound([this]() {
    server_.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server_.send(302, "text/plain", "");
  });
}

// ---------------------------------------------------------------------------

inline void HSCRFace::begin() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_VIB, OUTPUT);
  digitalWrite(PIN_VIB, LOW);
  // Watchy buttons are active-HIGH (external pull-downs, button to 3V3) on
  // every revision.
  pinMode(BTN_MENU, INPUT);

#if WATCHY_HW == 3
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
#endif
  display.init(115200);
  display.setRotation(0);

  apSsid_ = String(AP_SSID_PREFIX) + String(TABLE_NUMBER);
  WiFi.mode(WIFI_AP);
  bool ok = strlen(AP_PASSWORD) >= 8
    ? WiFi.softAP(apSsid_.c_str(), AP_PASSWORD)
    : WiFi.softAP(apSsid_.c_str());
  Serial.printf("softAP \"%s\": %s, IP=%s\n",
                apSsid_.c_str(), ok ? "OK" : "FAILED",
                WiFi.softAPIP().toString().c_str());

  dns_.start(53, "*", WiFi.softAPIP());
  setupRoutes();
  server_.begin();

  drawIdle();
}

inline void HSCRFace::loop() {
  dns_.processNextRequest();
  server_.handleClient();
  vibratePump();

  // Resolve button: only acts while an alert is showing.
  bool btnDown = (digitalRead(BTN_MENU) == HIGH);
  if (btnDown && !btnWasDown_ && state_ == FACE_ALERT) {
    resolveAlert();
    buzz(60);
  }
  btnWasDown_ = btnDown;
}

#endif
