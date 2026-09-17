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

// SERVICES: wording (and icons) mirror the SERVICES map in hscr-portal.html
// so the phrasing matches, even though this page is served entirely by the
// watch. `icon` is the inner markup of a 0-24 viewBox SVG (stroke style).
struct ServiceLabel {
  const char *key, *label, *desc, *watchText, *doneText, *icon;
};
static const ServiceLabel SERVICES[] = {
  { "payment", "Make payment", "Bring the bill and card machine",
    "PAYMENT NEEDED AT TABLE ", "The bill is on its way to table ",
    "<rect x='2.5' y='5.5' width='19' height='13' rx='2.5'/><path d='M2.5 10h19'/><path d='M6 14.5h3.5'/>" },
  { "waiter", "Request waiter", "Someone comes to your table",
    "WAITER NEEDED AT TABLE ", "A waiter is heading to table ",
    "<path d='M3.5 17.5h17'/><path d='M4.8 14.2a7.2 7.2 0 0 1 14.4 0z'/><path d='M12 4v3'/><circle cx='12' cy='3.2' r='1.1'/>" },
  { "order", "Make an order", "Ready to order food or drinks",
    "ORDER REQUEST AT TABLE ", "A waiter will take your order at table ",
    "<path d='M6 3h9l4 4v14a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z'/><path d='M14.5 3v4.5H19'/><path d='M8.5 12.5h7M8.5 16.5h4.5'/>" },
};
static const int SERVICES_COUNT = sizeof(SERVICES) / sizeof(SERVICES[0]);

static const char *serviceWatchText(const String &key) {
  for (int i = 0; i < SERVICES_COUNT; i++) {
    if (key == SERVICES[i].key) return SERVICES[i].watchText;
  }
  return "SERVICE NEEDED AT TABLE ";
}

