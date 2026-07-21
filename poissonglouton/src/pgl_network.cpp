#include "pgl_network.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("secrets.h.example")
#include "secrets.h.example"
#else
#error "Fichier secrets.h manquant. Copier include/secrets.h.example en include/secrets.h"
#endif
#include "pgl_log.h"
#include "n3_data.h"
#include "n3_wifi.h"
#include "n3_wifi_reconnect.h"  // noyau pur mutualisé : record dernier AP + fast reconnect
#include <ArduinoJson.h>
#include <sys/time.h>
#include <time.h>

namespace {
// Ordre de tentative : inwi (SSID_2) en premier sur site production ; Techno/Android en secours.
N3WifiNetwork kWifiNetworks[] = {
    {PGL_WIFI_SSID_2, PGL_WIFI_PASS_2},
    {PGL_WIFI_SSID_1, PGL_WIFI_PASS_1},
#if defined(PGL_WIFI_SSID_3) && defined(PGL_WIFI_PASS_3)
    {PGL_WIFI_SSID_3, PGL_WIFI_PASS_3},
#endif
};

// --- Fast-reconnect WiFi -----------------------------------------------------
// Memorise le dernier AP connecte (SSID + BSSID + canal) en memoire RTC : ces
// donnees survivent au deep sleep (RTC_DATA_ATTR). Au reveil / a la reconnexion
// on tente d'abord une connexion CIBLEE (WiFi.begin avec canal + BSSID connus),
// ce qui evite le scan multi-canaux couteux. En cas d'echec/timeout court on
// retombe sur la session de scan n3_wifi habituelle (qui garantit la connexion
// meme si l'AP a change de canal/BSSID). Au power-on a froid, magic vaut 0 :
// pas de tentative ciblee, comportement actuel (scan direct).
// Record + validation/construction + politique de timeout mutualisés dans
// shared/n3_wifi_reconnect ; le magic reste propre à PGL (record RTC compatible
// à travers les OTA).
constexpr uint32_t kLastApMagic = 0x50474C57UL;  // "PGLW"
RTC_DATA_ATTR N3WifiReconnect::LastGoodAp gLastGoodAp;
uint8_t gLastStaDisconnectReason = 0;
const N3WifiSession* gWifiCallbackSession = nullptr;

void onWifiStaDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)event;
  gLastStaDisconnectReason = info.wifi_sta_disconnected.reason;
}

// Cherche le mot de passe configure pour un SSID donne (nullptr si introuvable).
const char* findPassForSsid(const char* ssid) {
  if (!ssid) return nullptr;
  const size_t netCount = sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]);
  for (size_t i = 0; i < netCount; ++i) {
    if (kWifiNetworks[i].ssid && strcmp(ssid, kWifiNetworks[i].ssid) == 0) {
      return kWifiNetworks[i].pass;
    }
  }
  return nullptr;
}

void onWifiConnecting() {
  PGL_LOG("WiFi: tentative connexion (scan / association)...");
}

void onWifiSuccess(const char* ssid) {
  PGL_LOG("WiFi: callback succes ssid=%s", ssid ? ssid : "?");
}

void onWifiFailure() {
  const uint8_t sessionCause =
      gWifiCallbackSession ? gWifiCallbackSession->failureReason : N3_WIFI_FAIL_NONE;
  PGL_LOG("WiFi: echec session — %s | stat=%s deauth=%u/%s",
          n3WifiSessionFailureReasonName(sessionCause),
          pglWifiStatusName(WiFi.status()),
          static_cast<unsigned int>(gLastStaDisconnectReason),
          pglWifiDisconnectReasonName(gLastStaDisconnectReason));
  if (gLastStaDisconnectReason == 15) {
    PGL_LOG("WiFi: echec 4WAY_HANDSHAKE — verifier mot de passe dans secrets.h (SSID courant)");
  }
}

void logWifiStatusDetail(const char* prefix) {
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG_V("%s: deconnecte (status=%s)", prefix, pglWifiStatusName(WiFi.status()));
    return;
  }
  char ssidBuf[33];
  strncpy(ssidBuf, WiFi.SSID().c_str(), sizeof(ssidBuf) - 1);
  ssidBuf[sizeof(ssidBuf) - 1] = '\0';
  PGL_LOG_V("%s: ssid=%s ip=%s gw=%s mask=%s dns=%s rssi=%d ch=%d mac=%s",
            prefix,
            ssidBuf,
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(),
            WiFi.subnetMask().toString().c_str(),
            WiFi.dnsIP().toString().c_str(),
            WiFi.RSSI(),
            WiFi.channel(),
            WiFi.macAddress().c_str());
}

