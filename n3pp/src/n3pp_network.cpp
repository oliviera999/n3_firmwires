#include "n3pp_network.h"
#include "n3pp_globals.h"
#include <Arduino_JSON.h>
#include "n3_wifi.h"
#include "n3_data.h"
#include "n3_mail.h"
#include "n3_notify.h"
#include "n3_defaults.h"
#include "n3_outputs_json.h"

static const uint16_t OUTPUTS_FETCH_DELAY_MS = 250;

void HeureSansWifi();

// Helpers de parsing (anciennement dupliques dans n3pp_network.cpp et
// msp_network.cpp). Depuis v4.39 : factorise dans shared/n3_common/n3_outputs_json.
using N3Outputs::readIntByKey;
using N3Outputs::tryReadIntByKey;
using N3Outputs::readStringByKey;

void datatobdd() {
  Serial.println("DATATOBDD!!!");
  if (displayOk) { display.drawCircle(5, 5, 5, WHITE); display.display(); }

  N3DataField fields[] = {
    {"api_key",       apiKeyValue},
    {"sensor",        sensorName},
    {"version",       version},
    {"TempAir",       String(temperatureAir)},
    {"Humidite",      String(h)},
    {"Luminosite",    String(photocellReading)},
    {"Humid1",        String(Humid1)},
    {"Humid2",        String(Humid2)},
    {"Humid3",        String(Humid3)},
    {"Humid4",        String(Humid4)},
    {"HumidMoy",      String(HumidMoy)},
    {"PontDiv",       String(PontDiv)},
    {"WakeUp",        String(WakeUp)},
    {"ArrosageManu",  String(ArrosageManu)},
    {"SeuilSec",      String(SeuilSec)},
    {"FreqWakeUp",    String(FreqWakeUp)},
    {"SeuilPontDiv",  String(SeuilPontDiv)},
    {"mail",          inputMessageMailAd},
    {"mailNotif",     enableEmailChecked},
    {"HeureArrosage", String(HeureArrosage)},
    {"resetMode",     String(resetMode)},
    {"etatPompe",     String(etatPompe)},
    {"tempsArrosage", String(tempsArrosageSec)},
    {"bootCount",     String(bootCount)},
  };

  N3PostConfig cfg = {};
  cfg.url = serverNamePostData;
  cfg.apiKey = API_KEY;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  // Auth HMAC FFP3 si API_SIG_SECRET est defini ET RTC sync (epoch > 1577836800 = 2020-01-01).
  cfg.sigSecret = (API_SIG_SECRET[0] != '\0') ? API_SIG_SECRET : nullptr;
  const unsigned long epochNow = (unsigned long)rtc.getEpoch();
  cfg.currentEpochSeconds = (epochNow > 1577836800UL) ? epochNow : 0UL;
  String postPreview =
      "api_key=<masque>&sensor=" + sensorName +
      "&version=" + version +
      "&TempAir=" + String(temperatureAir) +
      "&Humidite=" + String(h) +
      "&Luminosite=" + String(photocellReading) +
      "&Humid1=" + String(Humid1) +
      "&Humid2=" + String(Humid2) +
      "&Humid3=" + String(Humid3) +
      "&Humid4=" + String(Humid4) +
      "&HumidMoy=" + String(HumidMoy) +
      "&PontDiv=" + String(PontDiv) +
      "&WakeUp=" + String(WakeUp) +
      "&ArrosageManu=" + String(ArrosageManu) +
      "&SeuilSec=" + String(SeuilSec) +
      "&FreqWakeUp=" + String(FreqWakeUp) +
      "&SeuilPontDiv=" + String(SeuilPontDiv) +
      "&mail=" + inputMessageMailAd +
      "&mailNotif=" + enableEmailChecked +
      "&HeureArrosage=" + String(HeureArrosage) +
      "&resetMode=" + String(resetMode) +
      "&etatPompe=" + String(etatPompe) +
      "&tempsArrosage=" + String(tempsArrosageSec) +
      "&bootCount=" + String(bootCount);
  Serial.println("[SERVER][POST][PAYLOAD] " + postPreview);
  cfg.onSending = []() {
    if (displayOk) { display.fillCircle(5, 5, 5, WHITE); display.display(); }
  };
  cfg.onResult = [](int code) {
    if (code == 200) {
      Serial.println("Envoi BDD: OK");
    } else {
      Serial.printf("Envoi BDD: erreur HTTP %d\n", code);
    }
  };

  int code = n3DataPost(cfg);
  (void)code;
  delay(500);
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SERVER][HB][SKIP] WiFi non connecte");
    return;
  }

  static uint32_t minHeap = UINT32_MAX;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < minHeap) {
    minHeap = freeHeap;
  }

  const uint32_t uptimeSec = millis() / 1000UL;
  const int rssi = WiFi.RSSI();

  N3DataField fields[] = {
    {"api_key",  apiKeyValue},
    {"sensor",   sensorName},
    {"version",  version},
    {"uptime",   String(uptimeSec)},
    {"free",     String(freeHeap)},
    {"min",      String(minHeap == UINT32_MAX ? freeHeap : minHeap)},
    {"reboots",  String(bootCount)},
    {"rssi",     String(rssi)},
  };

  N3PostConfig cfg = {};
  cfg.url = serverNameHeartbeat;
  cfg.apiKey = API_KEY;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  cfg.sigSecret = (API_SIG_SECRET[0] != '\0') ? API_SIG_SECRET : nullptr;
  const unsigned long epochNow = (unsigned long)rtc.getEpoch();
  cfg.currentEpochSeconds = (epochNow > 1577836800UL) ? epochNow : 0UL;
  cfg.onResult = [](int code) {
    if (code == 200) {
      Serial.println("[SERVER][HB] OK");
    } else {
      Serial.printf("[SERVER][HB] erreur HTTP %d\n", code);
    }
  };

  Serial.printf("[SERVER][HB] Envoi vers %s\n", serverNameHeartbeat);
  (void)n3DataPost(cfg);
  delay(200);
}

