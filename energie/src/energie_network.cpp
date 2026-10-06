#include "energie_network.h"

#include <ESP32Time.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "energie_config.h"
#include "energie_display.h"
#include "n3_data.h"
#include "n3_mail.h"
#include "n3_ota.h"
#include "n3_ota_periodic.h"
#include "n3_ota_ui.h"
#include "n3_time.h"
#include "n3_wifi.h"

namespace {

const N3WifiNetwork kNetworks[] = {
    {WIFI_SSID1, WIFI_PASS1},
    {WIFI_SSID2, WIFI_PASS2},
    {WIFI_SSID3, WIFI_PASS3},
};

N3WifiConfig s_wifiConfig = {};
N3WifiSession s_wifiSession;
bool s_sessionActive = false;
uint32_t s_nextWifiAttemptMs = 0;
String s_ssid;

ESP32Time s_rtc;
EnergieNetStatus s_status = {};

// Energie des fenetres dont l'envoi a echoue : reportee sur l'envoi suivant
// (les deltas Wh restent sommables cote serveur, rien n'est perdu).
double s_carryWh[ENERGIE_CH_COUNT] = {};

// OTA periodique (2 h) : compteur initialise a l'intervalle -> verification des
// la premiere connexion. Pas de deep sleep ici : simple statique.
uint32_t s_otaElapsedSeconds = OtaPeriodic::kDefaultIntervalSeconds;
N3OtaUiContext s_otaCtx;
uint32_t s_lastOtaTickMs = 0;

bool s_mailPending = false;
String s_mailSubject;
String s_mailBody;

unsigned long hmacEpochSeconds() {
  return n3TimeHasPlausibleEpoch() ? static_cast<unsigned long>(time(nullptr)) : 0UL;
}

String fmt(float v, unsigned int decimals) {
  if (isnan(v) || isinf(v)) return String("");  // champ vide -> NULL cote serveur
  return String(v, decimals);
}

String fmtD(double v, unsigned int decimals) { return fmt(static_cast<float>(v), decimals); }

void onWifiConnected() {
  s_status.wifiConnected = true;
  Serial.printf("[WIFI] connecte a %s (%s, RSSI %d dBm)\n", s_ssid.c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  // HMAC : le serveur exige un horodatage plausible (fenetre SIG_VALID_WINDOW).
  if (!s_status.timeSynced) {
    s_status.timeSynced = n3TimeSyncNtp(s_rtc, N3_GMT_OFFSET, N3_DAYLIGHT_OFFSET, N3_NTP_SERVER, 5000U);
    Serial.printf("[NTP] %s\n", s_status.timeSynced ? "synchronise" : "echec (POST sans HMAC, repli api_key)");
  }
}

void startWifiSession() {
  n3WifiSessionBegin(s_wifiSession, s_wifiConfig);
  s_sessionActive = true;
}

}  // namespace

void networkBegin() {
  s_wifiConfig.networks = kNetworks;
  s_wifiConfig.networkCount = sizeof(kNetworks) / sizeof(kNetworks[0]);
  s_wifiConfig.timeoutMs = N3_WIFI_TIMEOUT_MS;
  s_wifiConfig.delayBetweenMs = 250;
  s_wifiConfig.preScanDelayMs = 100;
  s_wifiConfig.scanMax = 10;
  s_wifiConfig.onFailure = []() { Serial.println("[WIFI][WARN] aucun reseau configure joignable"); };

  const N3OtaUiConfig otaCfg = {
      ENERGIE_OTA_TITLE,
      ENERGIE_OTA_URL_PROD,
      ENERGIE_OTA_URL_TEST,
#ifdef TEST_MODE
      true,
#else
      false,
#endif
      FIRMWARE_VERSION,
      &g_display,
      &g_displayOk,
      OtaPeriodic::kDefaultIntervalSeconds,
      &s_otaElapsedSeconds,
  };
  n3OtaUiInit(s_otaCtx, otaCfg);

  Serial.printf("[NET] POST %s (toutes les %lu s)\n", ENERGIE_POST_URL,
                static_cast<unsigned long>(ENERGIE_POST_MS / 1000));
  startWifiSession();
}

void networkLoop(uint32_t nowMs) {
  // --- WiFi (session non bloquante : la mesure 1 Hz continue pendant la connexion)
  if (s_sessionActive) {
    const N3WifiPollResult r = n3WifiSessionPoll(s_wifiSession, 30, &s_ssid);
    if (r == N3WifiPollResult::Connected) {
      s_sessionActive = false;
      onWifiConnected();
    } else if (r == N3WifiPollResult::Failed) {
      s_sessionActive = false;
      s_nextWifiAttemptMs = nowMs + ENERGIE_WIFI_RETRY_MS;
      Serial.printf("[WIFI][WARN] echec (%s), nouvel essai dans %lu s\n",
                    n3WifiSessionFailureReasonName(s_wifiSession.failureReason),
                    static_cast<unsigned long>(ENERGIE_WIFI_RETRY_MS / 1000));
    }
  } else if (WiFi.status() != WL_CONNECTED) {
    if (s_status.wifiConnected) {
      s_status.wifiConnected = false;
      Serial.println("[WIFI][WARN] connexion perdue");
      s_nextWifiAttemptMs = nowMs;
    }
    if (static_cast<int32_t>(nowMs - s_nextWifiAttemptMs) >= 0) startWifiSession();
  }
  s_status.rssi = s_status.wifiConnected ? WiFi.RSSI() : 0;

  // --- OTA periodique (2 h), seulement en ligne et a l'heure
  if (nowMs - s_lastOtaTickMs >= 1000) {
    const uint32_t elapsedS = (nowMs - s_lastOtaTickMs) / 1000;
    s_lastOtaTickMs += elapsedS * 1000;
    n3OtaUiAccumulateElapsed(s_otaCtx, static_cast<int>(elapsedS));
  }
  if (s_status.wifiConnected && OtaPeriodic::due(s_otaElapsedSeconds, OtaPeriodic::kDefaultIntervalSeconds)) {
    n3OtaUiMaybePeriodicCheck(s_otaCtx, "periodique");
  }

  // --- Mail en attente (alerte levee hors connexion)
  if (s_mailPending && s_status.wifiConnected) {
    if (networkSendMail(s_mailSubject.c_str(), s_mailBody.c_str())) s_mailPending = false;
  }
}

int networkPost(const EnergieWindow& w) {
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) s_carryWh[i] += w.ch[i].energyWh;
  if (!s_status.wifiConnected) {
    s_status.lastPostCode = -1;
    ++s_status.postFail;
    return -1;
  }