const char* httpVerdict(int code) {
  if (code >= 200 && code < 300) return "OK";
  if (code >= 300 && code < 400) return "redirection";
  if (code == 401) return "cle API invalide (verifier PGL_API_KEY secrets.h)";
  if (code == 400) return "payload rejete (events manquant ou invalide)";
  if (code >= 400 && code < 500) return "erreur client";
  if (code == 500) return "erreur serveur (table BDD manquante ?)";
  if (code >= 500) return "erreur serveur";
  if (code == -1) return "timeout ou reseau (WiFi peut etre connecte)";
  if (code <= 0) return "echec reseau/timeout";
  return "inconnu";
}
const char* pglOptionalSigSecret() {
#if defined(PGL_API_SIG_SECRET)
  if (PGL_API_SIG_SECRET[0] != '\0') {
    return PGL_API_SIG_SECRET;
  }
#endif
  return nullptr;
}

}  // namespace

const PglServerCommStatus& PglNetwork::getServerStatus() const {
  return serverStatus_;
}

void PglNetwork::recordPostResult(int httpCode) {
  serverStatus_.lastPostHttp = httpCode;
  serverStatus_.lastPostMs = millis();
}

void PglNetwork::recordHeartbeatResult(int httpCode) {
  serverStatus_.lastHeartbeatHttp = httpCode;
  serverStatus_.lastHeartbeatMs = millis();
}

void PglNetwork::begin() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.onEvent(onWifiStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  const size_t netCount = sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]);
  PGL_LOG("WiFi: %u reseau(x) configure(s), timeout=%ums (arriere-plan)",
          static_cast<unsigned int>(netCount), PGL_WIFI_TIMEOUT_MS);
  for (size_t i = 0; i < netCount; ++i) {
    const bool hasPass = kWifiNetworks[i].pass && kWifiNetworks[i].pass[0] != '\0';
    PGL_LOG("WiFi: [%u] ssid=\"%s\" mot_de_passe=%s",
            static_cast<unsigned int>(i + 1),
            kWifiNetworks[i].ssid ? kWifiNetworks[i].ssid : "(null)",
            hasPass ? "oui" : "NON");
  }
  refreshWifiDiag();
  lastWifiStatus_ = WiFi.status();
}

void PglNetwork::buildWifiConfig(N3WifiConfig& cfg) const {
  cfg = {
      kWifiNetworks,
      sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]),
      PGL_WIFI_TIMEOUT_MS,
      300,
      PGL_WIFI_PRESCAN_DELAY_MS,
      8,
      onWifiConnecting,
      onWifiFailure,
      onWifiSuccess,
      true,  // PGL gere deja le fast-reconnect RTC (gLastGoodAp)
  };
}

// Tente une connexion ciblee sur le dernier AP connu (BSSID + canal memorises
// en RTC). Retourne true si une tentative directe a ete lancee : dans ce cas on
// n'ouvre PAS encore la session de scan, on attend l'issue dans pollWifi() avant
// d'eventuellement retomber sur le scan. Retourne false si aucun AP memorise
// (power-on a froid, ou AP precedent absent de la config) -> scan habituel.
bool PglNetwork::tryFastReconnect() {
  // Gate mutualisée (magic + canal) via shared/n3_wifi_reconnect.
  if (!N3WifiReconnect::valid(gLastGoodAp, kLastApMagic)) {
    return false;
  }
  const char* pass = findPassForSsid(gLastGoodAp.ssid);
  if (!pass) {
    // SSID memorise plus dans la config : on invalide et on passe au scan.
    gLastGoodAp.magic = 0;
    return false;
  }
  PGL_LOG("WiFi: reconnexion rapide ssid=%s ch=%u (BSSID memorise)",
          gLastGoodAp.ssid, static_cast<unsigned int>(gLastGoodAp.channel));
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(gLastGoodAp.ssid, pass, gLastGoodAp.channel, gLastGoodAp.bssid);
  wifiDirectAttempt_ = true;
  // Borne de la tentative ciblee : moitie du timeout WiFi (min 2 s) — politique mutualisée.
  wifiDirectDeadlineMs_ = millis() + N3WifiReconnect::fastTimeoutMs(PGL_WIFI_TIMEOUT_MS);
  return true;
}