// Compteur d'echecs consecutifs pour le polling /outputs_state.
// Sert a tracer une perte prolongee de contact serveur sans bloquer le firmware.
static unsigned int s_outputsGetFailureCount = 0;

void variablestoesp() {
  if (displayOk) { display.drawCircle(115, 5, 5, WHITE); display.display(); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SERVER][GET][SKIP] WiFi non connecte, config distante non rafraichie");
    return;
  }

  Serial.printf("[SERVER][GET] Lecture config distante depuis %s\n", serverNameOutput);
  // X-Api-Key envoye aussi sur le GET d'etat : sans effet sur un serveur ancien
  // (en-tete ignore), mais permet a un serveur a jour d'exiger la cle sur ce GET
  // (flag FIRMWARE_STATE_REQUIRE_KEY) au lieu de le laisser totalement public.
  outputsState = n3DataGet(serverNameOutput, &httpResponseCode, API_KEY);
  delay(OUTPUTS_FETCH_DELAY_MS);
  Serial.printf("[SERVER][GET] HTTP=%u\n", httpResponseCode);
  Serial.println("[SERVER][GET][BODY] " + outputsState);

  // Exiger explicitement HTTP 200 : un 301/302/4xx/5xx ne doit pas etre traite
  // comme une reponse valide (avant v4.39, tout code > 0 etait accepte).
  if (httpResponseCode != 200) {
    ++s_outputsGetFailureCount;
    Serial.printf("[SERVER][GET][OFFLINE] Echec consecutif #%u (HTTP=%u), config locale conservee\n",
                  s_outputsGetFailureCount, httpResponseCode);
    return;
  }

  JSONVar myObject = JSON.parse(outputsState);
  if (JSON.typeof(myObject) == "undefined") {
    ++s_outputsGetFailureCount;
    Serial.printf("[SERVER][GET][OFFLINE] JSON invalide (#%u), config locale conservee\n",
                  s_outputsGetFailureCount);
    return;
  }
  if (s_outputsGetFailureCount > 0) {
    Serial.printf("[SERVER][GET] Reprise apres %u echec(s)\n", s_outputsGetFailureCount);
    s_outputsGetFailureCount = 0;
  }
  Serial.print("GPIO bdd : ");
  Serial.println(myObject);

  String pumpGpioKey = String(POMPE);
  int pumpState = readIntByKey(myObject, pumpGpioKey.c_str(), 0);
  Serial.print("variable Pompe est ");
  Serial.println(pumpState);
  pinMode(POMPE, OUTPUT);
  digitalWrite(POMPE, pumpState);

  String relaisGpioKey = String(RELAIS);
  ArrosageManu = readIntByKey(myObject, relaisGpioKey.c_str(), ArrosageManu);
  int parsedResetMode = resetMode ? 1 : 0;
  int parsedWakeUp = WakeUp ? 1 : 0;
  int parsedFreqWakeUp = FreqWakeUp;
  bool hasResetMode = tryReadIntByKey(myObject, "110", &parsedResetMode);
  bool hasWakeUp = tryReadIntByKey(myObject, "106", &parsedWakeUp);
  bool hasFreqWakeUp = tryReadIntByKey(myObject, "107", &parsedFreqWakeUp);
  String raw110 = readStringByKey(myObject, "110", "<absent>");
  String raw106 = readStringByKey(myObject, "106", "<absent>");
  String raw107 = readStringByKey(myObject, "107", "<absent>");

  if (!hasResetMode) {
    Serial.println(String("[SERVER][GET][WARN] Cle 110 absente/invalide (raw=") + raw110 +
                   "), conservation=" + String(resetMode ? 1 : 0));
  } else {
    resetMode = (parsedResetMode != 0);
  }

  if (!hasWakeUp) {
    Serial.println(String("[SERVER][GET][WARN] Cle 106 absente/invalide (raw=") + raw106 +
                   "), conservation=" + String(WakeUp ? 1 : 0));
  } else {
    WakeUp = (parsedWakeUp != 0);
  }

  if (!hasFreqWakeUp) {
    Serial.println(String("[SERVER][GET][WARN] Cle 107 absente/invalide (raw=") + raw107 +
                   "), conservation=" + String(FreqWakeUp));
  } else if (parsedFreqWakeUp < 1 || parsedFreqWakeUp > 86400) {
    Serial.println(String("[SERVER][GET][WARN] Cle 107 hors plage (raw=") + raw107 +
                   " parsed=" + String(parsedFreqWakeUp) + "), conservation=" + String(FreqWakeUp));
  } else {
    FreqWakeUp = parsedFreqWakeUp;
  }
  inputMessageMailAd = readStringByKey(myObject, "100", inputMessageMailAd);
  enableEmailChecked = readStringByKey(myObject, "101", enableEmailChecked);
  // SeuilSec est compare a HumidMoy (moyenne ADC brute 0..4095) : on borne la
  // valeur serveur dans cette plage pour eviter un seuil inatteignable (ex. 5000
  // -> sol toujours "sec"). Hors plage -> valeur precedente conservee.
  {
    int parsedSeuilSec = SeuilSec;
    if (tryReadIntByKey(myObject, "102", &parsedSeuilSec)) {
      if (parsedSeuilSec < 0) parsedSeuilSec = 0;
      if (parsedSeuilSec > 4095) parsedSeuilSec = 4095;
      SeuilSec = parsedSeuilSec;
    }
  }
  SeuilPontDiv = readIntByKey(myObject, "103", SeuilPontDiv);
  HeureArrosage = readIntByKey(myObject, "104", HeureArrosage);
  tempsArrosageSec = readIntByKey(myObject, "105", tempsArrosageSec);
  Serial.println(String("[SERVER][GET][APPLY] 110:") + raw110 + "=>" + String(resetMode ? 1 : 0) +
                 " 106:" + raw106 + "=>" + String(WakeUp ? 1 : 0) +
                 " 107:" + raw107 + "=>" + String(FreqWakeUp));
  Serial.println(String("[SERVER][GET] resetMode=") + String(resetMode ? 1 : 0) +
                 " wakeUp=" + String(WakeUp ? 1 : 0) +
                 " sleep=" + String(FreqWakeUp));
  if (displayOk) { display.fillCircle(115, 5, 5, WHITE); display.display(); }
}

