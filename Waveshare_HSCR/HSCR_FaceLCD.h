#ifndef HSCR_FACE_LCD_H
#define HSCR_FACE_LCD_H

// ----------------------------------------------------------------------------
// HSCR_FaceLCD.h — self-contained, offline table-service interface for the
// Waveshare ESP32-S3-Touch-AMOLED-2.06 (QSPI AMOLED + capacitive touch, no
// e-paper — unlike the Watchy_HSCR build this mirrors).
// ----------------------------------------------------------------------------
// Same architecture as Watchy_HSCR: no venue WiFi, no internet, no backend.
// The device broadcasts its own WiFi network; a guest's phone joins it,
// gets a tiny order page served directly, and submitting it updates the
// screen immediately and flashes the alert banner repeatedly until the
// screen is tapped to mark it delivered. This board has no physical MENU
// button and no buzzer/vibration motor (it has a speaker, but driving it
// needs the ES8311 I2S codec — out of scope for now), so touch + a flashing
// screen replace those.
//
// Pin numbers and driver classes below come from Waveshare's own GitHub demo
// repo (github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06,
// examples/arduino/libraries/board_config/pin_config.h) for this exact
// board, not guessed. This firmware was originally built against the wrong
// Waveshare board (ESP32-S3-Touch-LCD-1.69) before a hardware photo showed
// the real one — if touch/display ever go silent again on different unit,
// confirm against that repo rather than assuming these pins.
// ----------------------------------------------------------------------------

#include "settings.h"
#include <Arduino_GFX_Library.h>
#include <Arduino_DriveBus_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

// ---- pin map (Waveshare's pin_config.h for this board) ---------------------
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK  11
#define LCD_CS    12
#define LCD_RESET 8
#define LCD_WIDTH  410
#define LCD_HEIGHT 502

#define IIC_SDA   15
#define IIC_SCL   14
#define TP_INT    38
#define TP_RESET  9

static Arduino_DataBus *gfxBus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_GFX *gfx = new Arduino_CO5300(gfxBus, LCD_RESET, 0 /* rotation */,
                                              LCD_WIDTH, LCD_HEIGHT, 22, 0, 0, 0);

static std::shared_ptr<Arduino_IIC_DriveBus> touchBus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
static void touchInterrupt();
static std::unique_ptr<Arduino_IIC> touch(new Arduino_FT3x68(
    touchBus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, touchInterrupt));
static void touchInterrupt() { touch->IIC_Interrupt_Flag = true; }

// Brand palette (matches info.hscr.online).
#define COLOR_BRAND_BLUE   0x1A5E  // RGB565 for #1842B6
#define COLOR_ALERT_RED    0xC104  // RGB565 for #C8203A
#define COLOR_MUTED        0x7BCF  // RGB565 for #767A7E
#define COLOR_PULSIQ_PURPLE 0x597A  // RGB565 for #5B2FD6 (pulsiq-logo-endorsed.svg)

// SERVICES: identical wording/icons to Watchy_HSCR's HSCR_Face.h so the
// guest-facing page reads the same regardless of which device table they're
// sitting at.
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

class HSCRFaceLCD {
public:
  void begin();
  void loop();

private:
  FaceState state_ = FACE_IDLE;
  String activeService_ = "";
  String activeTable_ = "";
  bool touchWasDown_ = false;
  unsigned long flashUntil_ = 0;
  bool flashOn_ = false;

  DNSServer dns_;
  WebServer server_{80};
  WiFiServer tlsSink_{443};
  String apSsid_;
  void pumpTlsSink();

  bool touchIsDown();
  void flashPump();   // non-blocking alert-banner flash while alerting
  void drawAlertBanner(bool highlighted);

  void printCentered(const char *txt, int16_t y, uint8_t size);
  void drawBootScreen();
  void drawIdle();
  void drawAlert();

  void setupRoutes();
  String pageOrderForm();
  String pageThanks(const String &table, const String &service);
};

// ---------------------------------------------------------------------------

inline void HSCRFaceLCD::printCentered(const char *txt, int16_t y, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setTextSize(size);
  gfx->getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_WIDTH - (int16_t)w) / 2, y);
  gfx->print(txt);
}

