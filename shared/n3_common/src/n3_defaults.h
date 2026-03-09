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