void Wificonnect() {
  static const N3WifiNetwork networks[] = {
    {ssid, password}, {ssid2, password2}, {ssid3, password3}
  };
  N3WifiConfig cfg = {};
  cfg.networks = networks;
  cfg.networkCount = 3;
  cfg.timeoutMs = 5000;
  cfg.delayBetweenMs = 250;
  cfg.preScanDelayMs = 100;  // 300 ms était conservateur ; la radio se stabilise en ~100 ms
  cfg.scanMax = 10;
  cfg.onConnecting = []() {
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Wifi");
      display.println("CONNEXION...");
      display.display();
    }
  };
  cfg.onFailure = []() {
    Serial.println("WiFi : echec. Tous les reseaux ont echoue.");
    HeureSansWifi();
    if (displayOk) { display.println("ECHEC"); display.display(); }
  };
  cfg.onSuccess = [](const char* s) {
    (void)s;
    if (displayOk) { display.println(Wifiactif); display.println("OK"); display.display(); }
  };

  n3WifiConnect(cfg, &Wifiactif);

  delay(100);
  Serial.print("Reseau wifi: ");
  Serial.println(Wifiactif);
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

// Rapport mail reseau periodique (cumul RTC deep sleep, cf. OTA 2h).
RTC_DATA_ATTR static uint32_t s_netReportElapsedSeconds = 0;

