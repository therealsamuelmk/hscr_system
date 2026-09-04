#ifndef SETTINGS_H
#define SETTINGS_H

// ---------------------------------------------------------------
// Hardware revision: 2 = ESP32-PICO-D4 (v1 / v1.5 / v2, micro-USB)
//                     3 = ESP32-S3 (v3, USB-C)
// Must match HSCR_PinTest.ino's WATCHY_HW for whatever board you flash.
// ---------------------------------------------------------------
#define WATCHY_HW 2

// ---------------------------------------------------------------
// This watch's table number. The ONE thing that differs between the
// two physical watches — flash Watch A with 1, Watch B with 2.
// Must be one of the tables listed in ACTIVE_TABLES in
// backend/api/_bootstrap.php on the server, or the API will reject it.
// ---------------------------------------------------------------
#define TABLE_NUMBER 1

// ---------------------------------------------------------------
// WiFi. The watch stays associated continuously so it can poll
// the API every POLL_INTERVAL_MS — pick a network that reaches the
// table area reliably.
// ---------------------------------------------------------------
#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// ---------------------------------------------------------------
// HSCR API (see backend/api/ in the project, deployed to hscr.online).
// TLS certificate validation is skipped (setInsecure()) to match this
// phase's "no auth yet" API — fine for a closed venue WiFi, revisit if
// this ever leaves that trust boundary.
// ---------------------------------------------------------------
#define API_BASE_URL      "https://hscr.online/api"
#define POLL_INTERVAL_MS  12000UL   // 10-15s, per the agreed trade-off
#define HTTP_TIMEOUT_MS   6000UL

// ---------------------------------------------------------------
// Time: Lusaka / CAT is UTC+2. Used only for the idle clock face;
// synced over NTP since WiFi is connected continuously anyway.
// ---------------------------------------------------------------
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_SEC  7200
#define DST_OFFSET_SEC  0

#endif