static const char *serviceDoneText(const String &key) {
  for (int i = 0; i < SERVICES_COUNT; i++) {
    if (key == SERVICES[i].key) return SERVICES[i].doneText;
  }
  return "Staff is on the way to table ";
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

// Shared <head>: iOS-style glassmorphism, entirely self-contained (no
// external fonts/CDNs — the watch's AP has no internet to fetch them from).
static const char PAGE_STYLE[] =
  "<style>"
  ":root{--glass:rgba(255,255,255,.14);--glass-strong:rgba(255,255,255,.22);"
  "--border:rgba(255,255,255,.35);--muted:rgba(255,255,255,.72)}"
  "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
  "html,body{margin:0;padding:0}"
  "body{"
  "font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text',system-ui,sans-serif;"
  "color:#fff;min-height:100vh;display:flex;justify-content:center;"
  "background:"
  "radial-gradient(circle at 15% 8%,#8B5CF6 0%,transparent 45%),"
  "radial-gradient(circle at 88% 18%,#EC4899 0%,transparent 42%),"
  "radial-gradient(circle at 50% 105%,#3B82F6 0%,transparent 55%),"
  "linear-gradient(160deg,#4B23AC 0%,#1B1530 100%);"
  "background-attachment:fixed;"
  "padding:max(28px,env(safe-area-inset-top)) 20px max(28px,env(safe-area-inset-bottom))"
  "}"
  ".wrap{width:100%;max-width:420px;display:flex;flex-direction:column;gap:18px}"
  ".brand{display:flex;align-items:center;gap:12px}"
  ".mark{width:44px;height:44px;border-radius:14px;background:var(--glass-strong);"
  "backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);border:1px solid var(--border);"
  "display:grid;place-items:center;font-weight:800;font-size:14px}"
  ".brand b{display:block;font-size:15px;font-weight:800}"
  ".brand small{display:block;color:var(--muted);font-size:12.5px}"
  ".glass{background:var(--glass);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);"
  "border:1px solid var(--border);border-radius:26px;box-shadow:0 8px 32px rgba(0,0,0,.25)}"
  "h1{font-size:24px;font-weight:800;margin:0 0 6px;letter-spacing:-.02em}"
  ".lead{color:var(--muted);font-size:14.5px;margin:0;line-height:1.5}"
  ".field{padding:16px 18px;display:flex;align-items:center;gap:14px}"
  ".field label{font-size:13px;font-weight:600;color:var(--muted);flex:none}"
  ".field input{flex:1;min-width:0;border:0;background:transparent;outline:none;"
  "font-family:inherit;color:#fff;font-size:26px;font-weight:800;text-align:right}"
  "input[type=number]::-webkit-outer-spin-button,input[type=number]::-webkit-inner-spin-button{"
  "-webkit-appearance:none;margin:0}"
  ".services{display:flex;flex-direction:column;gap:12px}"
  ".svc{position:relative;display:block}"
  ".svc input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer}"
  ".svc .card{display:flex;align-items:center;gap:14px;padding:16px 18px;border-radius:20px;"
  "background:var(--glass);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);"
  "border:1px solid var(--border);transition:background .18s ease,border-color .18s ease,transform .12s ease}"
  ".svc input:checked~.card{background:linear-gradient(135deg,rgba(139,92,246,.55),rgba(236,72,153,.35));"
  "border-color:rgba(255,255,255,.7)}"
  ".svc input:active~.card{transform:scale(.98)}"
  ".icon{width:42px;height:42px;flex:none;border-radius:13px;background:rgba(255,255,255,.18);"
  "display:grid;place-items:center}"
  ".icon svg{width:20px;height:20px;stroke:#fff;fill:none;stroke-width:1.9;stroke-linecap:round;stroke-linejoin:round}"
  ".card b{display:block;font-size:15.5px;font-weight:700}"
  ".card span{display:block;font-size:12px;color:var(--muted);margin-top:2px}"
  "button{width:100%;padding:17px;border:0;border-radius:20px;font-family:inherit;"
  "background:linear-gradient(135deg,#8B5CF6,#6D3BE4);color:#fff;font-size:16px;font-weight:700;"
  "box-shadow:0 10px 24px rgba(109,59,228,.45)}"
  "button:active{transform:scale(.98)}"
  "a.back{display:inline-flex;align-items:center;gap:6px;color:#fff;font-weight:700;"
  "text-decoration:none;font-size:14.5px}"
  ".tick{width:56px;height:56px;border-radius:50%;background:var(--glass-strong);"
  "backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);border:1px solid var(--border);"
  "display:grid;place-items:center;margin:0 auto 4px}"
  ".tick svg{width:26px;height:26px;stroke:#fff;fill:none;stroke-width:2.4;stroke-linecap:round;stroke-linejoin:round}"
  "</style>";

inline String HSCRFace::pageOrderForm() {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<title>HSCR Table Service</title>";
  html += PAGE_STYLE;
  html +=
    "</head><body><div class='wrap'>"
    "<div class='brand'><div class='mark'>HS</div>"
    "<div><b>HSCR</b><small>Table service</small></div></div>"

    "<div class='glass' style='padding:24px 22px'>"
    "<h1>What do you need?</h1>"
    "<p class='lead'>Confirm your table and choose a service &mdash; staff is notified instantly.</p>"
    "</div>"

    "<form method='POST' action='/request'>"
    "<div class='glass field' style='margin-bottom:2px'>"
    "<label for='table'>Table</label>"
    "<input type='number' inputmode='numeric' id='table' name='table' min='1' max='99' "
    "value='" + String(TABLE_NUMBER) + "' required>"
    "</div>"

    "<div class='services' style='margin-top:16px'>";

  for (int i = 0; i < SERVICES_COUNT; i++) {
    html += "<label class='svc'>"
            "<input type='radio' name='service' value='" + String(SERVICES[i].key) + "'"
            + (i == 0 ? " checked" : "") + ">"
            "<div class='card'>"
            "<span class='icon'><svg viewBox='0 0 24 24'>" + String(SERVICES[i].icon) + "</svg></span>"
            "<span><b>" + String(SERVICES[i].label) + "</b>"
            "<span>" + String(SERVICES[i].desc) + "</span></span>"
            "</div></label>";
  }

  html +=
    "</div>"
    "<button type='submit' style='margin-top:20px'>Send request</button>"
    "</form>"
    "</div></body></html>";
  return html;
}

inline String HSCRFace::pageThanks(const String &table, const String &service) {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<title>Request sent</title>";
  html += PAGE_STYLE;
  html +=
    "</head><body><div class='wrap'>"
    "<div class='brand'><div class='mark'>HS</div>"
    "<div><b>HSCR</b><small>Table service</small></div></div>"

    "<div class='glass' style='padding:32px 24px;text-align:center'>"
    "<div class='tick'><svg viewBox='0 0 24 24'><path d='M4 12.5l5.5 5.5L20 6.5'/></svg></div>"
    "<h1>Request sent</h1>"
    "<p class='lead'>" + String(serviceDoneText(service)) + table + ".</p>"
    "<p class='lead' style='margin-top:6px'>Your waiter's watch is buzzing now.</p>"
    "</div>"

    "<a class='back' href='/'>&larr; Send another request</a>"
    "</div></body></html>";
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
  Serial.printf("MENU pressed -> resolved table=%s service=%s\n",
                activeTable_.c_str(), activeService_.c_str());
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
    Serial.printf("GET / from %s\n", server_.client().remoteIP().toString().c_str());
    server_.send(200, "text/html", pageOrderForm());
  });

  server_.on("/request", HTTP_POST, [this]() {
    String table = server_.arg("table");
    String service = server_.arg("service");
    table.trim();
    Serial.printf("POST /request table=\"%s\" service=\"%s\"\n", table.c_str(), service.c_str());

    bool validTable = table.length() > 0 && table.length() <= 2;
    for (size_t i = 0; validTable && i < table.length(); i++) {
      if (!isDigit(table[i])) validTable = false;
    }
    bool validService = false;
    for (int i = 0; i < SERVICES_COUNT; i++) {
      if (service == SERVICES[i].key) validService = true;
    }

    if (!validTable || !validService) {
      Serial.println("  -> rejected (invalid table/service)");
      server_.send(400, "text/plain", "Please choose a table number and a service.");
      return;
    }

    Serial.println("  -> accepted, alert started");
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
    server_.on(path, HTTP_GET, [this, path]() {
      Serial.printf("captive-portal probe %s -> redirecting to /\n", path);
      server_.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
      server_.send(302, "text/plain", "");
    });
  }
  server_.onNotFound([this]() {
    Serial.printf("unhandled %s -> redirecting to /\n", server_.uri().c_str());
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