// Memorise l'AP courant (SSID + BSSID + canal) en RTC apres une connexion
// reussie, pour permettre la reconnexion rapide au prochain reveil.
void PglNetwork::rememberLastGoodAp() {
  N3WifiReconnect::store(gLastGoodAp, kLastApMagic, WiFi.SSID().c_str(), WiFi.BSSID(),
                         WiFi.channel());
}

void PglNetwork::startBackgroundWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiSessionActive_ = false;
    wifiConnecting_ = false;
    wifiBackoff_ = false;
    wifiDirectAttempt_ = false;
    return;
  }
  // Fast-reconnect : si un AP est memorise, on tente d'abord une connexion
  // ciblee (canal + BSSID) avant d'engager le scan multi-reseaux n3_wifi.
  if (tryFastReconnect()) {
    wifiSessionActive_ = false;
    wifiConnecting_ = true;
    wifiBackoff_ = false;
    wifiRetryAfterMs_ = 0;
    refreshWifiDiag();
    return;
  }
  buildWifiConfig(wifiConfig_);
  n3WifiSessionBegin(wifiSession_, wifiConfig_);
  wifiSessionActive_ = true;
  wifiConnecting_ = true;
  wifiBackoff_ = false;
  wifiDirectAttempt_ = false;
  wifiRetryAfterMs_ = 0;
  PGL_LOG("WiFi: connexion en arriere-plan demarree");
  refreshWifiDiag();
}

bool PglNetwork::pollWifi(uint32_t budgetMs) {
  const wl_status_t prevStatus = WiFi.status();
  bool stateChanged = (prevStatus != lastWifiStatus_);

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting_ || wifiSessionActive_ || wifiDirectAttempt_) {
      wifiConnecting_ = false;
      wifiSessionActive_ = false;
      wifiBackoff_ = false;
      wifiDirectAttempt_ = false;
      // Memorise SSID+BSSID+canal pour la reconnexion rapide au prochain reveil.
      rememberLastGoodAp();
      stateChanged = true;
      char ssidBuf[33];
      strncpy(ssidBuf, WiFi.SSID().c_str(), sizeof(ssidBuf) - 1);
      ssidBuf[sizeof(ssidBuf) - 1] = '\0';
      PGL_LOG("WiFi: connecte ssid=%s ip=%s rssi=%d ch=%d",
              ssidBuf,
              WiFi.localIP().toString().c_str(),
              WiFi.RSSI(),
              WiFi.channel());
      logWifiStatusDetail("WiFi");
    }
    lastWifiStatus_ = WiFi.status();
    refreshWifiDiag();
    logWifiDiagPeriodic();
    return stateChanged;
  }

  // Perte de lien inattendue : relancer la connexion (sauf backoff actif).
  if (prevStatus == WL_CONNECTED && !wifiBackoff_) {
    wifiConnecting_ = false;
    wifiSessionActive_ = false;
    wifiDirectAttempt_ = false;
    startBackgroundWifi();
    stateChanged = true;
  }

  // Tentative ciblee (fast-reconnect) en cours : on attend l'issue sans engager
  // le scan. Au-dela de la borne de temps, on invalide l'AP memorise et on
  // bascule sur la session de scan n3_wifi (qui garantit la connexion).
  if (wifiDirectAttempt_) {
    if (millis() < wifiDirectDeadlineMs_) {
      lastWifiStatus_ = WiFi.status();
      refreshWifiDiag();
      logWifiDiagPeriodic();
      return stateChanged;
    }
    PGL_LOG("WiFi: reconnexion rapide echouee — bascule sur scan complet");
    gLastGoodAp.magic = 0;
    WiFi.disconnect(false, true);
    wifiDirectAttempt_ = false;
    N3WifiConfig cfg;
    buildWifiConfig(cfg);
    n3WifiSessionBegin(wifiSession_, cfg);
    wifiSessionActive_ = true;
    wifiConnecting_ = true;
    stateChanged = true;
  }

  if (wifiBackoff_) {
    if (millis() < wifiRetryAfterMs_) {
      lastWifiStatus_ = WiFi.status();
      refreshWifiDiag();
      logWifiDiagPeriodic();
      return stateChanged;
    }
    wifiBackoff_ = false;
    startBackgroundWifi();
    stateChanged = true;
  }

  if (!wifiSessionActive_) {
    lastWifiStatus_ = WiFi.status();
    refreshWifiDiag();
    logWifiDiagPeriodic();
    return stateChanged;
  }

  String connectedSsid;
  gWifiCallbackSession = &wifiSession_;
  const N3WifiPollResult result = n3WifiSessionPoll(wifiSession_, budgetMs, &connectedSsid);
  gWifiCallbackSession = nullptr;
  if (result == N3WifiPollResult::Connected) {
    wifiConnecting_ = false;
    wifiSessionActive_ = false;
    wifiBackoff_ = false;
    wifiDirectAttempt_ = false;
    // Memorise l'AP pour la reconnexion rapide au prochain reveil.
    rememberLastGoodAp();
    stateChanged = true;
    PGL_LOG("WiFi: connecte ssid=%s ip=%s rssi=%d ch=%d",
            connectedSsid.c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI(),
            WiFi.channel());
    logWifiStatusDetail("WiFi");
  } else if (result == N3WifiPollResult::Failed) {
    wifiConnecting_ = false;
    wifiSessionActive_ = false;
    wifiBackoff_ = true;
    wifiRetryAfterMs_ = millis() + PGL_WIFI_RETRY_INTERVAL_MS;
    stateChanged = true;
    PGL_LOG("WiFi: echec connexion (status=%s), nouvel essai dans %ums",
            pglWifiStatusName(WiFi.status()),
            static_cast<unsigned int>(PGL_WIFI_RETRY_INTERVAL_MS));
  } else {
    wifiConnecting_ = true;
  }

  if (prevStatus != WiFi.status()) {
    stateChanged = true;
  }
  lastWifiStatus_ = WiFi.status();
  refreshWifiDiag();
  logWifiDiagPeriodic();
  return stateChanged;
}