inline bool HSCRFaceLCD::touchIsDown() {
  int32_t fingers = (int32_t)touch->IIC_Read_Device_Value(
      Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  return fingers > 0;
}

// Drawn as vector shapes (matching pulsiq-logo-endorsed.svg's own rect +
// circle + arc construction) rather than a rasterized bitmap: a rasterized
// RGB565 bitmap blit through this board's QSPI/DMA path reliably crashed
// with a LoadStoreAlignment panic, so primitives are both simpler and
// safer here.
inline void HSCRFaceLCD::drawBootScreen() {
  gfx->fillScreen(WHITE);

  int16_t cx = LCD_WIDTH / 2;
  int16_t cy = LCD_HEIGHT / 2 - 40;

  gfx->fillRoundRect(cx - 58, cy - 58, 116, 116, 30, COLOR_PULSIQ_PURPLE);
  gfx->fillCircle(cx - 20, cy, 9, WHITE);
  gfx->drawArc(cx - 20, cy, 20, 28, -40, 40, WHITE);
  gfx->drawArc(cx - 20, cy, 35, 43, -40, 40, WHITE);

  gfx->setTextColor(BLACK);
  printCentered("Pulsiq", cy + 92, 4);
  gfx->setTextColor(COLOR_MUTED);
  printCentered("by ServStack Tech Solutions", cy + 136, 2);
}

inline void HSCRFaceLCD::drawIdle() {
  gfx->fillScreen(WHITE);

  gfx->fillRect(0, 0, LCD_WIDTH, 72, COLOR_BRAND_BLUE);
  gfx->setTextColor(WHITE);
  printCentered("PULSIQ", 26, 3);

  gfx->setTextColor(COLOR_MUTED);
  printCentered("WAITING FOR", 220, 3);
  printCentered("REQUESTS", 264, 3);

  gfx->setTextColor(BLACK);
  printCentered(("JOIN WIFI: " + apSsid_).c_str(), 460, 2);
}

inline void HSCRFaceLCD::drawAlertBanner(bool highlighted) {
  gfx->fillRect(0, 0, LCD_WIDTH, 72, highlighted ? WHITE : COLOR_ALERT_RED);
  gfx->setTextColor(highlighted ? COLOR_ALERT_RED : WHITE);
  printCentered("SERVICE NEEDED", 26, 2);
}

inline void HSCRFaceLCD::drawAlert() {
  const char *label = serviceWatchText(activeService_);

  gfx->fillScreen(WHITE);
  drawAlertBanner(false);

  gfx->setTextColor(BLACK);
  printCentered(("TABLE " + activeTable_).c_str(), 140, 5);

  gfx->drawLine(20, 220, LCD_WIDTH - 20, 220, BLACK);

  gfx->setTextSize(2);
  gfx->setCursor(24, 244);
  gfx->print(String(label) + activeTable_);

  gfx->drawLine(20, 320, LCD_WIDTH - 20, 320, BLACK);

  gfx->fillRoundRect(30, 360, LCD_WIDTH - 60, 110, 20, COLOR_BRAND_BLUE);
  gfx->setTextColor(WHITE);
  printCentered("TAP ANYWHERE", 392, 2);
  printCentered("TO MARK DELIVERED", 428, 2);
}

// ---- web pages --------------------------------------------------------------
// Identical guest-facing HTML/CSS to Watchy_HSCR so the ordering experience
// matches regardless of which device's AP the guest joined.

static const char PAGE_STYLE[] =
  "<style>"
  ":root{"
  "--bg:#F0F0F3;--ink:#101012;--muted:#6B6B70;"
  "--glass:rgba(255,255,255,.55);--glass-strong:rgba(255,255,255,.75);"
  "--border:rgba(255,255,255,.8);"
  "--out:6px 6px 14px rgba(0,0,0,.14),-6px -6px 14px rgba(255,255,255,.9);"
  "--out-sm:4px 4px 9px rgba(0,0,0,.12),-4px -4px 9px rgba(255,255,255,.85);"
  "--in:inset 4px 4px 8px rgba(0,0,0,.12),inset -4px -4px 8px rgba(255,255,255,.9);"
  "}"
  "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
  "html,body{margin:0;padding:0}"
  "body{"
  "font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text',system-ui,sans-serif;"
  "color:var(--ink);background:var(--bg);min-height:100vh;display:flex;justify-content:center;"
  "padding:max(28px,env(safe-area-inset-top)) 20px max(28px,env(safe-area-inset-bottom))"
  "}"
  ".wrap{width:100%;max-width:420px;display:flex;flex-direction:column;gap:18px}"
  ".brand{display:flex;align-items:center;gap:12px}"
  ".mark{width:44px;height:44px;border-radius:14px;background:var(--glass-strong);"
  "backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);box-shadow:var(--out-sm);"
  "display:grid;place-items:center;font-weight:800;font-size:14px;color:var(--ink)}"
  ".brand b{display:block;font-size:15px;font-weight:800}"
  ".brand small{display:block;color:var(--muted);font-size:12.5px}"
  ".glass{background:var(--glass);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);"
  "border:1px solid var(--border);border-radius:26px;box-shadow:var(--out)}"
  "h1{font-size:24px;font-weight:800;margin:0 0 6px;letter-spacing:-.02em;color:var(--ink)}"
  ".lead{color:var(--muted);font-size:14.5px;margin:0;line-height:1.5}"
  ".field{padding:16px 18px;display:flex;align-items:center;gap:14px;border-radius:20px;"
  "background:var(--glass);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);"
  "box-shadow:var(--in)}"
  ".field label{font-size:13px;font-weight:600;color:var(--muted);flex:none}"
  ".field input{flex:1;min-width:0;border:0;background:transparent;outline:none;"
  "font-family:inherit;color:var(--ink);font-size:26px;font-weight:800;text-align:right}"
  "input[type=number]::-webkit-outer-spin-button,input[type=number]::-webkit-inner-spin-button{"
  "-webkit-appearance:none;margin:0}"
  ".services{display:flex;flex-direction:column;gap:12px}"
  ".svc{position:relative;display:block}"
  ".svc input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer}"
  ".svc .card{display:flex;align-items:center;gap:14px;padding:16px 18px;border-radius:20px;"
  "background:var(--glass);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);"
  "border:1px solid var(--border);box-shadow:var(--out-sm);"
  "transition:background .18s ease,color .18s ease,transform .12s ease}"
  ".svc input:checked~.card{background:var(--ink);color:#fff;border-color:var(--ink)}"
  ".svc input:checked~.card .muted-text{color:rgba(255,255,255,.72)}"
  ".svc input:checked~.card .icon{background:rgba(255,255,255,.16)}"
  ".svc input:checked~.card .icon svg{stroke:#fff}"
  ".svc input:active~.card{transform:scale(.98)}"
  ".icon{width:42px;height:42px;flex:none;border-radius:13px;background:rgba(0,0,0,.06);"
  "display:grid;place-items:center;transition:background .18s ease}"
  ".icon svg{width:20px;height:20px;stroke:var(--ink);fill:none;stroke-width:1.9;"
  "stroke-linecap:round;stroke-linejoin:round;transition:stroke .18s ease}"
  ".card b{display:block;font-size:15.5px;font-weight:700}"
  ".card span.muted-text{display:block;font-size:12px;color:var(--muted);margin-top:2px;"
  "transition:color .18s ease}"
  "button{width:100%;padding:17px;border:0;border-radius:20px;font-family:inherit;"
  "background:var(--ink);color:#fff;font-size:16px;font-weight:700;box-shadow:var(--out)}"
  "button:active{transform:scale(.98);box-shadow:var(--out-sm)}"
  "a.back{display:inline-flex;align-items:center;gap:6px;color:var(--ink);font-weight:700;"
  "text-decoration:none;font-size:14.5px}"
  ".tick{width:56px;height:56px;border-radius:50%;background:var(--ink);"
  "box-shadow:var(--out);display:grid;place-items:center;margin:0 auto 4px}"
  ".tick svg{width:26px;height:26px;stroke:#fff;fill:none;stroke-width:2.4;stroke-linecap:round;stroke-linejoin:round}"
  "</style>";

inline String HSCRFaceLCD::pageOrderForm() {
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
            "<span class='muted-text'>" + String(SERVICES[i].desc) + "</span></span>"
            "</div></label>";
  }

  html +=
    "</div>"
    "<button type='submit' style='margin-top:20px'>Send request</button>"
    "</form>"
    "</div></body></html>";
  return html;
}

