#pragma once

/**
 * Constantes par defaut partagees entre les firmwares legacy n3 IoT.
 * Chaque firmware peut surcharger ces valeurs dans son propre config.h.
 */

#ifndef N3_NTP_SERVER
#define N3_NTP_SERVER "pool.ntp.org"
#endif

#ifndef N3_GMT_OFFSET
#define N3_GMT_OFFSET 3600
#endif

#ifndef N3_DAYLIGHT_OFFSET
#define N3_DAYLIGHT_OFFSET 3600
#endif

#ifndef N3_WIFI_TIMEOUT_MS
#define N3_WIFI_TIMEOUT_MS 5000
#endif

#ifndef N3_DATA_INTERVAL_MS
#define N3_DATA_INTERVAL_MS 120000
#endif

// Timeout HTTP par defaut (POST/GET) pour les libs partagees n3_data, n3_http, n3_ota.
// Aligne sur la regle "5 s max" (conventions-firmwares.mdc).
#ifndef N3_HTTP_TIMEOUT_MS
#define N3_HTTP_TIMEOUT_MS 5000
#endif

// Defaut deep sleep entre deux cycles (secondes). 300 s = 5 min, alignement prod
// (cf. doc legacy : reveil timer 3000 s reduit a 300 s pour rester reactif).
#ifndef N3_DEFAULT_FREQ_WAKE_UP_S
#define N3_DEFAULT_FREQ_WAKE_UP_S 300
#endif

#ifndef N3_WAKEUP_GPIO
#define N3_WAKEUP_GPIO GPIO_NUM_4
#endif

#ifndef N3_BATTERY_R1
#define N3_BATTERY_R1 2200.0f
#endif

#ifndef N3_BATTERY_R2
#define N3_BATTERY_R2 2180.0f
#endif

#ifndef N3_BATTERY_VREF
#define N3_BATTERY_VREF 3.33f
#endif

#ifndef N3_BATTERY_NUM_SAMPLES
#define N3_BATTERY_NUM_SAMPLES 10
#endif

#ifndef N3_PONTDIV_PIN
#define N3_PONTDIV_PIN 36
#endif

// OLED SSD1306 (n3pp, msp)
#ifndef N3_OLED_WIDTH
#define N3_OLED_WIDTH 128
#endif
#ifndef N3_OLED_HEIGHT
#define N3_OLED_HEIGHT 64
#endif