void PglNetwork::refreshWifiDiag() {
  wifiDiag_.wlStatus = static_cast<uint8_t>(WiFi.status());
  wifiDiag_.connected = (WiFi.status() == WL_CONNECTED);
  wifiDiag_.disconnectReason = gLastStaDisconnectReason;
  wifiDiag_.retryInSec = 0;
  wifiDiag_.targetSsid[0] = '\0';

  if (wifiDiag_.connected) {
    strncpy(wifiDiag_.phase, "ok", sizeof(wifiDiag_.phase) - 1);
    wifiDiag_.phase[sizeof(wifiDiag_.phase) - 1] = '\0';
    char ssidBuf[33];
    strncpy(ssidBuf, WiFi.SSID().c_str(), sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    strncpy(wifiDiag_.targetSsid, ssidBuf, sizeof(wifiDiag_.targetSsid) - 1);
    wifiDiag_.targetSsid[sizeof(wifiDiag_.targetSsid) - 1] = '\0';
    snprintf(wifiDiag_.line, sizeof(wifiDiag_.line), "WiFi: %s (%d dBm)",
             ssidBuf[0] ? ssidBuf : "?", WiFi.RSSI());
    wifiDiag_.connecting = false;
    return;
  }

  wifiDiag_.connecting =
      wifiConnecting_ || wifiSessionActive_ || wifiDirectAttempt_ || wifiBackoff_;

  if (wifiBackoff_) {
    if (millis() < wifiRetryAfterMs_) {
      wifiDiag_.retryInSec =
          static_cast<uint16_t>((wifiRetryAfterMs_ - millis() + 999) / 1000);
    }
    strncpy(wifiDiag_.phase, "attente", sizeof(wifiDiag_.phase) - 1);
    wifiDiag_.phase[sizeof(wifiDiag_.phase) - 1] = '\0';
    snprintf(wifiDiag_.line, sizeof(wifiDiag_.line),
             "WiFi: pause %us\nstat=%s raison=%s",
             static_cast<unsigned int>(wifiDiag_.retryInSec),
             pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)),
             pglWifiDisconnectReasonName(wifiDiag_.disconnectReason));
    return;
  }

  if (wifiDirectAttempt_) {
    strncpy(wifiDiag_.phase, "rapide", sizeof(wifiDiag_.phase) - 1);
    wifiDiag_.phase[sizeof(wifiDiag_.phase) - 1] = '\0';
    strncpy(wifiDiag_.targetSsid, gLastGoodAp.ssid, sizeof(wifiDiag_.targetSsid) - 1);
    wifiDiag_.targetSsid[sizeof(wifiDiag_.targetSsid) - 1] = '\0';
    const uint32_t leftMs = (millis() < wifiDirectDeadlineMs_)
                                ? (wifiDirectDeadlineMs_ - millis())
                                : 0;
    snprintf(wifiDiag_.line, sizeof(wifiDiag_.line),
             "WiFi: rapide «%s» ch%u\nstat=%s ~%lus",
             wifiDiag_.targetSsid[0] ? wifiDiag_.targetSsid : "?",
             static_cast<unsigned int>(gLastGoodAp.channel),
             pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)),
             static_cast<unsigned long>((leftMs + 999) / 1000));
    return;
  }

  if (wifiSessionActive_) {
    const char* phase = n3WifiSessionPhaseName(wifiSession_.phase);
    strncpy(wifiDiag_.phase, phase, sizeof(wifiDiag_.phase) - 1);
    wifiDiag_.phase[sizeof(wifiDiag_.phase) - 1] = '\0';
    if (wifiSession_.currentSsid[0] != '\0') {
      strncpy(wifiDiag_.targetSsid, wifiSession_.currentSsid, sizeof(wifiDiag_.targetSsid) - 1);
      wifiDiag_.targetSsid[sizeof(wifiDiag_.targetSsid) - 1] = '\0';
    }
    if (wifiDiag_.targetSsid[0] != '\0') {
      snprintf(wifiDiag_.line, sizeof(wifiDiag_.line),
               "WiFi: %s «%.32s»\nstat=%s idx=%u/%u",
               phase,
               wifiDiag_.targetSsid,
               pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)),
               wifiSession_.orderCount > 0
                   ? static_cast<unsigned int>(wifiSession_.orderIdx + 1)
                   : 0U,
               static_cast<unsigned int>(wifiSession_.orderCount));
    } else {
      snprintf(wifiDiag_.line, sizeof(wifiDiag_.line),
               "WiFi: %s\nstat=%s idx=%u/%u",
               phase,
               pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)),
               wifiSession_.orderCount > 0
                   ? static_cast<unsigned int>(wifiSession_.orderIdx + 1)
                   : 0U,
               static_cast<unsigned int>(wifiSession_.orderCount));
    }
    return;
  }

  if (wifiConnecting_) {
    strncpy(wifiDiag_.phase, "demarrage", sizeof(wifiDiag_.phase) - 1);
    wifiDiag_.phase[sizeof(wifiDiag_.phase) - 1] = '\0';
    snprintf(wifiDiag_.line, sizeof(wifiDiag_.line),
             "WiFi: demarrage...\nstat=%s",
             pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)));
    return;
  }

  strncpy(wifiDiag_.phase, "offline", sizeof(wifiDiag_.phase) - 1);
  wifiDiag_.phase[sizeof(wifiDiag_.phase) - 1] = '\0';
  snprintf(wifiDiag_.line, sizeof(wifiDiag_.line),
           "WiFi: hors ligne\nstat=%s raison=%s",
           pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)),
           pglWifiDisconnectReasonName(wifiDiag_.disconnectReason));
  wifiDiag_.connecting = false;
}

