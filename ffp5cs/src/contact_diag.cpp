// contact_diag.cpp — voir include/contact_diag.h (v15.34, incident FFP3 06/11/2026).
#include "contact_diag.h"

#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <new>

#include "boot_log.h"
#include "config.h"
#include "nvs_manager.h"
#include "ota_model.h"
#include "power.h"
#include "web_client.h"
#include "n3_boot_diag.h"
#include "n3_event_ring.h"
#include "n3_reset_reason.h"
#include "n3_sleep_clock.h"

extern PowerManager power;  // app.cpp
extern WebClient web;       // app.cpp

namespace {

constexpr const char* kKeyFailedBoots = "evt_bootfail";  // ≤ 15 car. (limite NVS)
constexpr const char* kSlotPrefix = "evt_";             // evt_00 … evt_11
constexpr uint32_t kRepeatSuppressMs = 30000;

// Anneau alloué sur le tas au premier usage : la DRAM statique du WROOM est saturée (~99,9 %),
// 12 × 76 o en .bss faisaient déborder dram0_0_seg. nullptr si l'allocation échoue (no-op).
using RingT = n3evt::Ring<ContactDiag::kRingCapacity>;
RingT* s_ringPtr = nullptr;
RingT* ring() {
  if (!s_ringPtr) s_ringPtr = new (std::nothrow) RingT();
  return s_ringPtr;
}
bool s_began = false;
bool s_safeMode = false;
bool s_healthy = false;
bool s_bootFlagPending = true;  // boot=1 à émettre dans le premier heartbeat
uint8_t s_failedBoots = 0;
uint32_t s_wifiConnectMs = 0;
uint16_t s_wifiAttempts = 0;
bool s_hasOffset = false;
int32_t s_offsetS = 0;
int32_t s_lastLoggedDriftS = 0;
char s_lastEvent[n3evt::kEventChars] = {0};
uint32_t s_lastEventMs = 0;
char s_partition[8] = "?";
uint32_t s_otaState = 0xFFFFFFFFUL;
esp_reset_reason_t s_resetReason = ESP_RST_UNKNOWN;

void slotKey(uint8_t i, char* out, size_t size) { snprintf(out, size, "%s%02u", kSlotPrefix, (unsigned)i); }

uint32_t nowEpochOrZero() {
  time_t t = time(nullptr);
  // Avant 2024 l'heure n'est pas plausible (compile fallback / non synchronisée) : 0 = inconnue.
  return (t > 1704067200L) ? (uint32_t)t : 0;
}

void persistSlot(uint8_t index) {
  if (!ring()) return;
  char key[16];
  char value[n3evt::kSlotChars];
  slotKey(index, key, sizeof(key));
  if (!ring()->encodeSlot(index, value, sizeof(value))) return;
  if (value[0] == '\0') {
    g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, key);
  } else {
    g_nvsManager.saveString(NVS_NAMESPACES::LOGS, key, value);
  }
}

void loadRing() {
  if (!ring()) return;
  ring()->clear();
  for (uint8_t i = 0; i < ContactDiag::kRingCapacity; ++i) {
    char key[16];
    char value[n3evt::kSlotChars];
    slotKey(i, key, sizeof(key));
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, key, value, sizeof(value), "");
    if (!ring()->decodeSlot(i, value)) {
      ring()->decodeSlot(i, "");  // slot illisible → vide
      g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, key);
    }
  }
  ring()->recomputeHead();
}

void readPartitionState() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    strncpy(s_partition, running->label, sizeof(s_partition) - 1);
    s_partition[sizeof(s_partition) - 1] = '\0';
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK) s_otaState = (uint32_t)st;
  }
}

// Acquittements du serveur : {"status":"ok","acked":[12,13,14],...} → plus haute séquence.
void appendf(char* buf, size_t size, size_t& len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
void appendf(char* buf, size_t size, size_t& len, const char* fmt, ...) {
  if (len >= size) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + len, size - len, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((size_t)n >= size - len) { buf[len] = '\0'; return; }  // champ incomplet → retiré
  len += (size_t)n;
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
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) break;
    if (v > max) max = (uint32_t)v;
    p = end;
  }
  return max;
}

}  // namespace

