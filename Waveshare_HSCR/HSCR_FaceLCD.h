#ifndef HSCR_FACE_LCD_H
#define HSCR_FACE_LCD_H

// ----------------------------------------------------------------------------
// HSCR_FaceLCD.h — self-contained, offline table-service interface for the
// Waveshare ESP32-S3-Touch-AMOLED-2.06 (QSPI AMOLED + capacitive touch, no
// e-paper — unlike the Watchy_HSCR build this mirrors).
// ----------------------------------------------------------------------------
// Same architecture as Watchy_HSCR: no venue WiFi, no internet, no backend.
// The device broadcasts its own WiFi network; a guest's phone joins it, gets
// a tiny order page served directly, and submitting it queues a pending
// request. The home screen shows four service icons (Order/Bill/Waiter/
// Water); any icon with a pending request shows a red badge, blinking until
// attended. Tapping a badged icon opens a detail screen naming the table;
// its "Attended" button clears that request and returns home, showing the
// next queued request's badge if there is one. This board has no physical
// MENU button and no buzzer/vibration motor (it has a speaker, but driving
// it needs the ES8311 I2S codec — out of scope for now), so touch + a
// blinking badge replace those.
//
// Pin numbers and driver classes below come from Waveshare's own GitHub demo
// repo (github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06,
// examples/arduino/libraries/board_config/pin_config.h) for this exact
// board, not guessed. See [[waveshare-amoled-device]] for the two real bugs
// hit getting this board working (GFX library version, FT3168 reset pulse).
// ----------------------------------------------------------------------------

#include "settings.h"
#include <Arduino_GFX_Library.h>
#include <Arduino_DriveBus_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <SensorPCF85063.hpp>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include "boot_logo.h"
#include "bg_image.h"
#include "service_icons.h"

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
#define PIN_BOOT  0   // BOOT button, active-low

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

static SensorPCF85063 rtc;
static XPowersPMU pmu;

// Brand palette (matches info.hscr.online) plus the four service-tile colours
// from the reference mockup (pulsiq-logo-endorsed.svg / the Ankara-pattern
// home screen design).
#define COLOR_BRAND_BLUE    0x1A5E  // #1842B6
#define COLOR_BADGE_RED     0xE1C6  // #E53935 — badge + WATER tile
#define COLOR_TILE_ORDER    0x4D6A  // #4CAF50
#define COLOR_TILE_BILL     0xE3E4  // #E67E22
#define COLOR_TILE_WAITER   0x2104  // #202020
#define COLOR_TILE_WATER    0xE1C6  // #E53935

// SERVICES: four categories matching the staff home screen's icon grid.
// `key`/`label`/`desc`/`icon` feed the guest-facing web order form; `detail`
// feeds the on-watch detail screen's "<X> needed/requested at Table N"
// message; `doneText` feeds the guest "request sent" page; `color` fills
// the on-watch icon tile.
struct ServiceLabel {
  const char *key, *label, *desc, *detail, *doneText, *icon;
  uint16_t color;
};
static const ServiceLabel SERVICES[] = {
  { "order", "Order", "Ready to order food or drinks",
    "Order requested", "A waiter will take your order at table ",
    "<path d='M6 3h9l4 4v14a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z'/><path d='M14.5 3v4.5H19'/><path d='M8.5 12.5h7M8.5 16.5h4.5'/>",
    COLOR_TILE_ORDER },
  { "bill", "Bill", "Bring the bill and card machine",
    "Bill needed", "The bill is on its way to table ",
    "<rect x='2.5' y='5.5' width='19' height='13' rx='2.5'/><path d='M2.5 10h19'/><path d='M6 14.5h3.5'/>",
    COLOR_TILE_BILL },
  { "waiter", "Waiter", "Someone comes to your table",
    "Waiter needed", "A waiter is heading to table ",
    "<path d='M3.5 17.5h17'/><path d='M4.8 14.2a7.2 7.2 0 0 1 14.4 0z'/><path d='M12 4v3'/><circle cx='12' cy='3.2' r='1.1'/>",
    COLOR_TILE_WAITER },
  { "water", "Water", "Bring a jug or bottle of water",
    "Water needed", "Water is on its way to table ",
    "<path d='M12 3.5c3 4 5.5 7.2 5.5 10.3a5.5 5.5 0 0 1-11 0C6.5 10.7 9 7.5 12 3.5z'/>",
    COLOR_TILE_WATER },
};
static const int SERVICES_COUNT = sizeof(SERVICES) / sizeof(SERVICES[0]);

