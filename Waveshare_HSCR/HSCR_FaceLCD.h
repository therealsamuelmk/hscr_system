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
// next queued request's badge if there is one. The watch vibrates (motor on
// GPIO18) while any request is waiting.
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
#include <esp_netif.h>
#include <dhcpserver/dhcpserver.h>

// The ESP32 soft-AP's hard ceiling on simultaneously connected phones (the
// core's default is only 4).
#define AP_MAX_CLIENTS 10
#include <SensorPCF85063.hpp>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include "boot_logo.h"
#include "ui_fonts.h"
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
#define PIN_MOTOR 18  // vibration motor (NPN switch, motor fed from AXP2101 ALDO3)

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
  String apSsid_;

  bool touchIsDown();
  int pendingCountFor(int serviceIdx);
  int firstPendingFor(int serviceIdx);
  void removePending(int idx);

  void enterIdle();
  void wake();
  IconRect iconRect(int i);
  IconRect attendedButtonRect();
  void alertPump();

  void renderBoot();
  void renderHome();
  void renderDetail();
  void renderClock();
  void render();          // compose the current screen in RAM, then one blit
  void updateClock();

  void setupRoutes();
  String pageOrderForm();
  String pageThanks(const String &table, const String &service);
};

// ---------------------------------------------------------------------------

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
  render();
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

// ---- framebuffer rendering ---------------------------------------------
// Every screen is composed in a full-size framebuffer in PSRAM and pushed to
// the panel in a single transfer. Drawing straight to the panel shape by
// shape made updates visibly crawl down the screen, and the CO5300 panel
// garbles small updates that aren't aligned to even pixels (that was the
// broken-looking badge). Text, badges and tile corners are anti-aliased.
static uint16_t *fb = nullptr;

static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 255) return fg;
  uint32_t ia = 255 - a;
  uint32_t r = (((bg >> 11) & 0x1F) * ia + ((fg >> 11) & 0x1F) * a) / 255;
  uint32_t g = (((bg >> 5) & 0x3F) * ia + ((fg >> 5) & 0x3F) * a) / 255;
  uint32_t b = ((bg & 0x1F) * ia + (fg & 0x1F) * a) / 255;
  return (r << 11) | (g << 5) | b;
}

