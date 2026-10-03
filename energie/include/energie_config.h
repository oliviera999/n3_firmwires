#pragma once
/**
 * Banc d'essai energie — configuration (ESP32-S3 + 3 x INA226).
 *
 * Voies : panneau solaire (production), batterie plomb/AGM 12 V (charge /
 * decharge), consommation des peripheriques. Shunt, courant max, sens et gain
 * sont reglables ici ET a chaud par la console serie (persistes en NVS).
 *
 * Secrets : credentials.h a la racine du depot (meme fichier que n3pp/msp) :
 * WIFI_SSIDx/WIFI_PASSx, API_KEY, API_SIG_SECRET, SMTP_*.
 */

#include "credentials.h"
#include "n3_defaults.h"

#ifndef API_SIG_SECRET
#define API_SIG_SECRET ""
#endif

// -----------------------------------------------------------------------------
// Version (source unique — cf. firmwares.manifest.json, versionSource)
// -----------------------------------------------------------------------------
#define FIRMWARE_VERSION "0.1"
#define ENERGIE_SENSOR_NAME "energie"

// -----------------------------------------------------------------------------
// Broches (ESP32-S3-DevKitC-1 N16R8 ; carte n3-universal, PINMAP_UNIVERSAL)
// GPIO33-37 interdits (PSRAM octale). Ne jamais utiliser LED_BUILTIN /
// neopixelWrite : GPIO48 = relais K5 sur n3-universal.
// -----------------------------------------------------------------------------
#define ENERGIE_PIN_SDA 8
#define ENERGIE_PIN_SCL 9
#define ENERGIE_I2C_HZ 100000UL  // 100 kHz : OLED + DS3231 + 3 INA, cables courts

// GATE du rail +3V3_SW (n3-universal, Q8 via R34) : coupe au boot si JP1 != 1-2.
// Mis a HAUT au demarrage pour alimenter les prises I2C (OLED/INA). -1 = inutilise.
#define ENERGIE_PIN_RAIL_GATE 3
#define ENERGIE_RAIL_SETTLE_MS 100

// Relais n3-universal (drivers actifs HAUT, pull-down 10 k) : forces a BAS
// (OFF) au boot pour qu'un banc monte sur la carte ne commute rien.
// K1..K4 = 15..18, K5 = 48 (= LED RGB du DevKit), K6 = 45.
#define ENERGIE_SAFE_LOW_PINS {15, 16, 17, 18, 48, 45}

// Broche ALERT des INA (non routee sur les PCB : polling 1 Hz). -1 = inutilisee.
#define ENERGIE_PIN_INA_ALERT -1

// Pont diviseur batterie de la carte n3-universal (ADC_VBAT = GPIO7, 100k/27k),
// lu en option pour comparaison avec l'INA batterie. -1 = desactive (DevKit nu).
#define ENERGIE_PIN_ADC_VBAT -1
#define ENERGIE_VBAT_R1 100000UL
#define ENERGIE_VBAT_R2 27000UL
#define ENERGIE_VBAT_VREF 3.3f

// -----------------------------------------------------------------------------
// Voies INA226 (adresses prevues par la doc n3-universal : 0x40 / 0x41 / 0x44)
// Shunt par defaut = module standard « R100 » (0,1 ohm) -> 0,82 A max !
// Module reel (panneau 50-80 Wc, batterie, conso) : shunt externe 5-10 mohm.
// -----------------------------------------------------------------------------
#define ENERGIE_CH_COUNT 3
#define ENERGIE_CH_PANNEAU 0
#define ENERGIE_CH_BATTERIE 1
#define ENERGIE_CH_CONSO 2

#define ENERGIE_PANNEAU_ADDR 0x40
#define ENERGIE_PANNEAU_SHUNT_OHM 0.1f
#define ENERGIE_PANNEAU_IMAX_A 0.8f
#define ENERGIE_PANNEAU_INVERT false

#define ENERGIE_BATTERIE_ADDR 0x41
#define ENERGIE_BATTERIE_SHUNT_OHM 0.1f
#define ENERGIE_BATTERIE_IMAX_A 0.8f
// Convention : courant batterie > 0 = CHARGE. Mettre true si le module est
// cable a l'envers (verifier avec la commande « dump » en charge).
#define ENERGIE_BATTERIE_INVERT false

#define ENERGIE_CONSO_ADDR 0x44
#define ENERGIE_CONSO_SHUNT_OHM 0.1f
#define ENERGIE_CONSO_IMAX_A 0.8f
#define ENERGIE_CONSO_INVERT false

// Moyennage materiel : 128 x (1,1 ms bus + 1,1 ms shunt) ~ 282 ms par resultat,
// lisse le hachage d'un regulateur PWM. Reglable par la commande « avg ».
#define ENERGIE_INA_AVG_SAMPLES 128
#define ENERGIE_INA_CONV_US 1100

// -----------------------------------------------------------------------------
// Cadences
// -----------------------------------------------------------------------------
#define ENERGIE_SAMPLE_MS 1000UL          // mesure (1 Hz)
#define ENERGIE_POST_MS 10000UL           // envoi serveur (moyennes + energie de la fenetre)
#define ENERGIE_MAX_GAP_MS 30000UL        // trou de mesure au-dela duquel on n'integre pas
#define ENERGIE_DISPLAY_MS 1000UL
#define ENERGIE_WIFI_RETRY_MS 30000UL     // nouvelle session WiFi apres echec
#define ENERGIE_SOC_SAVE_MS 900000UL      // sauvegarde NVS du SoC (15 min)

// -----------------------------------------------------------------------------
// Batterie plomb/AGM 12 V
// -----------------------------------------------------------------------------
#define ENERGIE_BAT_CAPACITY_AH 12.0f
#define ENERGIE_BAT_CHARGE_EFFICIENCY 0.90f
#define ENERGIE_BAT_REST_CURRENT_A 0.12f   // ~C/100
#define ENERGIE_BAT_REST_SECONDS 1800UL    // repos avant recalage OCV

// Alerte mail batterie basse (n3_mail, SMTP de credentials.h)
#define ENERGIE_ALERT_ENABLED 1
#define ENERGIE_ALERT_LOW_V 11.8f
#define ENERGIE_ALERT_HOLD_S 60UL
#define ENERGIE_ALERT_REARM_V 12.4f
#define ENERGIE_ALERT_COOLDOWN_S 21600UL   // 6 h
#define ENERGIE_ALERT_CHARGING_A 0.05f

// -----------------------------------------------------------------------------
// Serveur (famille « energie » de n3_serveur)
// -----------------------------------------------------------------------------
#ifdef USE_HTTPS_ENDPOINTS
#define ENERGIE_SERVER_SCHEME "https://"
#else
#define ENERGIE_SERVER_SCHEME "http://"
#endif

#ifdef TEST_MODE
#define ENERGIE_POST_URL ENERGIE_SERVER_SCHEME "iot.olution.info/energie-test/post-data"
#else
#define ENERGIE_POST_URL ENERGIE_SERVER_SCHEME "iot.olution.info/energie/post-data"
#endif

#define ENERGIE_OTA_TITLE "ENERGIE OTA"
#define ENERGIE_OTA_URL_PROD "http://iot.olution.info/ota/energie/metadata.json"
#define ENERGIE_OTA_URL_TEST "http://iot.olution.info/ota/energie-test/metadata.json"

// -----------------------------------------------------------------------------
// OLED SSD1306 (prise J13 n3-universal)
// -----------------------------------------------------------------------------
#define ENERGIE_OLED_ADDR 0x3C