void PglNetwork::logWifiDiagPeriodic() {
  if (wifiDiag_.connected) {
    return;
  }
  const uint32_t now = millis();
  if (wifiLastDiagLogMs_ != 0 && (now - wifiLastDiagLogMs_) < 5000) {
    return;
  }
  wifiLastDiagLogMs_ = now;
  PGL_LOG("WiFi diag: phase=%s stat=%s ssid=\"%s\" retry=%us reason=%u/%s | %s",
          wifiDiag_.phase,
          pglWifiStatusName(static_cast<wl_status_t>(wifiDiag_.wlStatus)),
          wifiDiag_.targetSsid[0] ? wifiDiag_.targetSsid : "-",
          static_cast<unsigned int>(wifiDiag_.retryInSec),
          static_cast<unsigned int>(wifiDiag_.disconnectReason),
          pglWifiDisconnectReasonName(wifiDiag_.disconnectReason),
          wifiDiag_.line);
}

const PglWifiDiag& PglNetwork::getWifiDiag() const {
  return wifiDiag_;
}

bool PglNetwork::isWifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool PglNetwork::isWifiOffline() const {
  return WiFi.status() != WL_CONNECTED && !isWifiConnecting();
}

bool PglNetwork::isWifiConnecting() const {
  if (WiFi.status() == WL_CONNECTED) {
    return false;
  }
  return wifiConnecting_ || wifiSessionActive_ || wifiDirectAttempt_ || wifiBackoff_;
}

