// n3_contact_diag.cpp — voir n3_contact_diag.h (n3_data 1.4, incident FFP3 06/11/2026).
#include "n3_contact_diag.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n3_boot_diag.h"     // n3_common (pur)
#include "n3_event_ring.h"    // n3_common (pur)
#include "n3_ota_hooks.h"     // n3_common : définitions FORTES des accroches OTA ci-dessous
#include "n3_reset_reason.h"  // n3_common (pur)
#include "n3_sleep_clock.h"   // n3_time (pur) : décalage serveur

namespace {

constexpr uint8_t kCapacity = 12;
constexpr const char* kNvsNamespace = "n3evt";
constexpr const char* kKeyFails = "fails";
constexpr uint32_t kRepeatSuppressMs = 30000;
constexpr uint32_t kMinPlausibleEpoch = 1704067200UL;  // 2024-01-01

// Anneau alloué sur le tas au premier usage : cette unité est compilée dans TOUS les firmwares
// qui embarquent n3_data (dont ffp5cs, dont la DRAM statique WROOM est saturée) ; un objet statique
// de ~1 Ko ferait déborder dram0_0_seg même quand le module n'est pas utilisé.
using RingT = n3evt::Ring<kCapacity>;
RingT* s_ringPtr = nullptr;
RingT& ring() {
  if (!s_ringPtr) s_ringPtr = new RingT();
  return *s_ringPtr;
}
N3ContactDiagConfig s_cfg = {};
bool s_began = false;
bool s_safeMode = false;
bool s_healthy = false;
bool s_ntpConfirmed = false;
bool s_bootFlagPending = true;
uint8_t s_failedBoots = 0;
uint32_t s_wifiMs = 0;
uint16_t s_wifiAttempts = 0;
bool s_hasOffset = false;
int32_t s_offsetS = 0;
int32_t s_lastLoggedDriftS = 0;
char s_lastEvent[n3evt::kEventChars] = {0};
uint32_t s_lastEventMs = 0;
char s_partition[8] = "?";
uint32_t s_otaState = 0xFFFFFFFFUL;
esp_reset_reason_t s_resetReason = ESP_RST_UNKNOWN;

void slotKey(uint8_t i, char* out, size_t size) { snprintf(out, size, "s%02u", (unsigned)i); }

uint32_t nowEpochOrZero() {
  const time_t t = time(nullptr);
  return (t > (time_t)kMinPlausibleEpoch) ? (uint32_t)t : 0;
}

void persistSlot(uint8_t index) {
  char key[8];
  char value[n3evt::kSlotChars];
  slotKey(index, key, sizeof(key));
  if (!ring().encodeSlot(index, value, sizeof(value))) return;
  Preferences p;
  if (!p.begin(kNvsNamespace, false)) return;
  if (value[0] == '\0') p.remove(key); else p.putString(key, value);
  p.end();
}

void persistFails() {
  Preferences p;
  if (!p.begin(kNvsNamespace, false)) return;
  p.putUChar(kKeyFails, s_failedBoots);
  p.end();
}

void loadState() {
  ring().clear();
  Preferences p;
  if (!p.begin(kNvsNamespace, false)) return;  // RW : crée l'espace de noms au premier boot
  s_failedBoots = p.getUChar(kKeyFails, 0);
  for (uint8_t i = 0; i < kCapacity; ++i) {
    char key[8];
    slotKey(i, key, sizeof(key));
    char value[n3evt::kSlotChars] = {0};
    if (p.isKey(key)) p.getString(key, value, sizeof(value));
    if (!ring().decodeSlot(i, value)) {
      ring().decodeSlot(i, "");
      p.remove(key);
    }
  }
  p.end();
  ring().recomputeHead();
}

void readPartitionState() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  strncpy(s_partition, running->label, sizeof(s_partition) - 1);
  s_partition[sizeof(s_partition) - 1] = '\0';
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(running, &st) == ESP_OK) s_otaState = (uint32_t)st;
}

uint32_t parseMaxAcked(const char* resp) {
  if (!resp) return 0;
  const char* p = strstr(resp, "\"acked\"");
  if (!p) return 0;
  p = strchr(p, '[');
  if (!p) return 0;
  uint32_t max = 0;
  ++p;
  while (*p && *p != ']') {
    while (*p == ' ' || *p == ',') ++p;
    if (*p < '0' || *p > '9') break;
    char* end = nullptr;
    const unsigned long v = strtoul(p, &end, 10);
    if (end == p) break;
    if (v > max) max = (uint32_t)v;
    p = end;
  }
  return max;
}

void markHealthy() {
  if (s_healthy) return;
  s_healthy = true;
  s_failedBoots = 0;
  persistFails();
  n3ContactDiagLogf("contact_ok", "uptime=%lus%s", millis() / 1000UL, s_safeMode ? " safe" : "");
}