inline String HSCRFaceLCD::pageThanks(const String &table, const String &service) {
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
    "<p class='lead' style='margin-top:6px'>Staff's screen is alerting now.</p>"
    "</div>"

    "<a class='back' href='/'>&larr; Send another request</a>"
    "</div></body></html>";
  return html;
}

inline void HSCRFaceLCD::setupRoutes() {
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
    activeTable_ = table;
    activeService_ = service;
    state_ = FACE_ALERT;
    flashUntil_ = 0;
    flashOn_ = false;
    drawAlert();
  });

  // Captive-portal probes. See [[hscr-deployment]] / HSCR_Face.h for why
  // these serve real 200 content instead of a bare redirect: some OEM
  // builds (Samsung's One UI in particular) only auto-pop the sign-in
  // browser when the probe gets back actual HTML.
  const char *htmlProbePaths[] = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html", "/fwlink"
  };
  for (const char *path : htmlProbePaths) {
    server_.on(path, HTTP_GET, [this, path]() {
      Serial.printf("captive-portal probe %s -> serving order page\n", path);
      server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
      server_.send(200, "text/html", pageOrderForm());
    });
  }
  server_.on("/ncsi.txt", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server_.send(200, "text/plain", "Microsoft NCSI");
  });
  server_.on("/connecttest.txt", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server_.send(200, "text/plain", "Microsoft Connect Test");
  });
  server_.on("/success.txt", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server_.send(200, "text/plain", "success\n");
  });
  server_.onNotFound([this]() {
    Serial.printf("unhandled %s -> redirecting to /\n", server_.uri().c_str());
    server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server_.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server_.send(302, "text/plain", "");
  });
}