static int serviceIndex(const String &key) {
  for (int i = 0; i < SERVICES_COUNT; i++) {
    if (key == SERVICES[i].key) return i;
  }
  return -1;
}

static const char *serviceDoneText(const String &key) {
  int i = serviceIndex(key);
  return i >= 0 ? SERVICES[i].doneText : "Staff is on the way to table ";
}

// ---- pending-request queue ---------------------------------------------
// Every submitted guest request is queued here (not just one at a time) —
// several tables can be waiting on different (or the same) service at once.
// A service's icon badge reflects whether *any* queued entry matches it;
// opening that icon shows the oldest matching entry.
struct PendingRequest {
  String table;
  String service;
};
static const int MAX_PENDING = 16;

enum FaceState { FACE_HOME, FACE_DETAIL };

struct IconRect {
  int16_t x, y, w, h;
};

class HSCRFaceLCD {
public:
  void begin();
  void loop();

private:
  FaceState state_ = FACE_HOME;
  PendingRequest pending_[MAX_PENDING];
  int pendingCount_ = 0;
  int detailIndex_ = -1;  // index into pending_ currently shown on the detail screen

  bool touchWasDown_ = false;
  bool idle_ = false;              // screen off, still serving requests
  unsigned long lastActivity_ = 0;
  unsigned long badgeFlashUntil_ = 0;
  bool badgeFlashOn_ = true;
  String lastClockText_ = "";

  DNSServer dns_;
  WebServer server_{80};
  WiFiServer tlsSink_{443};
  String apSsid_;
  void pumpTlsSink();

  bool touchIsDown();
  int pendingCountFor(int serviceIdx);
  int firstPendingFor(int serviceIdx);
  void removePending(int idx);

  void printCentered(const char *txt, int16_t y, uint8_t size);
  void printCenteredAt(const char *txt, int16_t cx, int16_t y, uint8_t size);
  void enterIdle();
  void wake();
  IconRect iconRect(int i);
  IconRect attendedButtonRect();
  void drawServiceGlyph(int serviceIdx, int16_t cx, int16_t cy);
  void drawIconBadge(int i, bool on);
  void badgeFlashPump();