// Observateur n3_data : chaque POST (données, heartbeat, /ota-report) passe ici.
void onResponse(int code, bool signedRequest, uint32_t serverEpoch, uint32_t rttMs) {
  if (serverEpoch >= kMinPlausibleEpoch) {
    const uint32_t deviceNow = (uint32_t)time(nullptr);
    s_offsetS = n3clock::serverOffsetSeconds(deviceNow, serverEpoch, rttMs);
    s_hasOffset = true;
    if (!s_ntpConfirmed && s_cfg.applyServerEpoch) {
      if (s_cfg.applyServerEpoch(serverEpoch, rttMs)) {
        n3ContactDiagLogf("clock_srv", "offset=%+lds rtt=%lums", (long)s_offsetS, (unsigned long)rttMs);
      }
      s_ntpConfirmed = true;  // heure serveur appliquée : on peut signer (fenêtre HMAC)
    } else {
      const int32_t a = s_offsetS < 0 ? -s_offsetS : s_offsetS;
      const int32_t l = s_lastLoggedDriftS < 0 ? -s_lastLoggedDriftS : s_lastLoggedDriftS;
      if (a > 60 && (l == 0 || a > l + 30 || a + 30 < l)) {
        s_lastLoggedDriftS = s_offsetS;
        n3ContactDiagLogf("clock_drift", "offset=%+lds ntp=%d", (long)s_offsetS, s_ntpConfirmed ? 1 : 0);
      }
    }
  }
  if (code >= 200 && code < 400) {
    if (signedRequest) markHealthy();
    return;
  }
  if (code == 401) {
    n3ContactDiagLogf("http_401", "%s clk=%ld", signedRequest ? "hmac" : "api_key",
                      s_hasOffset ? (long)s_offsetS : 0L);
  } else if (code >= 500) {
    n3ContactDiagLogf("http_5xx", "code=%d", code);
  } else if (code <= 0) {
    n3ContactDiagLogf("http_fail", "code=%d rssi=%d", code,
                      (WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : -127);
  } else {
    n3ContactDiagLogf("http_4xx", "code=%d", code);
  }
}

}  // namespace

// ---- Accroches OTA (définitions fortes, cf. n3_common/n3_ota_hooks.h) -----------------------
void n3OtaHookEvent(const char* event, const char* detail) {
  n3ContactDiagLog(event, detail);
  if (event && (strcmp(event, "ota_start") == 0 || strcmp(event, "ota_done") == 0 ||
                strcmp(event, "ota_fail") == 0)) {
    n3ContactDiagFlush();  // pousser au serveur AVANT le reboot (ota_done) ou l'abandon
  }
}
bool n3OtaHookUpdatesAllowed() { return !s_safeMode; }
const char* n3OtaHookBoardLabel() { return s_cfg.board; }

// ---- API publique --------------------------------------------------------------------------
bool n3ContactDiagDeriveOtaReportUrl(const char* heartbeatUrl, char* out, size_t outSize) {
  if (!heartbeatUrl || !out || outSize == 0) return false;
  static const char kSuffix[] = "/heartbeat";
  const size_t len = strlen(heartbeatUrl);
  const size_t sl = sizeof(kSuffix) - 1;
  if (len < sl || strcmp(heartbeatUrl + len - sl, kSuffix) != 0) { out[0] = '\0'; return false; }
  const int n = snprintf(out, outSize, "%.*s/ota-report", (int)(len - sl), heartbeatUrl);
  return n > 0 && (size_t)n < outSize;
}

void n3ContactDiagBegin(const N3ContactDiagConfig& cfg) {
  if (s_began) return;
  s_began = true;
  s_cfg = cfg;
  s_resetReason = esp_reset_reason();
  readPartitionState();
  loadState();
  if (s_failedBoots < 250) ++s_failedBoots;
  persistFails();  // AVANT tout le reste : un crash dans la suite du boot compte
  s_safeMode = n3boot::shouldEnterSafeMode(s_failedBoots);
  n3DataSetResponseObserver(onResponse);

  char detail[n3evt::kDetailChars];
  snprintf(detail, sizeof(detail), "rst=%s part=%s ota=%s fails=%u v=%s%s",
           ResetReason::resetReasonToString(s_resetReason), s_partition, n3boot::otaStateLabel(s_otaState),
           (unsigned)s_failedBoots, cfg.firmwareVersion ? cfg.firmwareVersion : "?", s_safeMode ? " SAFE" : "");
  n3ContactDiagLog(s_resetReason == ESP_RST_DEEPSLEEP ? "wake" : "boot", detail);
  if (s_safeMode) {
    Serial.printf("[ContactDiag] MODE SANS ECHEC : %u reveils consecutifs sans contact sain -> OTA suspendue\n",
                  (unsigned)s_failedBoots);
  }
  Serial.printf("[ContactDiag] %s | %u evenement(s) en attente\n", detail, (unsigned)ring().pendingCount());
}

void n3ContactDiagLog(const char* event, const char* detail) {
  if (!event) return;
  const uint32_t now = millis();
  if (s_lastEvent[0] && strcmp(s_lastEvent, event) == 0 && (now - s_lastEventMs) < kRepeatSuppressMs) return;
  strncpy(s_lastEvent, event, sizeof(s_lastEvent) - 1);
  s_lastEvent[sizeof(s_lastEvent) - 1] = '\0';
  s_lastEventMs = now;
  const uint8_t index = ring().head();
  ring().push(event, detail, nowEpochOrZero(), now / 1000UL);
  if (s_began) persistSlot(index);
  Serial.printf("[ContactDiag] %s %s\n", event, detail ? detail : "");
}

