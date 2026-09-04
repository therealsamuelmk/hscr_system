/*
 * HSCR_PinTest.ino
 * ----------------------------------------------------------
 * Bare-metal test: talks to the Watchy e-paper panel through raw GPIO,
 * with NO Watchy library. Use this to confirm your pin map / board
 * revision before flashing the real watchface.
 *
 * Needs only the GxEPD2 library (Library Manager > "GxEPD2").
 *
 * Set WATCHY_HW below to 2 (ESP32-PICO-D4, micro-USB) or 3 (ESP32-S3, USB-C).
 */

#define WATCHY_HW 2

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#if WATCHY_HW == 3
  // ---- Watchy v3 (ESP32-S3) ----
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
  // ---- Watchy v1 / v1.5 / v2 (ESP32-PICO-D4) ----
  #define PIN_CS     5
  #define PIN_DC    10
  #define PIN_RES    9
  #define PIN_BUSY  19
  #define PIN_SCK   18   // hardware VSPI
  #define PIN_MOSI  23   // hardware VSPI
  #define PIN_MISO  -1   // display is write-only
  #define BTN_MENU  26
  #define BTN_BACK  25
  #define BTN_UP    35   // 32 on v1 and v1.5
  #define BTN_DOWN   4
  #define PIN_VIB   13
#endif

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(PIN_CS, PIN_DC, PIN_RES, PIN_BUSY));

void setup() {
#if WATCHY_HW == 3
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
#endif
  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_BLACK);
    display.fillRect(0, 0, 200, 28, GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(14, 20);
    display.print("HSCR SYSTEM");

    display.setTextColor(GxEPD_WHITE);
    display.setCursor(14, 70);
    display.print("PIN TEST");
    display.setCursor(14, 100);
    display.print("HW REV ");
    display.print(WATCHY_HW);
    display.setCursor(14, 130);
    display.print("> SCREEN OK");
  } while (display.nextPage());

  display.hibernate();

  // Quick buzz so you know the sketch actually ran.
  pinMode(PIN_VIB, OUTPUT);
  digitalWrite(PIN_VIB, HIGH);
  delay(150);
  digitalWrite(PIN_VIB, LOW);
}

void loop() {}