void PglNetwork::tryConnectBeforeUpload() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  // Ne pas relancer une connexion si une tentative ciblee (fast-reconnect) ou
  // une session de scan est deja en cours.
  if (!wifiSessionActive_ && !wifiBackoff_ && !wifiDirectAttempt_) {
    startBackgroundWifi();
  }
  pollWifi(PGL_WIFI_UPLOAD_BUDGET_MS);
}

PglUploadResult PglNetwork::uploadBatch(const PglStoredEvent* events, size_t count,
                                        uint32_t totalCount, uint32_t todayCount) {
  PglUploadResult result;
  if (!events || count == 0) {
    result.ok = true;
    return result;
  }

  tryConnectBeforeUpload();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Serveur POST: annule — WiFi %s", pglWifiStatusName(WiFi.status()));
    recordPostResult(-1);
    return result;
  }
  logWifiStatusDetail("Serveur POST");

  PGL_LOG("Serveur POST: envoi vers %s", PGL_SERVER_POST_URL);
  PGL_LOG("Serveur POST: lot=%u evt | total=%lu today=%lu | sensor=%s location=%s v=%s",
          static_cast<unsigned int>(count),
          static_cast<unsigned long>(totalCount),
          static_cast<unsigned long>(todayCount),
          PGL_SENSOR_NAME,
          PGL_SENSOR_LOCATION,
          PGL_FIRMWARE_VERSION);

  char eventsPayload[2048];
  size_t off = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0 && off + 1 < sizeof(eventsPayload)) {
      eventsPayload[off++] = ',';
    }
    const int written = snprintf(
        eventsPayload + off, sizeof(eventsPayload) - off, "%lu:%u:%u:%u:%d:%d:%lu",
        static_cast<unsigned long>(events[i].epoch),
        static_cast<unsigned int>(events[i].countDelta),
        static_cast<unsigned int>(events[i].sensorMode),
        static_cast<unsigned int>(events[i].tandemValidated),
        static_cast<int>(events[i].batteryMilliVolt),
        static_cast<int>(events[i].rssi),
        static_cast<unsigned long>(events[i].eventId));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(eventsPayload) - off) {
      PGL_LOG("Serveur POST: payload events trop long (lot=%u)", static_cast<unsigned int>(count));
      recordPostResult(-2);
      return result;
    }
    off += static_cast<size_t>(written);
  }
  eventsPayload[off] = '\0';

  PGL_LOG_V("Serveur POST: payload events=%s", eventsPayload);

  String responseBody;

  N3DataField fields[] = {
      {"api_key", String(PGL_API_KEY)},
      {"sensor", String(PGL_SENSOR_NAME)},
      {"location", String(PGL_SENSOR_LOCATION)},
      {"version", String(PGL_FIRMWARE_VERSION)},
      {"total_count", String(totalCount)},
      {"today_count", String(todayCount)},
      {"batch_count", String(static_cast<unsigned int>(count))},
      {"events", String(eventsPayload)},
  };

  const time_t epochNow = time(nullptr);
  const unsigned long epochSec =
      (epochNow > 1577836800L) ? static_cast<unsigned long>(epochNow) : 0UL;
  const char* sigSecret = pglOptionalSigSecret();

  N3PostConfig cfg = {
      PGL_SERVER_POST_URL,
      PGL_API_KEY,
      fields,
      sizeof(fields) / sizeof(fields[0]),
      nullptr,
      nullptr,
      sigSecret,
      epochSec,
      &responseBody,
  };
  const int code = n3DataPost(cfg);
  recordPostResult(code);
  result.ok = (code >= 200 && code < 300);

  PGL_LOG("Serveur POST: HTTP %d — %s (lot=%u evt, total=%lu)",
          code,
          httpVerdict(code),
          static_cast<unsigned int>(count),
          static_cast<unsigned long>(totalCount));

  if (result.ok && responseBody.length() > 0) {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, responseBody);
    if (!err && doc["last_acked_event_id"].is<uint32_t>()) {
      result.lastAckedEventId = doc["last_acked_event_id"].as<uint32_t>();
      PGL_LOG_V("Serveur POST: ack last_event_id=%lu",
                static_cast<unsigned long>(result.lastAckedEventId));
    } else if (err) {
      PGL_LOG_V("Serveur POST: parse JSON ack echoue (%s)", err.c_str());
    }
  }

  return result;
}

