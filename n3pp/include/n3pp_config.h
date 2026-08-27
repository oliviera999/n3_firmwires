#pragma once

#include "credentials.h"
#include "n3_defaults.h"

#ifndef API_SIG_SECRET
#define API_SIG_SECRET ""
#endif

#define FIRMWARE_VERSION "4.71"

// Schema serveur : HTTPS par defaut (Vague 1 audit 2026-07).
// Rollback HTTP : definir USE_HTTP_ENDPOINTS au build (-DUSE_HTTP_ENDPOINTS).
// Transport TLS : USE_HTTPS_ENDPOINTS (aligne WiFiClientSecure sur l'URL https://).
#if defined(USE_HTTP_ENDPOINTS)
#define N3PP_SERVER_SCHEME "http://"
#else
#define N3PP_SERVER_SCHEME "https://"
#endif

//définitions des pins
#if defined(PINMAP_UNIVERSAL)
// Carte porteuse n3-universal (hardware/n3-universal) : nets partages entre les
// 3 firmwares — source de verite pinmap_universel_propose.json. GPIO13 (gate) et
// I2C (21/22) inchanges ; la pompe passe sur le canal relais K1 de la carte.
#define RELAIS 13      // net GATE (rail capteurs commute)
#define POMPE 16       // net K1 (canal relais 1, partage POMPE_AQUA ffp5cs)
#define pontdiv 39     // net ADC_VBAT (diviseur commute)
#define humidite1 32   // net ADC_A (partage LUMINOSITEa msp)
#define humidite2 33   // net ADC_B
#define humidite3 34   // net ADC_C
#define humidite4 35   // net ADC_D
#define LUMINOSITE 36  // net ADC_E (partage HumiditeSol msp / LDR ffp5cs)
#define DHTPIN 15      // net DHT_INT (commun aux 3 firmwares)
#define DHTTYPE DHT11
#else
#define RELAIS 13
#define POMPE 12
#define pontdiv 36
#define humidite1 33
#define humidite2 32
#define humidite3 35
#define humidite4 34
#define LUMINOSITE 39
#define DHTPIN 18      // Pin numérique connectée au DHT (température et humidité air)
#define DHTTYPE DHT11  // Type de capteur DHT (DHT11)
#endif

#define uS_TO_S_FACTOR N3_US_TO_S_FACTOR
#define TIME_TO_SLEEP FreqWakeUp

#define emailSubject "Information N3PP"

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

//#define pontdiv 36  // Pin pour la lecture du diviseur de tension
// R1/R2 non definis ici pour eviter conflit avec struct N3BatteryConfig (n3_battery.h) ; n3pp_sensors utilise N3_BATTERY_R1/R2
#define NUM_SAMPLES N3_BATTERY_NUM_SAMPLES

#define SCREEN_WIDTH N3_OLED_WIDTH
#define SCREEN_HEIGHT N3_OLED_HEIGHT