void n3ContactDiagLogf(const char* event, const char* fmt, ...) {
  char detail[n3evt::kDetailChars];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof(detail), fmt, ap);
  va_end(ap);
  n3ContactDiagLog(event, detail);
}

void n3ContactDiagOnWifiResult(bool connected, uint32_t connectMs, uint16_t attempts) {
  s_wifiMs = connectMs;
  s_wifiAttempts = attempts;
  if (connected) {
    n3ContactDiagLogf("wifi_ok", "rssi=%d in %lums tries=%u", (int)WiFi.RSSI(), (unsigned long)connectMs, (unsigned)attempts);
  } else {
    n3ContactDiagLogf("wifi_fail", "status=%d after=%lums tries=%u", (int)WiFi.status(), (unsigned long)connectMs, (unsigned)attempts);
  }
}

void n3ContactDiagOnNtpResult(bool confirmed) {
  s_ntpConfirmed = confirmed;
  n3ContactDiagLogf(confirmed ? "ntp_ok" : "ntp_fail", "epoch=%lu", (unsigned long)time(nullptr));
}

bool n3ContactDiagIsSafeMode() { return s_safeMode; }
uint8_t n3ContactDiagFailedBoots() { return s_failedBoots; }
bool n3ContactDiagIsHealthy() { return s_healthy; }
bool n3ContactDiagHasClockOffset() { return s_hasOffset; }
int32_t n3ContactDiagClockOffsetSeconds() { return s_offsetS; }

size_t n3ContactDiagHeartbeatFields(N3DataField* out, size_t max) {
  if (!out || max == 0) return 0;
  readPartitionState();
  size_t n = 0;
  auto put = [&](const char* name, const String& value) {
    if (n < max) { out[n].name = name; out[n].value = value; ++n; }
  };
  put("rst", String(ResetReason::resetReasonToString(s_resetReason)));
  put("part", String(s_partition));
  put("ota", String(n3boot::otaStateLabel(s_otaState)));
  put("fails", String((unsigned)s_failedBoots));
  put("safe", String(s_safeMode ? 1 : 0));
  if (s_hasOffset) put("clk", String((long)s_offsetS));
  if (s_wifiMs > 0) {
    put("wifi_ms", String((unsigned long)s_wifiMs));
    put("wifi_tries", String((unsigned)s_wifiAttempts));
  }
  if (s_cfg.board) put("board", String(s_cfg.board));
  if (s_bootFlagPending) {
    put("boot", String(1));
    s_bootFlagPending = false;
  }
  if (ring().pendingCount() > 0) {
    char json[361];
    if (ring().toJson(json, sizeof(json)) > 0) put("log", String(json));
  }
  return n;
}

uint8_t n3ContactDiagPendingCount() { return ring().pendingCount(); }

bool n3ContactDiagFlush(bool force) {
  if (ring().pendingCount() == 0) return true;
  if (!s_began || !s_cfg.otaReportUrl || !s_cfg.otaReportUrl[0]) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  const unsigned long epoch = s_cfg.epochProvider ? s_cfg.epochProvider() : 0UL;
  if (!force && (epoch == 0 || !s_cfg.sigSecret || !s_cfg.sigSecret[0])) return false;

  char events[700];
  uint32_t lastSeq = 0;
  const uint8_t count = ring().toJson(events, sizeof(events), &lastSeq);
  if (count == 0) return true;

  N3DataField fields[] = {
    {"api_key", String(s_cfg.apiKeyField ? s_cfg.apiKeyField : "")},
    {"src",     String(s_cfg.source ? s_cfg.source : "ota_report")},
    {"board",   String(s_cfg.board ? s_cfg.board : "")},
    {"version", String(s_cfg.firmwareVersion ? s_cfg.firmwareVersion : "")},
    {"uptime",  String(millis() / 1000UL)},
    {"events",  String(events)},
  };
  String response;
  N3PostConfig cfg = {};
  cfg.url = s_cfg.otaReportUrl;
  cfg.apiKey = s_cfg.apiKeyHeader;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  cfg.sigSecret = s_cfg.sigSecret;
  cfg.currentEpochSeconds = epoch;
  cfg.responseBodyOut = &response;
  const int code = n3DataPost(cfg);
  if (code < 200 || code >= 300) return false;
  const uint32_t acked = parseMaxAcked(response.c_str());
  if (acked == 0) return false;
  for (uint8_t i = 0; i < kCapacity; ++i) {
    const n3evt::Event& e = ring().slot(i);
    if (e.seq != 0 && e.seq <= acked) {
      ring().ackUpTo(e.seq);
      persistSlot(i);
    }
  }
  Serial.printf("[ContactDiag] /ota-report : %u envoye(s), acquittes jusqu'a #%lu, reste %u\n",
                (unsigned)count, (unsigned long)acked, (unsigned)ring().pendingCount());
  return true;
}