static inline void fbBlend(int x, int y, uint16_t c, uint8_t a) {
  if (!a || x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  uint16_t &p = fb[y * LCD_WIDTH + x];
  p = blend565(p, c, a);
}

static void fbBackground() {
  memcpy(fb, BG_IMAGE, (size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
}

// Filled rounded rectangle with anti-aliased corners; a square with
// r = w/2 is an anti-aliased circle. `alpha` < 255 gives a translucent fill.
static void fbRoundRect(int x0, int y0, int w, int h, int r, uint16_t c, uint8_t alpha = 255) {
  for (int y = 0; y < h; y++) {
    float dy = 0;
    if (y < r) dy = r - (y + 0.5f);
    else if (y >= h - r) dy = (y + 0.5f) - (h - r);
    for (int x = 0; x < w; x++) {
      float dx = 0;
      if (x < r) dx = r - (x + 0.5f);
      else if (x >= w - r) dx = (x + 0.5f) - (w - r);
      uint8_t a = alpha;
      if (dx > 0 && dy > 0) {
        float cov = r - sqrtf(dx * dx + dy * dy) + 0.5f;
        if (cov <= 0) continue;
        if (cov < 1) a = (uint8_t)(alpha * cov);
      }
      fbBlend(x0 + x, y0 + y, c, a);
    }
  }
}

static void fbImage(int x0, int y0, const uint16_t *img, int w, int h) {
  for (int y = 0; y < h; y++) {
    memcpy(&fb[(y0 + y) * LCD_WIDTH + x0], &img[y * w], w * sizeof(uint16_t));
  }
}

static int fbTextWidth(const AAFont &f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    const char *p = strchr(f.chars, *s);
    if (p) w += f.w[p - f.chars];
  }
  return w;
}

// `y` is the top of the text line (f.h tall).
static void fbText(const AAFont &f, const char *s, int x, int y, uint16_t c) {
  for (; *s; s++) {
    const char *p = strchr(f.chars, *s);
    if (!p) continue;
    int g = p - f.chars, gw = f.w[g];
    const uint8_t *m = f.data + f.off[g];
    for (int yy = 0; yy < f.h; yy++) {
      for (int xx = 0; xx < gw; xx++) fbBlend(x + xx, y + yy, c, m[yy * gw + xx]);
    }
    x += gw;
  }
}

static void fbTextCentered(const AAFont &f, const char *s, int cx, int y, uint16_t c) {
  fbText(f, s, cx - fbTextWidth(f, s) / 2, y, c);
}

static void fbBadge(int cx, int cy, int count) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", count > 99 ? 99 : count);
  fbRoundRect(cx - 19, cy - 19, 38, 38, 19, WHITE);
  fbRoundRect(cx - 17, cy - 17, 34, 34, 17, COLOR_BADGE_RED);
  fbTextCentered(FONT_BADGE, buf, cx, cy - FONT_BADGE.h / 2, WHITE);
}

static const uint16_t *serviceIcon(int i) {
  static const uint16_t *icons[] = {ICON_ORDER, ICON_BILL, ICON_WAITER, ICON_WATER};
  return icons[i];
}

inline void HSCRFaceLCD::renderBoot() {
  fbBackground();
  // The logo's dark wordmark is unreadable straight on the pattern, so it
  // sits on a white card.
  const int pad = 18;
  int cardW = BOOT_LOGO_W + 2 * pad, cardH = BOOT_LOGO_H + 2 * pad;
  int cardX = (LCD_WIDTH - cardW) / 2, cardY = (LCD_HEIGHT - cardH) / 2;
  fbRoundRect(cardX, cardY, cardW, cardH, 24, WHITE);
  fbImage(cardX + pad, cardY + pad, BOOT_LOGO, BOOT_LOGO_W, BOOT_LOGO_H);
}

inline void HSCRFaceLCD::renderClock() {
  fbTextCentered(FONT_CLOCK, lastClockText_.c_str(), LCD_WIDTH / 2, 20, WHITE);
}

inline void HSCRFaceLCD::renderHome() {
  fbBackground();
  renderClock();
  fbTextCentered(FONT_TITLE, "Table Service", LCD_WIDTH / 2, 104, WHITE);

  for (int i = 0; i < SERVICES_COUNT; i++) {
    IconRect r = iconRect(i);
    fbRoundRect(r.x, r.y, r.w, r.h, 22, SERVICES[i].color);
    // Icon art is pre-composited onto the tile colour, so it drops straight in.
    fbImage(r.x + (r.w - ICON_ORDER_W) / 2, r.y + 8, serviceIcon(i), ICON_ORDER_W, ICON_ORDER_H);
    fbTextCentered(FONT_LABEL, SERVICES[i].label, r.x + r.w / 2, r.y + r.h - FONT_LABEL.h - 4, WHITE);
    int count = pendingCountFor(i);
    if (count > 0 && badgeFlashOn_) fbBadge(r.x + r.w - 20, r.y + 20, count);
  }
}

inline void HSCRFaceLCD::renderDetail() {
  const PendingRequest &req = pending_[detailIndex_];
  int svc = serviceIndex(req.service);
  const char *verb = svc >= 0 ? SERVICES[svc].detail : "Service needed";

  fbBackground();
  renderClock();

  // Dark translucent panel keeps the message readable on the busy pattern.
  fbRoundRect(24, 150, LCD_WIDTH - 48, 180, 28, BLACK, 170);
  fbTextCentered(FONT_MSG, verb, LCD_WIDTH / 2, 182, WHITE);
  fbTextCentered(FONT_MSG, ("at Table " + req.table).c_str(), LCD_WIDTH / 2, 244, WHITE);

  IconRect btn = attendedButtonRect();
  fbRoundRect(btn.x, btn.y, btn.w, btn.h, btn.h / 2, COLOR_TILE_ORDER);
  fbTextCentered(FONT_TITLE, "Attended", btn.x + btn.w / 2, btn.y + (btn.h - FONT_TITLE.h) / 2, WHITE);

  int others = pendingCount_ - 1;
  if (others > 0) fbBadge(btn.x + btn.w - 14, btn.y + 6, others);
}

inline void HSCRFaceLCD::render() {
  if (idle_ || !fb) return;
  if (state_ == FACE_DETAIL) renderDetail(); else renderHome();
  gfx->draw16bitRGBBitmap(0, 0, fb, LCD_WIDTH, LCD_HEIGHT);
}

// Re-renders only when the displayed minute changes.
inline void HSCRFaceLCD::updateClock() {
  RTC_DateTime now = rtc.getDateTime();
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", now.getHour(), now.getMinute());
  if (lastClockText_ == buf) return;
  lastClockText_ = buf;
  render();
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
    badgeFlashOn_ = true;
    badgeFlashUntil_ = millis() + BUZZ_ON_MS;
    digitalWrite(PIN_MOTOR, HIGH);
    if (state_ == FACE_HOME) render();
    // If staff is viewing a *different* request's detail screen, the new
    // one just waits quietly in the queue — no need to interrupt them.
  });

  // Captive-portal probes: send everything to us so phones auto-pop the
  // "Sign in to network" page after joining. Deliberately the original,
  // plain-redirect behaviour: later "improvements" (serving the page
  // directly, DHCP option 114, an HTTPS sink) stopped it auto-opening.
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
  fb = (uint16_t *)heap_caps_malloc((size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!fb) Serial.println("framebuffer allocation failed!");
  if (fb) {
    renderBoot();
    gfx->draw16bitRGBBitmap(0, 0, fb, LCD_WIDTH, LCD_HEIGHT);
  }
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

  // Power button: handled by the AXP2101 power chip itself rather than in
  // firmware — hold ~1s to switch on, hold ~4s to switch off. Being a
  // hardware cut-off, it works even if this firmware ever hangs.
  if (pmu.init(Wire, IIC_SDA, IIC_SCL)) {
    pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_1S);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    pmu.setLongPressPowerOFF();
    pmu.enableLongPressShutdown();
    pmu.enableALDO3();  // supply rail for the vibration motor
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    delay(100);
    Serial.printf("power: battery %s, %umV, %s, usb %s\n",
                  pmu.isBatteryConnect() ? "connected" : "NOT DETECTED",
                  pmu.getBattVoltage(),
                  pmu.isCharging() ? "charging" : "not charging",
                  pmu.isVbusIn() ? "in" : "out");
  } else {
    Serial.println("AXP2101 PMU not found - power button long-press not configured");
  }

  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);

  // There's no internet (AP-only) to get real time from, so there's no NTP
  // here. isClockIntegrityGuaranteed() reflects the PCF85063's own
  // oscillator-stop flag — true only if the RTC has run continuously since
  // it was last set. On a truly fresh chip (or one that lost backup power)
  // this is false, so fall back to the firmware's own build time; once set,
  // the battery-backed RTC keeps correct time across reflashes/reboots on
  // its own. Staff can nudge it by a few minutes by reflashing if it drifts
  // — there's no on-device "set time" UI yet.
  rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!rtc.isClockIntegrityGuaranteed()) {
    Serial.println("RTC time not trustworthy, setting from build time");
    rtc.setDateTime(RTC_DateTime(__DATE__, __TIME__));
  }

  apSsid_ = String(AP_SSID_PREFIX) + String(TABLE_NUMBER);
  WiFi.mode(WIFI_AP);
  bool ok = strlen(AP_PASSWORD) >= 8
    ? WiFi.softAP(apSsid_.c_str(), AP_PASSWORD, 1, 0, AP_MAX_CLIENTS)
    : WiFi.softAP(apSsid_.c_str(), NULL, 1, 0, AP_MAX_CLIENTS);

  // Captive-portal network setup. Without these the order page never pops up:
  // on this ESP32 core the AP's DHCP server only tells phones to use the
  // watch for DNS when a DNS address is given explicitly. Without one, phones
  // can't even look up their connectivity-check hosts, conclude there's
  // simply "no internet", and never show the sign-in page.
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0), IPAddress(192, 168, 4, 2), apIP);

  // The core's default pool is only 10 addresses held for 2 hours, so the
  // 11th new phone within that window would get no address at all. Use a
  // large pool and short leases so a busy venue never runs out.
  esp_netif_t *apNetif = WiFi.AP.netif();
  esp_netif_dhcps_stop(apNetif);
  dhcps_lease_t lease;
  lease.enable = true;
  lease.start_ip.addr = static_cast<uint32_t>(IPAddress(192, 168, 4, 2));
  lease.end_ip.addr = static_cast<uint32_t>(IPAddress(192, 168, 4, 250));
  esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS, &lease, sizeof(lease));
  uint32_t leaseMinutes = 5;
  esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &leaseMinutes, sizeof(leaseMinutes));
  esp_netif_dhcps_start(apNetif);
  Serial.printf("softAP \"%s\": %s, IP=%s\n",
                apSsid_.c_str(), ok ? "OK" : "FAILED",
                WiFi.softAPIP().toString().c_str());

  dns_.start(53, "*", WiFi.softAPIP());
  setupRoutes();
  server_.begin();

  updateClock();  // sets the time text and renders the home screen
  lastActivity_ = millis();
}