namespace ContactDiag {

void begin() {
  if (s_began) return;
  s_began = true;
  s_resetReason = esp_reset_reason();
  readPartitionState();
  loadRing();

  int fails = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, kKeyFailedBoots, fails, 0);
  if (fails < 0) fails = 0;
  if (fails > 250) fails = 250;
  s_failedBoots = (uint8_t)(fails + 1);
  // Persisté AVANT tout le reste : un crash dans la suite du boot doit compter.
  g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, kKeyFailedBoots, (int)s_failedBoots);
  s_safeMode = n3boot::shouldEnterSafeMode(s_failedBoots);

  char detail[n3evt::kDetailChars];
  snprintf(detail, sizeof(detail), "rst=%s part=%s ota=%s fails=%u v=%s%s",
           ResetReason::resetReasonToString(s_resetReason), s_partition,
           n3boot::otaStateLabel(s_otaState), (unsigned)s_failedBoots, ProjectConfig::VERSION,
           s_safeMode ? " SAFE" : "");
  s_lastEvent[0] = '\0';  // jamais supprimé
  logEvent("boot", detail);
  if (s_safeMode) {
    BOOT_LOG("[ContactDiag] MODE SANS ECHEC : %u boots consecutifs sans contact sain (OTA et automatismes non essentiels suspendus)\n",
             (unsigned)s_failedBoots);
  }
  BOOT_LOG("[ContactDiag] %s | %u evenement(s) en attente d'acquittement\n", detail, (unsigned)pendingCount());
}

void logEvent(const char* event, const char* detail) {
  if (!event || !ring()) return;
  const uint32_t now = millis();
  if (s_lastEvent[0] != '\0' && strcmp(s_lastEvent, event) == 0 && (now - s_lastEventMs) < kRepeatSuppressMs) {
    return;  // anti-rafale (ex. http_fail en boucle)
  }
  strncpy(s_lastEvent, event, sizeof(s_lastEvent) - 1);
  s_lastEvent[sizeof(s_lastEvent) - 1] = '\0';
  s_lastEventMs = now;
  const uint8_t index = ring()->head();
  ring()->push(event, detail, nowEpochOrZero(), now / 1000UL);
  if (s_began) persistSlot(index);
  BOOT_LOG("[ContactDiag] %s %s\n", event, detail ? detail : "");
}

void logEventf(const char* event, const char* fmt, ...) {
  char detail[n3evt::kDetailChars];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof(detail), fmt, ap);
  va_end(ap);
  logEvent(event, detail);
}

bool isSafeMode() { return s_safeMode; }
uint8_t failedBoots() { return s_failedBoots; }
bool isBootHealthy() { return s_healthy; }

void markBootHealthy() {
  if (s_healthy) return;
  s_healthy = true;
  s_failedBoots = 0;
  g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, kKeyFailedBoots, 0);
  logEventf("contact_ok", "uptime=%lus%s", millis() / 1000UL, s_safeMode ? " (safe mode, reboot requis)" : "");
}

void setWifiConnectStats(uint32_t connectMs, uint16_t attempts) {
  s_wifiConnectMs = connectMs;
  s_wifiAttempts = attempts;
}

void onServerTime(uint32_t serverEpoch, uint32_t rttMs) {
  if (serverEpoch < 1704067200UL) return;  // réponse implausible
  const uint32_t deviceNow = (uint32_t)time(nullptr);
  s_offsetS = n3clock::serverOffsetSeconds(deviceNow, serverEpoch, rttMs);
  s_hasOffset = true;
  if (!power.hasTrustedNtpTime()) {
    // Pas de NTP : l'heure serveur devient la référence (PowerManager journalise l'application).
    power.applyServerEpoch(serverEpoch, rttMs);
    return;
  }
  const int32_t absOff = s_offsetS < 0 ? -s_offsetS : s_offsetS;
  const int32_t lastAbs = s_lastLoggedDriftS < 0 ? -s_lastLoggedDriftS : s_lastLoggedDriftS;
  if (absOff > 60 && (lastAbs == 0 || absOff > lastAbs + 30 || absOff + 30 < lastAbs)) {
    s_lastLoggedDriftS = s_offsetS;
    logEventf("clock_drift", "offset=%+lds ntp=1", (long)s_offsetS);
  }
}

