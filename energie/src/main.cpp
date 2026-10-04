/**
 * Banc d'essai energie — ESP32-S3 + 3 x INA226 (panneau solaire, batterie
 * plomb/AGM 12 V, consommation des peripheriques).
 *
 * Objectif : comprendre et valider les INA226 avant leur integration au module
 * d'aquaponie mobile et autonome (futur ffp5cs S3, carte n3-universal).
 *
 * Boucle (sans deep sleep) :
 *   - 1 Hz  : mesure des 3 voies, integration Wh/Ah, SoC, alerte batterie basse,
 *             trame CSV + ecran OLED ;
 *   - 10 s  : POST de la fenetre (moyennes, min/max, energie) vers le serveur
 *             (famille « energie », /energie-test/ pour le banc) ;
 *   - fond  : WiFi non bloquant, NTP, OTA periodique (2 h), console serie.
 *
 * Briques partagees : n3_power (INA226 + energie + batterie), n3_wifi, n3_time,
 * n3_data (POST + HMAC), n3_mail, n3_ota_ui / n3_ota, n3_display, n3_battery.
 */

#include <Arduino.h>

#include "energie_board.h"
#include "energie_config.h"
#include "energie_console.h"
#include "energie_display.h"
#include "energie_network.h"
#include "energie_sensors.h"
#include "n3_ota.h"

namespace {
uint32_t s_lastSampleMs = 0;
uint32_t s_lastPostMs = 0;
uint32_t s_lastDisplayMs = 0;

void sendLowBatteryMail() {
  const EnergieChannel& bat = g_channels[ENERGIE_CH_BATTERIE];
  char body[320];
  snprintf(body, sizeof(body),
           "Batterie basse sur le banc energie.\n"
           "Tension : %.2f V (seuil %.2f V tenu %lu s)\n"
           "Courant : %+.3f A\n"
           "Etat de charge estime : %.0f %%\n"
           "Uptime : %lu s, firmware %s\n",
           bat.last.busV, ENERGIE_ALERT_LOW_V, static_cast<unsigned long>(ENERGIE_ALERT_HOLD_S), bat.last.currentA,
           g_batterySoc.socPercent(), static_cast<unsigned long>(millis() / 1000), FIRMWARE_VERSION);
  Serial.printf("[ALERTE] batterie basse %.2f V\n", bat.last.busV);
  networkSendMail("[ENERGIE][P2] Batterie basse", body);
}

void postWindow() {
  const EnergieWindow w = sensorsTakeWindow();
  const int code = networkPost(w);
  const N3Power::WindowSnapshot& pv = w.ch[ENERGIE_CH_PANNEAU];
  const N3Power::WindowSnapshot& bat = w.ch[ENERGIE_CH_BATTERIE];
  const N3Power::WindowSnapshot& co = w.ch[ENERGIE_CH_CONSO];
  Serial.printf("[POST] %d  PV %.2fW  BAT %.2fV %+.3fA  CONSO %.2fW  SoC %.0f%%  status=0x%03X\n", code, pv.avgP,
                bat.avgV, bat.avgI, co.avgP, w.batterieSoc, w.inaStatus);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  // Valide la partition OTA courante tot (anti-rollback n3_ota).
  n3OtaSyncBootPartition();
  Serial.printf("\n=== Banc energie INA226 — firmware %s%s ===\n", FIRMWARE_VERSION,
#ifdef TEST_MODE
                " (TEST)"
#else
                ""
#endif
  );

  boardBegin();
  displayBegin();
  boardI2cScan();
  sensorsBegin();
  networkBegin();
  consoleBegin();

  const uint32_t now = millis();
  s_lastSampleMs = now;
  s_lastPostMs = now;
  s_lastDisplayMs = now;
}

void loop() {
  const uint32_t now = millis();

  consolePoll();
  networkLoop(now);

  if (now - s_lastSampleMs >= ENERGIE_SAMPLE_MS) {
    // Rattrapage borne : apres un POST/OTA bloquant on repart de maintenant
    // (l'integration couvre le trou tant qu'il reste < ENERGIE_MAX_GAP_MS).
    s_lastSampleMs = (now - s_lastSampleMs >= 2 * ENERGIE_SAMPLE_MS) ? now : s_lastSampleMs + ENERGIE_SAMPLE_MS;
    sensorsSample(now);
    consolePrintCsv(now);
    sensorsMaybeSaveSoc(now);
    if (sensorsConsumeLowBatteryAlert()) sendLowBatteryMail();
  }

  if (now - s_lastPostMs >= ENERGIE_POST_MS || consoleConsumePostRequest()) {
    s_lastPostMs = now;
    postWindow();
  }

  if (now - s_lastDisplayMs >= ENERGIE_DISPLAY_MS) {
    s_lastDisplayMs = now;
    displayUpdate();
  }

  delay(5);
}