bool PglNetwork::sendHeartbeat(uint32_t bootCount, const PglHeartbeatTelemetry& telemetry) {
#if !PGL_ENABLE_SERVER_HEARTBEAT
  (void)bootCount;
  (void)telemetry;
  return false;
#else
  tryConnectBeforeUpload();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Serveur HB: annule — WiFi %s", pglWifiStatusName(WiFi.status()));
    recordHeartbeatResult(-1);
    return false;
  }
  logWifiStatusDetail("Serveur HB");

  static uint32_t minHeap = UINT32_MAX;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < minHeap) {
    minHeap = freeHeap;
  }

  const uint32_t uptimeSec = millis() / 1000UL;
  const int rssi = WiFi.RSSI();

  PGL_LOG("Serveur HB: envoi vers %s", PGL_SERVER_HEARTBEAT_URL);
  PGL_LOG_V("Serveur HB: uptime=%lus free=%lu min=%lu reboots=%lu rssi=%d",
            static_cast<unsigned long>(uptimeSec),
            static_cast<unsigned long>(freeHeap),
            static_cast<unsigned long>(minHeap == UINT32_MAX ? freeHeap : minHeap),
            static_cast<unsigned long>(bootCount),
            rssi);
  PGL_LOG_V("Serveur HB: pending=%u journal=%lu nvs=%u sd=%d batt=%dmV sensors_present=%u",
            static_cast<unsigned int>(telemetry.pending),
            static_cast<unsigned long>(telemetry.journalPending),
            static_cast<unsigned int>(telemetry.nvsPending),
            telemetry.sdOk ? 1 : 0,
            telemetry.batteryMilliVolt,
            static_cast<unsigned int>(telemetry.sensorsPresent));

  N3DataField fields[] = {
      {"api_key", String(PGL_API_KEY)},
      {"sensor", String(PGL_SENSOR_NAME)},
      {"version", String(PGL_FIRMWARE_VERSION)},
      {"uptime", String(uptimeSec)},
      {"free", String(freeHeap)},
      {"min", String(minHeap == UINT32_MAX ? freeHeap : minHeap)},
      {"reboots", String(bootCount)},
      {"rssi", String(rssi)},
      {"pending", String(telemetry.pending)},
      {"journal_pending", String(telemetry.journalPending)},
      {"nvs_pending", String(telemetry.nvsPending)},
      {"sd_ok", String(telemetry.sdOk ? 1 : 0)},
      {"battery_mv", String(telemetry.batteryMilliVolt)},
      {"sensors_present", String(static_cast<unsigned int>(telemetry.sensorsPresent))},
  };

  const char* sigSecret = pglOptionalSigSecret();
  const time_t epochNow = time(nullptr);
  const unsigned long epochSec =
      (epochNow > 1577836800L) ? static_cast<unsigned long>(epochNow) : 0UL;

  N3PostConfig cfg = {
      PGL_SERVER_HEARTBEAT_URL,
      PGL_API_KEY,
      fields,
      sizeof(fields) / sizeof(fields[0]),
      nullptr,
      nullptr,
      sigSecret,
      epochSec,
      nullptr,
  };
  const int code = n3DataPost(cfg);
  recordHeartbeatResult(code);
  const bool ok = (code >= 200 && code < 300);
  PGL_LOG("Serveur HB: HTTP %d — %s (uptime=%lus reboots=%lu)",
          code,
          httpVerdict(code),
          static_cast<unsigned long>(uptimeSec),
          static_cast<unsigned long>(bootCount));
  return ok;
#endif
}