bool hasClockOffset() { return s_hasOffset; }
int32_t clockOffsetSeconds() { return s_offsetS; }

const char* resetReasonLabel() { return ResetReason::resetReasonToString(s_resetReason); }
const char* partitionLabel() { return s_partition; }
const char* otaStateLabel() { return n3boot::otaStateLabel(s_otaState); }

size_t appendHeartbeatExtras(char* buf, size_t size) {
  if (!buf || size == 0) return 0;
  size_t len = strlen(buf);
#define add(...) appendf(buf, size, len, __VA_ARGS__)
  const int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  readPartitionState();  // l'état OTA peut passer pending → valid pendant le boot
  add("&rssi=%d&version=%s&rst=%s&part=%s&ota=%s&fails=%u&safe=%u", rssi, ProjectConfig::VERSION,
      resetReasonLabel(), s_partition, otaStateLabel(), (unsigned)s_failedBoots, s_safeMode ? 1U : 0U);
  if (s_hasOffset) add("&clk=%ld", (long)s_offsetS);
  if (s_wifiConnectMs > 0) add("&wifi_ms=%lu&wifi_tries=%u", (unsigned long)s_wifiConnectMs, (unsigned)s_wifiAttempts);
  add("&board=%s", OtaModel::MODEL);
  if (s_bootFlagPending) {
    add("&boot=1");
    s_bootFlagPending = false;
  }
  if (ring() && ring()->pendingCount() > 0 && len + 8 < size) {
    size_t budget = size - len - 6;  // "&log=" + NUL
    if (budget > kHeartbeatLogMaxChars) budget = kHeartbeatLogMaxChars;
    char json[kHeartbeatLogMaxChars + 1];
    if (ring()->toJson(json, budget > sizeof(json) ? sizeof(json) : budget) > 0) {
      add("&log=%s", json);
    }
  }
#undef add
  return len;
}

uint8_t pendingCount() { return ring() ? ring()->pendingCount() : 0; }

bool flushPending(bool force) {
  if (!ring() || ring()->pendingCount() == 0) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!force && !power.hasTrustedNtpTime()) return false;  // le serveur exige une signature valide

  char events[700];
  uint32_t lastSeq = 0;
  const uint8_t n = ring()->toJson(events, sizeof(events), &lastSeq);
  if (n == 0) return true;
  char payload[900];
  snprintf(payload, sizeof(payload), "src=ota_report&board=%s&version=%s&uptime=%lu&events=%s",
           OtaModel::MODEL, ProjectConfig::VERSION, millis() / 1000UL, events);
  char url[256];
  ServerConfig::getOtaReportUrl(url, sizeof(url));
  char resp[384];
  if (!web.postToUrlWithResponse(url, payload, resp, sizeof(resp), NetworkConfig::HTTP_TIMEOUT_MS)) {
    return false;
  }
  const uint32_t acked = parseMaxAcked(resp);
  if (acked == 0) return false;
  for (uint8_t i = 0; i < kRingCapacity; ++i) {
    const n3evt::Event& e = ring()->slot(i);
    if (e.seq != 0 && e.seq <= acked) {
      ring()->ackUpTo(e.seq);  // slot par slot pour ne persister que ceux vidés
      persistSlot(i);
    }
  }
  BOOT_LOG("[ContactDiag] /ota-report : %u evenement(s) envoye(s), acquittes jusqu'a #%lu, reste %u\n",
           (unsigned)n, (unsigned long)acked, (unsigned)ring()->pendingCount());
  return true;
}

}  // namespace ContactDiag
