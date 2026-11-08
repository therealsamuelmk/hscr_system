/*
 * Waveshare_HSCR.ino
 * ----------------------------------------------------------
 * Self-contained, offline HSCR table-service interface for the Waveshare
 * ESP32-S3-Touch-AMOLED-2.06 (QSPI AMOLED + capacitive touch — no e-paper,
 * unlike Watchy_HSCR, and no backlight pin since AMOLED is self-emissive).
 * Same architecture otherwise: the device broadcasts its own WiFi network
 * (see AP_SSID_PREFIX/TABLE_NUMBER in settings.h). A guest joins it, gets a
 * small order page served by the device itself, and submitting it shows the
 * table + requested service on-screen and flashes the alert banner
 * repeatedly until the screen is tapped to mark it delivered (no buzzer or
 * vibration motor on this board).
 *
 * Folder must be named Waveshare_HSCR and contain:
 *   Waveshare_HSCR.ino   (this file)
 *   settings.h            (table number, AP name/password — edit before
 *                          flashing)
 *   HSCR_FaceLCD.h         (display + touch + AP + web server)
 *
 * Board settings (Arduino IDE):
 *   Tools > Board > ESP32 Arduino > "ESP32S3 Dev Module"
 *   USB CDC On Boot: Enabled (this board flashes/logs over its native USB)
 *   Flash Size: 32MB, PSRAM: OPI PSRAM
 *
 * Library dependencies (not in Library Manager — copy from Waveshare's own
 * demo repo, github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06, into
 * ~/Documents/Arduino/libraries/, same as their wiki's install
 * instructions):
 *   GFX_Library_for_Arduino (CO5300 QSPI AMOLED driver)
 *   Arduino_DriveBus (FT3168 touch driver)
 * WiFi / DNSServer / WebServer ship with the ESP32 core.
 *
 * Stays awake continuously (no sleep) to keep serving the AP and web
 * requests, same trade-off as Watchy_HSCR.
 */

#include "settings.h"
#include "HSCR_FaceLCD.h"

HSCRFaceLCD face;

void setup() {
  face.begin();
}

void loop() {
  face.loop();
  delay(20);
}
