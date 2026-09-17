/*
 * Watchy_HSCR.ino
 * ----------------------------------------------------------
 * Self-contained, offline HSCR table-service watch. No venue WiFi or
 * internet needed: the watch broadcasts its own WiFi network (see
 * AP_SSID_PREFIX/TABLE_NUMBER in settings.h). A guest joins it, gets
 * a small order page served by the watch itself, and submitting it
 * shows the table + requested service on-screen and buzzes repeatedly
 * until staff presses MENU to mark it delivered.
 *
 * Folder must be named Watchy_HSCR and contain:
 *   Watchy_HSCR.ino   (this file)
 *   settings.h        (table number, AP name/password — edit before flashing)
 *   HSCR_Face.h        (display + AP + web server)
 *
 * Board settings (Arduino IDE):
 *   Watchy v1 / v1.5 / v2  ->  Tools > Board > ESP32 Arduino > "ESP32 Dev Module"
 *                              (set WATCHY_HW 2 in settings.h)
 *   Watchy v3 (ESP32-S3)   ->  Tools > Board > ESP32 Arduino > "ESP32S3 Dev Module"
 *                              Flash Size: 8MB, Partition Scheme: 8M with spiffs,
 *                              USB CDC On Boot: Enabled (set WATCHY_HW 3 in settings.h)
 *
 * Library Manager dependencies: GxEPD2. WiFi / DNSServer / WebServer ship
 * with the ESP32 core — nothing else to install.
 *
 * Stays awake continuously (no deep sleep) to keep serving the AP and web
 * requests — battery life is shorter than a stock Watchy, same trade-off
 * as before.
 */

#include "settings.h"
#include "HSCR_Face.h"

HSCRFace face;

void setup() {
  face.begin();
}

void loop() {
  face.loop();
  delay(50);
}
