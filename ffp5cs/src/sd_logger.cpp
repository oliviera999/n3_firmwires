/**
 * @file sd_logger.cpp
 * @brief Implémentation stockage POSTs sur carte SD (BOARD_S3 uniquement).
 */

#include "sd_logger.h"
#include "sd_card.h"
#include "boot_log.h"

#if defined(BOARD_S3)

#include <SD.h>
#include <cstdio>
#include <cstring>
#include <time.h>
#include "config.h"
#include "app_tasks.h"
#include <WiFi.h>
#include <esp_task_wdt.h>

namespace {

// Chemins relatifs au point de montage (SD.begin utilise "/sdcard") :
// ne pas préfixer par /sdcard sinon la lib produit /sdcard/sdcard/...
constexpr const char* LOG_DIR   = "/log";
constexpr const char* QUEUE_DIR = "/queue";
constexpr const char* META_FILE = "/meta.txt";

static uint32_t s_nextSeq = 1;
static bool s_ready = false;

static void ensureDir(const char* path) {
  if (!SD.exists(path)) {
    SD.mkdir(path);
  }
}

static bool loadMeta() {
  File f = SD.open(META_FILE, FILE_READ);
  if (!f) {
    s_nextSeq = 1;
    return false;
  }
  char buf[16];
  size_t n = f.readBytes(buf, sizeof(buf) - 1);
  buf[n] = '\0';
  f.close();
  s_nextSeq = (uint32_t)atol(buf);
  if (s_nextSeq == 0) s_nextSeq = 1;
  return true;
}

static bool saveMeta() {
  File f = SD.open(META_FILE, FILE_WRITE);
  if (!f) return false;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_nextSeq);
  f.print(buf);
  f.close();
  return true;
}

static void dateStr(uint32_t epoch, char* out, size_t outSize) {
  time_t t = (time_t)epoch;
  struct tm ti;
  gmtime_r(&t, &ti);
  snprintf(out, outSize, "%04d%02d%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
}

static void seqToPath(uint32_t seq, char* out, size_t outSize) {
  snprintf(out, outSize, "%s/%06lu.dat", QUEUE_DIR, (unsigned long)seq);
}

}  // namespace

