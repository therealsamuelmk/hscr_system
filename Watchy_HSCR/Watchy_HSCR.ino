/*
 * Watchy_HSCR.ino
 * ----------------------------------------------------------
 * HSCR table-service watch. Polls the HSCR API every 10-15s for this
 * watch's table (TABLE_NUMBER in settings.h); when staff needs to be
 * called it buzzes and shows the table + requested service until the
 * MENU button is pressed to resolve it.
 *
 * Folder must be named Watchy_HSCR and contain:
 *   Watchy_HSCR.ino   (this file)
 *   settings.h        (WiFi, table number, API URL — edit before flashing)
 *   HSCR_Face.h        (display + networking)
 *
 * Board settings (Arduino IDE):
 *   Watchy v1 / v1.5 / v2  ->  Tools > Board > ESP32 Arduino > "ESP32 Dev Module"
 *                              (set WATCHY_HW 2 in settings.h)
 *   Watchy v3 (ESP32-S3)   ->  Tools > Board > ESP32 Arduino > "ESP32S3 Dev Module"
 *                              Flash Size: 8MB, Partition Scheme: 8M with spiffs,
 *                              USB CDC On Boot: Enabled (set WATCHY_HW 3 in settings.h)
 *
 * Library Manager dependencies: GxEPD2, ArduinoJson (v7).
 * WiFi / WiFiClientSecure / HTTPClient ship with the ESP32 core.
 *
 * Unlike the original stock watchface, this build stays awake and
 * connected to WiFi continuously (no deep sleep) so it can poll the
 * API every POLL_INTERVAL_MS — see the trade-off notes at the top of
 * HSCR_Face.h and in settings.h.
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