  const N3Power::WindowSnapshot& pv = w.ch[ENERGIE_CH_PANNEAU];
  const N3Power::WindowSnapshot& bat = w.ch[ENERGIE_CH_BATTERIE];
  const N3Power::WindowSnapshot& co = w.ch[ENERGIE_CH_CONSO];
  const bool pvOk = w.valid[ENERGIE_CH_PANNEAU];
  const bool batOk = w.valid[ENERGIE_CH_BATTERIE];
  const bool coOk = w.valid[ENERGIE_CH_CONSO];

  const N3DataField fields[] = {
      {"api_key", API_KEY},
      {"sensor", ENERGIE_SENSOR_NAME},
      {"version", FIRMWARE_VERSION},
      {"PanneauV", pvOk ? fmt(pv.avgV, 3) : String("")},
      {"PanneauI", pvOk ? fmt(pv.avgI, 4) : String("")},
      {"PanneauP", pvOk ? fmt(pv.avgP, 3) : String("")},
      {"PanneauImax", pvOk ? fmt(pv.maxI, 4) : String("")},
      {"BatterieV", batOk ? fmt(bat.avgV, 3) : String("")},
      {"BatterieVmin", batOk ? fmt(bat.minV, 3) : String("")},
      {"BatterieI", batOk ? fmt(bat.avgI, 4) : String("")},
      {"BatterieImin", batOk ? fmt(bat.minI, 4) : String("")},
      {"BatterieImax", batOk ? fmt(bat.maxI, 4) : String("")},
      {"BatterieP", batOk ? fmt(bat.avgP, 3) : String("")},
      {"BatterieVadc", fmt(w.batterieVadc, 3)},
      {"ConsoV", coOk ? fmt(co.avgV, 3) : String("")},
      {"ConsoI", coOk ? fmt(co.avgI, 4) : String("")},
      {"ConsoP", coOk ? fmt(co.avgP, 3) : String("")},
      {"ConsoImax", coOk ? fmt(co.maxI, 4) : String("")},
      {"EnergiePanneauWh", pvOk ? fmtD(s_carryWh[ENERGIE_CH_PANNEAU], 5) : String("")},
      {"EnergieConsoWh", coOk ? fmtD(s_carryWh[ENERGIE_CH_CONSO], 5) : String("")},
      {"BatterieAh", batOk ? fmtD(w.batterieAh, 4) : String("")},
      {"BatterieSoc", fmt(w.batterieSoc, 1)},
      {"InaStatus", String(w.inaStatus)},
      {"I2cErreurs", String(static_cast<unsigned long>(w.i2cErrors))},
      {"Rssi", String(s_status.rssi)},
      {"Uptime", String(static_cast<unsigned long>(millis() / 1000))},
      {"FreeHeap", String(static_cast<unsigned long>(ESP.getFreeHeap()))},
      {"BootCount", String(static_cast<unsigned long>(sensorsBootCount()))},
  };

  N3PostConfig cfg = {};
  cfg.url = ENERGIE_POST_URL;
  cfg.apiKey = API_KEY;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  cfg.sigSecret = (API_SIG_SECRET[0] != '\0') ? API_SIG_SECRET : nullptr;
  cfg.currentEpochSeconds = hmacEpochSeconds();
  const int code = n3DataPost(cfg);

  s_status.lastPostCode = code;
  if (code >= 200 && code < 300) {
    ++s_status.postOk;
    // Seules les voies effectivement envoyees soldent leur report.
    for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
      if (w.valid[i]) s_carryWh[i] = 0.0;
    }
  } else {
    ++s_status.postFail;
  }
  return code;
}

bool networkSendMail(const char* subject, const char* body) {
  if (!s_status.wifiConnected) {
    s_mailPending = true;
    s_mailSubject = subject;
    s_mailBody = body;
    Serial.println("[MAIL] hors ligne : envoi differe");
    return false;
  }
  const N3MailSmtpConfig smtp = {
      SMTP_HOST_ADDR, static_cast<uint16_t>(SMTP_PORT_NUM), SMTP_EMAIL, SMTP_PASSWORD,
      "Banc energie", "Admin", SMTP_DEST,
  };
  String err;
  const bool ok = n3MailSendText(smtp, subject, body, &err);
  Serial.printf("[MAIL] %s%s%s\n", ok ? "envoye" : "echec", ok ? "" : " : ", ok ? "" : err.c_str());
  return ok;
}

void networkOtaCheckNow() {
  if (!s_status.wifiConnected) {
    Serial.println("[OTA] hors ligne");
    return;
  }
  n3OtaUiCheckNow(s_otaCtx);
}

const EnergieNetStatus& networkStatus() { return s_status; }