namespace SdLogger {

bool begin() {
  if (!SdCard::isPresent()) return false;
  ensureDir(LOG_DIR);
  ensureDir(QUEUE_DIR);
  loadMeta();
  s_ready = true;
  BOOT_LOG("[SdLog] Prêt (seq=%lu)\n", (unsigned long)s_nextSeq);
  return true;
}

QueueResult logAndQueue(const char* payload, uint32_t epochSec) {
  QueueResult res{false, 0};
  if (!s_ready || !payload || payload[0] == '\0') return res;

  char dateBuf[16];
  dateStr(epochSec, dateBuf, sizeof(dateBuf));

  // 1. Append au fichier log journalier
  char logPath[48];
  snprintf(logPath, sizeof(logPath), "%s/%s.csv", LOG_DIR, dateBuf);
  File logFile = SD.open(logPath, FILE_APPEND);
  if (logFile) {
    logFile.printf("%lu;%s\n", (unsigned long)epochSec, payload);
    logFile.close();
  }

  // 2. Écrire le fichier queue
  uint32_t seq = s_nextSeq++;
  saveMeta();

  char queuePath[48];
  seqToPath(seq, queuePath, sizeof(queuePath));
  File qFile = SD.open(queuePath, FILE_WRITE);
  if (!qFile) return res;
  qFile.print(payload);
  qFile.close();

  res.ok = true;
  res.seqNum = seq;
  Serial.printf("[SdLog] Queued #%lu (%s)\n", (unsigned long)seq, dateBuf);
  return res;
}

bool markSent(uint32_t seqNum) {
  if (!s_ready) return false;
  char path[48];
  seqToPath(seqNum, path, sizeof(path));
  if (SD.exists(path)) {
    return SD.remove(path);
  }
  return true;
}

uint16_t replayPending(uint8_t maxBatch) {
  if (!s_ready) return 0;
  if (WiFi.status() != WL_CONNECTED) return 0;

  File dir = SD.open(QUEUE_DIR);
  if (!dir || !dir.isDirectory()) return 0;

  uint16_t sent = 0;
  File entry;
  while ((entry = dir.openNextFile()) && sent < maxBatch) {
    if (entry.isDirectory()) { entry.close(); continue; }

    size_t sz = entry.size();
    if (sz == 0 || sz >= BufferConfig::POST_PAYLOAD_MAX_SIZE) {
      entry.close();
      continue;
    }

    char payload[BufferConfig::POST_PAYLOAD_MAX_SIZE];
    size_t rd = entry.readBytes(payload, sizeof(payload) - 1);
    payload[rd] = '\0';
    const char* name = entry.name();
    entry.close();

    if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();

    bool ok = AppTasks::netPostRaw(payload, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS);
    if (ok) {
      char fullPath[64];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", QUEUE_DIR, name);
      SD.remove(fullPath);
      sent++;
      Serial.printf("[SdLog] Replay OK: %s\n", name);
    } else {
      Serial.printf("[SdLog] Replay fail: %s, arrêt batch\n", name);
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
  dir.close();
  return sent;
}

uint16_t pendingCount() {
  if (!s_ready) return 0;
  File dir = SD.open(QUEUE_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  uint16_t count = 0;
  File entry;
  while ((entry = dir.openNextFile())) {
    if (!entry.isDirectory()) count++;
    entry.close();
  }
  dir.close();
  return count;
}

bool readHistory(const char* date, char* outBuf, size_t bufSize, uint32_t offset, uint32_t limit) {
  if (!s_ready || !date || !outBuf || bufSize < 4) return false;

  char logPath[48];
  snprintf(logPath, sizeof(logPath), "%s/%s.csv", LOG_DIR, date);
  File f = SD.open(logPath, FILE_READ);
  if (!f) return false;

  size_t pos = 0;
  pos += snprintf(outBuf + pos, bufSize - pos, "[");

  uint32_t lineNum = 0;
  uint32_t written = 0;
  char lineBuf[1200];

  while (f.available() && written < limit && pos < bufSize - 20) {
    size_t len = 0;
    int c;
    while ((c = f.read()) >= 0 && c != '\n' && len < sizeof(lineBuf) - 1) {
      lineBuf[len++] = (char)c;
    }
    lineBuf[len] = '\0';
    if (len == 0) continue;

    if (lineNum++ < offset) continue;

    char* semi = strchr(lineBuf, ';');
    if (!semi) continue;
    *semi = '\0';
    const char* ts = lineBuf;
    const char* payload = semi + 1;

    if (written > 0 && pos < bufSize - 2) {
      outBuf[pos++] = ',';
    }

    int added = snprintf(outBuf + pos, bufSize - pos,
      "{\"ts\":%s,\"d\":\"%s\"}", ts, payload);
    if (added < 0 || (size_t)added >= bufSize - pos - 2) break;
    pos += (size_t)added;
    written++;
  }
  f.close();

  if (pos < bufSize - 2) {
    outBuf[pos++] = ']';
    outBuf[pos] = '\0';
  } else {
    outBuf[bufSize - 2] = ']';
    outBuf[bufSize - 1] = '\0';
  }
  return true;
}

void rotateLogs(uint16_t keepDays) {
  if (!s_ready) return;

  time_t now;
  time(&now);
  if (now < 1700000000) return;
  time_t cutoff = now - (time_t)keepDays * 86400;

  File dir = SD.open(LOG_DIR);
  if (!dir || !dir.isDirectory()) return;

  File entry;
  while ((entry = dir.openNextFile())) {
    if (entry.isDirectory()) { entry.close(); continue; }
    const char* name = entry.name();
    entry.close();

    // Parse YYYYMMDD from filename
    if (strlen(name) < 8) continue;
    struct tm ft = {};
    ft.tm_year = atoi(name) / 10000 - 1900;
    ft.tm_mon  = (atoi(name) / 100) % 100 - 1;
    ft.tm_mday = atoi(name) % 100;
    time_t fileDate = mktime(&ft);
    if (fileDate > 0 && fileDate < cutoff) {
      char fullPath[64];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", LOG_DIR, name);
      SD.remove(fullPath);
      Serial.printf("[SdLog] Rotation: supprimé %s\n", name);
    }
  }
  dir.close();
}

}  // namespace SdLogger

#else  // !BOARD_S3

namespace SdLogger {

bool begin() { return false; }
QueueResult logAndQueue(const char*, uint32_t) { return {false, 0}; }
bool markSent(uint32_t) { return false; }
uint16_t replayPending(uint8_t) { return 0; }
uint16_t pendingCount() { return 0; }
bool readHistory(const char*, char*, size_t, uint32_t, uint32_t) { return false; }
void rotateLogs(uint16_t) {}

}  // namespace SdLogger

#endif  // BOARD_S3