void n3ppAccumulateNetReportElapsedFromSleep(int sleepSeconds) {
  if (sleepSeconds <= 0) return;
  if (s_netReportElapsedSeconds >= N3_NETWORK_REPORT_INTERVAL_S) return;
  const uint32_t sleepSec = static_cast<uint32_t>(sleepSeconds);
  const uint32_t remaining = N3_NETWORK_REPORT_INTERVAL_S - s_netReportElapsedSeconds;
  s_netReportElapsedSeconds += (sleepSec >= remaining) ? remaining : sleepSec;
}

unsigned int n3ppGetOutputsGetFailureCount() {
  return s_outputsGetFailureCount;
}

void n3ppMaybeSendNetworkReportEmail() {
  if (s_netReportElapsedSeconds < N3_NETWORK_REPORT_INTERVAL_S) {
    const uint32_t remaining = N3_NETWORK_REPORT_INTERVAL_S - s_netReportElapsedSeconds;
    Serial.printf("[MAIL][NET] rapport ignore, restant=%lu s\n",
                  static_cast<unsigned long>(remaining));
    return;
  }

  // Rapport reseau = diagnostic (P4) : envoye seulement si le mode l'autorise
  // (mode Full / legacy "checked"). Mode none/important/partial -> filtre.
  if (!n3NotifModeAllows(n3NotifModeFromString(enableEmailChecked.c_str()), N3Severity::Diagnostic)) {
    Serial.println("[MAIL][NET] rapport diagnostic (P4) filtre par le mode de notification, timer reinitialise");
    s_netReportElapsedSeconds = 0;
    n3NetStatsResetPeriod();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MAIL][NET] WiFi indisponible, rapport reporte");
    return;
  }

  char localTime[32];
  snprintf(localTime, sizeof(localTime), "%s", rtc.getTime("%d/%m/%Y %H:%M:%S").c_str());

  char ssidBuf[33];
  snprintf(ssidBuf, sizeof(ssidBuf), "%s", WiFi.SSID().c_str());
  String ipStr = WiFi.localIP().toString();

  N3NetStatsSnapshot stats = {};
  n3NetStatsGetSnapshot(stats);

  N3MailNetReportInfo report = {};
  report.projectName = "n3pp";
  report.sensorName = sensorName.c_str();
  report.firmwareVersion = version.c_str();
  report.localTime = localTime;
  report.wifiSsid = ssidBuf;
  report.wifiIp = ipStr.c_str();
  report.wifiRssiNow = WiFi.RSSI();
  report.bootCount = static_cast<uint32_t>(bootCount);
  report.uptimeSeconds = millis() / 1000UL;
  report.freeHeap = ESP.getFreeHeap();
  report.minFreeHeap = ESP.getMinFreeHeap();
  report.reportPeriodSeconds = N3_NETWORK_REPORT_INTERVAL_S;
  report.httpTimeoutMs = N3_HTTP_TIMEOUT_MS;
  report.outputsGetFailureStreak = static_cast<uint32_t>(s_outputsGetFailureCount);
  report.stats = stats;

  char body[2048];
  if (!n3MailBuildNetReportBody(report, body, sizeof(body))) {
    Serial.println("[MAIL][NET] Echec generation corps rapport");
    return;
  }

  N3MailSmtpConfig smtpCfg = {};
  smtpCfg.smtpHost = SMTP_HOST;
  smtpCfg.smtpPort = SMTP_PORT;
  smtpCfg.authorEmail = AUTHOR_EMAIL;
  smtpCfg.authorPassword = AUTHOR_PASSWORD;
  smtpCfg.senderName = "N3PP IoT";
  smtpCfg.recipientName = "N3";
  smtpCfg.recipientEmail = inputMessageMailAd.c_str();

  char subject[96];
  snprintf(subject, sizeof(subject), "[N3PP][P4] rapport reseau v%s", FIRMWARE_VERSION);

  String smtpError;
  if (n3MailSendText(smtpCfg, subject, body, &smtpError)) {
    Serial.println("[MAIL][NET] Rapport reseau envoye");
    s_netReportElapsedSeconds = 0;
    n3NetStatsResetPeriod();
  } else {
    Serial.printf("[MAIL][NET] Echec envoi: %s\n", smtpError.c_str());
  }
}