inline void HSCRFaceLCD::pumpTlsSink() {
  WiFiClient client = tlsSink_.available();
  if (client) client.stop();
}

// ---------------------------------------------------------------------------

inline void HSCRFaceLCD::begin() {
  Serial.begin(115200);
  delay(200);

  // No backlight pin to drive — this is a self-emissive AMOLED panel, and
  // its power rail is on by default without needing AXP2101 PMU setup
  // (confirmed against Waveshare's own HelloWorld demo, which doesn't
  // touch XPowersLib either).
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  drawBootScreen();
  delay(1500);

  // Waveshare's own demo leaves this chip's reset line untouched by the
  // driver (DRIVEBUS_DEFAULT_VALUE below), relying on a true power-on
  // reset. Re-flashing over USB without unplugging doesn't power-cycle the
  // board, so the FT3168 can be left in an unresponsive state across
  // repeated uploads — pulse its reset line explicitly to recover from that.
  pinMode(TP_RESET, OUTPUT);
  digitalWrite(TP_RESET, HIGH);
  delay(10);
  digitalWrite(TP_RESET, LOW);
  delay(20);
  digitalWrite(TP_RESET, HIGH);
  delay(300);

  Wire.begin(IIC_SDA, IIC_SCL);
  int touchTries = 0;
  while (touch->begin() == false && touchTries < 10) {
    Serial.println("FT3168 touch init failed, retrying...");
    delay(200);
    touchTries++;
  }
  touch->IIC_Write_Device_State(Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                 Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);

  apSsid_ = String(AP_SSID_PREFIX) + String(TABLE_NUMBER);
  WiFi.mode(WIFI_AP);
  bool ok = strlen(AP_PASSWORD) >= 8
    ? WiFi.softAP(apSsid_.c_str(), AP_PASSWORD)
    : WiFi.softAP(apSsid_.c_str());
  Serial.printf("softAP \"%s\": %s, IP=%s\n",
                apSsid_.c_str(), ok ? "OK" : "FAILED",
                WiFi.softAPIP().toString().c_str());

  dns_.setTTL(0);
  dns_.start(53, "*", WiFi.softAPIP());
  setupRoutes();
  server_.begin();
  tlsSink_.begin();

  drawIdle();
}

inline void HSCRFaceLCD::loop() {
  dns_.processNextRequest();
  server_.handleClient();
  pumpTlsSink();
  flashPump();

  // Resolve gesture: tap anywhere while alerting. Whole screen is the
  // button — no coordinate math, nothing that can be thrown off by a
  // rotation setting.
  bool touchDown = touchIsDown();
  if (touchDown && !touchWasDown_ && state_ == FACE_ALERT) {
    Serial.printf("screen tapped -> resolved table=%s service=%s\n",
                  activeTable_.c_str(), activeService_.c_str());
    state_ = FACE_IDLE;
    drawIdle();
  }
  touchWasDown_ = touchDown;
}

// This board has no buzzer/vibration motor, so the alert is a flashing
// banner instead — just the top banner redraws each tick, not the whole
// screen, to keep it smooth on the QSPI link.
inline void HSCRFaceLCD::flashPump() {
  if (state_ != FACE_ALERT) return;
  unsigned long now = millis();
  if (now >= flashUntil_) {
    flashOn_ = !flashOn_;
    drawAlertBanner(flashOn_);
    flashUntil_ = now + (flashOn_ ? BUZZ_ON_MS : BUZZ_OFF_MS);
  }
}

#endif