  void drawBootScreen();
  void drawHome();
  void drawDetail();
  void updateClock(bool force);

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

inline void HSCRFaceLCD::printCenteredAt(const char *txt, int16_t cx, int16_t y, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setTextSize(size);
  gfx->getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  gfx->setCursor(cx - (int16_t)w / 2, y);
  gfx->print(txt);
}

// Idle = screen off only. WiFi, the web server and the request queue keep
// running, so guests can still order; a new request wakes the screen by
// itself, otherwise only the BOOT button does (touch is ignored while dark).
inline void HSCRFaceLCD::enterIdle() {
  idle_ = true;
  gfx->displayOff();
  Serial.println("idle: screen off");
}

inline void HSCRFaceLCD::wake() {
  lastActivity_ = millis();
  if (!idle_) return;
  idle_ = false;
  gfx->displayOn();
  if (state_ == FACE_DETAIL) drawDetail(); else drawHome();
  Serial.println("wake: screen on");
}

inline bool HSCRFaceLCD::touchIsDown() {
  int32_t fingers = (int32_t)touch->IIC_Read_Device_Value(
      Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  return fingers > 0;
}

inline int HSCRFaceLCD::pendingCountFor(int serviceIdx) {
  int c = 0;
  for (int i = 0; i < pendingCount_; i++) {
    if (pending_[i].service == SERVICES[serviceIdx].key) c++;
  }
  return c;
}

inline int HSCRFaceLCD::firstPendingFor(int serviceIdx) {
  for (int i = 0; i < pendingCount_; i++) {
    if (pending_[i].service == SERVICES[serviceIdx].key) return i;
  }
  return -1;
}

inline void HSCRFaceLCD::removePending(int idx) {
  if (idx < 0 || idx >= pendingCount_) return;
  for (int i = idx; i < pendingCount_ - 1; i++) pending_[i] = pending_[i + 1];
  pendingCount_--;
}

// ---- layout --------------------------------------------------------------
// 2x2 icon grid, shared by drawing and touch hit-testing so they can never
// disagree with each other.
inline IconRect HSCRFaceLCD::iconRect(int i) {
  const int16_t marginX = 24, gap = 16, iconW = 173, iconH = 150, gridTop = 150;
  int16_t col = i % 2, row = i / 2;
  IconRect r;
  r.x = marginX + col * (iconW + gap);
  r.y = gridTop + row * (iconH + gap);
  r.w = iconW;
  r.h = iconH;
  return r;
}

inline IconRect HSCRFaceLCD::attendedButtonRect() {
  return IconRect{30, 400, (int16_t)(LCD_WIDTH - 60), 80};
}

// Icon art is pre-composited onto each tile's own background colour when
// converted to RGB565, so there's no transparent-edge halo.
inline void HSCRFaceLCD::drawServiceGlyph(int serviceIdx, int16_t cx, int16_t cy) {
  const char *key = SERVICES[serviceIdx].key;
  if (strcmp(key, "order") == 0) {
    gfx->draw16bitRGBBitmap(cx - ICON_ORDER_W / 2, cy - ICON_ORDER_H / 2, ICON_ORDER, ICON_ORDER_W, ICON_ORDER_H);
  } else if (strcmp(key, "bill") == 0) {
    gfx->draw16bitRGBBitmap(cx - ICON_BILL_W / 2, cy - ICON_BILL_H / 2, ICON_BILL, ICON_BILL_W, ICON_BILL_H);
  } else if (strcmp(key, "waiter") == 0) {
    gfx->draw16bitRGBBitmap(cx - ICON_WAITER_W / 2, cy - ICON_WAITER_H / 2, ICON_WAITER, ICON_WAITER_W, ICON_WAITER_H);
  } else {  // water
    gfx->draw16bitRGBBitmap(cx - ICON_WATER_W / 2, cy - ICON_WATER_H / 2, ICON_WATER, ICON_WATER_W, ICON_WATER_H);
  }
}

inline void HSCRFaceLCD::drawIconBadge(int i, bool on) {
  IconRect r = iconRect(i);
  // Pulled in from the corner so the badge's "off" erase-fill (which just
  // repaints the tile's flat colour) doesn't spill past the tile's rounded
  // corner onto the background.
  int16_t bx = r.x + r.w - 20, by = r.y + 20;
  int count = pendingCountFor(i);
  if (count <= 0) return;  // no pending request for this icon — nothing to blink/clear
  if (on) {
    gfx->fillCircle(bx, by, 18, COLOR_BADGE_RED);
    gfx->setTextColor(WHITE);
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", count > 99 ? 99 : count);
    gfx->setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(bx - w / 2, by - h / 2);
    gfx->print(buf);
  } else {
    // Blink "off": paint the badge's circular footprint back over with the
    // tile's own colour instead of redrawing the whole tile.
    gfx->fillCircle(bx, by, 18, SERVICES[i].color);
  }
}

inline void HSCRFaceLCD::drawBootScreen() {
  gfx->draw16bitRGBBitmap(0, 0, BG_IMAGE, BG_W, BG_H);

  // The logo's dark wordmark is unreadable straight on the pattern, so it
  // sits on a white card.
  const int16_t pad = 16;
  int16_t cardW = BOOT_LOGO_W + 2 * pad, cardH = BOOT_LOGO_H + 2 * pad;
  int16_t cardX = (LCD_WIDTH - cardW) / 2, cardY = (LCD_HEIGHT - cardH) / 2;
  gfx->fillRoundRect(cardX, cardY, cardW, cardH, 22, WHITE);
  gfx->draw16bitRGBBitmap(cardX + pad, cardY + pad, BOOT_LOGO, BOOT_LOGO_W, BOOT_LOGO_H);
}

inline void HSCRFaceLCD::drawHome() {
  gfx->draw16bitRGBBitmap(0, 0, BG_IMAGE, BG_W, BG_H);

  gfx->setTextColor(WHITE);
  lastClockText_ = "";  // force the clock to redraw over the fresh background
  updateClock(true);

  printCentered("Table Service", 108, 2);

  for (int i = 0; i < SERVICES_COUNT; i++) {
    IconRect r = iconRect(i);
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 20, SERVICES[i].color);
    // Icon centered in the tile above the label strip (bottom ~34px).
    drawServiceGlyph(i, r.x + r.w / 2, r.y + (r.h - 34) / 2);
    gfx->setTextColor(WHITE);
    printCenteredAt(SERVICES[i].label, r.x + r.w / 2, r.y + r.h - 26, 2);
    drawIconBadge(i, true);
  }
  badgeFlashOn_ = true;
  badgeFlashUntil_ = 0;
}

inline void HSCRFaceLCD::drawDetail() {
  const PendingRequest &req = pending_[detailIndex_];
  int svc = serviceIndex(req.service);
  const char *verb = svc >= 0 ? SERVICES[svc].detail : "Service needed";

  gfx->draw16bitRGBBitmap(0, 0, BG_IMAGE, BG_W, BG_H);

  gfx->setTextColor(WHITE);
  printCentered(verb, 210, 3);
  printCentered(("at Table " + req.table).c_str(), 256, 3);

  IconRect btn = attendedButtonRect();
  gfx->fillRoundRect(btn.x, btn.y, btn.w, btn.h, 20, COLOR_TILE_ORDER);
  gfx->setTextColor(WHITE);
  int16_t x1, y1; uint16_t w, h;
  gfx->setTextSize(3);
  gfx->getTextBounds("Attended", 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(btn.x + (btn.w - (int16_t)w) / 2, btn.y + (btn.h - (int16_t)h) / 2);
  gfx->print("Attended");

  int others = pendingCount_ - 1;
  if (others > 0) {
    int16_t bx = btn.x + btn.w - 6, by = btn.y - 6;
    gfx->fillCircle(bx, by, 16, COLOR_BADGE_RED);
    gfx->setTextColor(WHITE);
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", others > 99 ? 99 : others);
    gfx->setTextSize(1);
    gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(bx - w / 2, by - h / 2);
    gfx->print(buf);
  }
}

// Draws "HH:MM" at the top of the home screen, only repainting when the
// displayed minute actually changes (or `force`, e.g. right after a
// background redraw) so the clock doesn't flicker every loop iteration.
inline void HSCRFaceLCD::updateClock(bool force) {
  RTC_DateTime now = rtc.getDateTime();
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", now.getHour(), now.getMinute());
  if (!force && lastClockText_ == buf) return;
  lastClockText_ = buf;

  gfx->setTextColor(WHITE);
  printCentered(buf, 40, 5);
}

// ---- web pages --------------------------------------------------------------
// Guest-facing HTML/CSS mirrors Watchy_HSCR's style (same monochrome glass
// design) so the ordering experience feels consistent across device types,
// even though this device's own screen now looks completely different.

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
    bool validService = serviceIndex(service) >= 0;

    if (!validTable || !validService) {
      Serial.println("  -> rejected (invalid table/service)");
      server_.send(400, "text/plain", "Please choose a table number and a service.");
      return;
    }

    if (pendingCount_ >= MAX_PENDING) {
      Serial.println("  -> rejected (too many pending requests)");
      server_.send(503, "text/plain", "Too many pending requests — please tell staff directly.");
      return;
    }

    Serial.println("  -> accepted, queued");
    server_.send(200, "text/html", pageThanks(table, service));
    pending_[pendingCount_++] = PendingRequest{table, service};
    wake();
    if (state_ == FACE_HOME) {
      int svc = serviceIndex(service);
      drawIconBadge(svc, true);
      badgeFlashOn_ = true;
      badgeFlashUntil_ = 0;
    }
    // If staff is viewing a *different* request's detail screen, the new
    // one just waits quietly in the queue — no need to interrupt them.
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

  // There's no internet (AP-only) to get real time from, so there's no NTP
  // here. isClockIntegrityGuaranteed() reflects the PCF85063's own
  // oscillator-stop flag — true only if the RTC has run continuously since
  // it was last set. On a truly fresh chip (or one that lost backup power)
  // this is false, so fall back to the firmware's own build time; once set,
  // the battery-backed RTC keeps correct time across reflashes/reboots on
  // its own. Staff can nudge it by a few minutes by reflashing if it drifts
  // — there's no on-device "set time" UI yet.
  // Power button: handled by the AXP2101 power chip itself rather than in
  // firmware — hold ~1s to switch on, hold ~4s to switch off. Being a
  // hardware cut-off, it works even if this firmware ever hangs.
  if (pmu.init(Wire, IIC_SDA, IIC_SCL)) {
    pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_1S);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    pmu.setLongPressPowerOFF();
    pmu.enableLongPressShutdown();
  } else {
    Serial.println("AXP2101 PMU not found - power button long-press not configured");
  }