inline void HSCRFaceLCD::loop() {
  dns_.processNextRequest();
  server_.handleClient();

  if (digitalRead(PIN_BOOT) == LOW) wake();
  alertPump();

  if (idle_) return;

  // Never go dark while a request is still waiting — the blinking badge is
  // the alert.
  if (pendingCount_ == 0 && millis() - lastActivity_ > IDLE_TIMEOUT_MS) {
    enterIdle();
    return;
  }

  updateClock();

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
            render();
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
        render();
      }
    }
  }
  touchWasDown_ = touchDown;
}

// While any request is waiting: pulse the vibration motor and blink the
// home-screen badges together (on BUZZ_ON_MS, off BUZZ_OFF_MS). Stops the
// moment the last request is marked Attended.
inline void HSCRFaceLCD::alertPump() {
  if (pendingCount_ == 0) {
    digitalWrite(PIN_MOTOR, LOW);
    if (!badgeFlashOn_) badgeFlashOn_ = true;
    return;
  }
  unsigned long now = millis();
  if (now < badgeFlashUntil_) return;
  badgeFlashOn_ = !badgeFlashOn_;
  digitalWrite(PIN_MOTOR, badgeFlashOn_ ? HIGH : LOW);
  badgeFlashUntil_ = now + (badgeFlashOn_ ? BUZZ_ON_MS : BUZZ_OFF_MS);
  if (state_ == FACE_HOME) render();
}

#endif