  pinMode(PIN_BOOT, INPUT_PULLUP);

  rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!rtc.isClockIntegrityGuaranteed()) {
    Serial.println("RTC time not trustworthy, setting from build time");
    rtc.setDateTime(RTC_DateTime(__DATE__, __TIME__));
  }

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

  drawHome();
  lastActivity_ = millis();
}

inline void HSCRFaceLCD::loop() {
  dns_.processNextRequest();
  server_.handleClient();
  pumpTlsSink();

  if (digitalRead(PIN_BOOT) == LOW) wake();

  if (idle_) return;

  // Never go dark while a request is still waiting — the blinking badge is
  // the alert.
  if (pendingCount_ == 0 && millis() - lastActivity_ > IDLE_TIMEOUT_MS) {
    enterIdle();
    return;
  }

  badgeFlashPump();
  if (state_ == FACE_HOME) updateClock(false);

  bool touchDown = touchIsDown();
  if (touchDown) lastActivity_ = millis();
  if (touchDown && !touchWasDown_) {
    int32_t tx = (int32_t)touch->IIC_Read_Device_Value(Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
    int32_t ty = (int32_t)touch->IIC_Read_Device_Value(Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

    if (state_ == FACE_HOME) {
      for (int i = 0; i < SERVICES_COUNT; i++) {
        IconRect r = iconRect(i);
        if (tx >= r.x && tx <= r.x + r.w && ty >= r.y && ty <= r.y + r.h) {
          int idx = firstPendingFor(i);
          if (idx >= 0) {
            Serial.printf("icon %s tapped -> showing table=%s\n",
                          SERVICES[i].key, pending_[idx].table.c_str());
            detailIndex_ = idx;
            state_ = FACE_DETAIL;
            drawDetail();
          }
          break;
        }
      }
    } else {  // FACE_DETAIL
      IconRect btn = attendedButtonRect();
      if (tx >= btn.x && tx <= btn.x + btn.w && ty >= btn.y && ty <= btn.y + btn.h) {
        Serial.printf("Attended -> resolved table=%s service=%s\n",
                      pending_[detailIndex_].table.c_str(), pending_[detailIndex_].service.c_str());
        removePending(detailIndex_);
        detailIndex_ = -1;
        state_ = FACE_HOME;
        drawHome();
      }
    }
  }
  touchWasDown_ = touchDown;
}

// Blinks every icon's badge (if it has one) while on the home screen with
// any pending request — the closest equivalent to a vibration motor this
// board has. Stops the instant the last pending request is attended.
inline void HSCRFaceLCD::badgeFlashPump() {
  if (state_ != FACE_HOME || pendingCount_ == 0) return;
  unsigned long now = millis();
  if (now >= badgeFlashUntil_) {
    badgeFlashOn_ = !badgeFlashOn_;
    for (int i = 0; i < SERVICES_COUNT; i++) drawIconBadge(i, badgeFlashOn_);
    badgeFlashUntil_ = now + (badgeFlashOn_ ? BUZZ_ON_MS : BUZZ_OFF_MS);
  }
}

#endif
