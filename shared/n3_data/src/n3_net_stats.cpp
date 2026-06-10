#include "n3_data.h"
#include "n3_defaults.h"

namespace {

struct N3NetStatsInternal {
  uint32_t postCount;
  uint32_t postOkCount;
  uint32_t postFailCount;
  uint32_t postLastDurationMs;
  uint32_t postMaxDurationMs;
  uint32_t postSumDurationMs;
  uint32_t postNearTimeoutCount;
  int postLastCode;
  int postLastRssi;
  uint32_t getCount;
  uint32_t getOkCount;
  uint32_t getFailCount;
  uint32_t getLastDurationMs;
  uint32_t getMaxDurationMs;
  uint32_t getSumDurationMs;
  int getLastCode;
  int getLastRssi;
};

N3NetStatsInternal s_stats = {};

bool isHttpOk(int code) { return code >= 200 && code < 400; }

void bumpMax(uint32_t& maxVal, uint32_t value) {
  if (value > maxVal) maxVal = value;
}

}  // namespace

void n3NetStatsRecordPost(int httpCode, unsigned long durationMs, int rssi) {
  const uint32_t dur = static_cast<uint32_t>(durationMs);
  ++s_stats.postCount;
  if (isHttpOk(httpCode)) {
    ++s_stats.postOkCount;
  } else {
    ++s_stats.postFailCount;
  }
  s_stats.postLastDurationMs = dur;
  s_stats.postSumDurationMs += dur;
  bumpMax(s_stats.postMaxDurationMs, dur);
  s_stats.postLastCode = httpCode;
  s_stats.postLastRssi = rssi;
  if (dur >= (unsigned long)N3_HTTP_TIMEOUT_MS - 500UL) {
    ++s_stats.postNearTimeoutCount;
  }
}

void n3NetStatsRecordGet(int httpCode, unsigned long durationMs, int rssi) {
  const uint32_t dur = static_cast<uint32_t>(durationMs);
  ++s_stats.getCount;
  if (isHttpOk(httpCode)) {
    ++s_stats.getOkCount;
  } else {
    ++s_stats.getFailCount;
  }
  s_stats.getLastDurationMs = dur;
  s_stats.getSumDurationMs += dur;
  bumpMax(s_stats.getMaxDurationMs, dur);
  s_stats.getLastCode = httpCode;
  s_stats.getLastRssi = rssi;
}

void n3NetStatsGetSnapshot(N3NetStatsSnapshot& out) {
  out.postCount = s_stats.postCount;
  out.postOkCount = s_stats.postOkCount;
  out.postFailCount = s_stats.postFailCount;
  out.postLastDurationMs = s_stats.postLastDurationMs;
  out.postMaxDurationMs = s_stats.postMaxDurationMs;
  out.postAvgDurationMs =
      (s_stats.postCount > 0) ? (s_stats.postSumDurationMs / s_stats.postCount) : 0;
  out.postNearTimeoutCount = s_stats.postNearTimeoutCount;
  out.postLastCode = s_stats.postLastCode;
  out.postLastRssi = s_stats.postLastRssi;
  out.getCount = s_stats.getCount;
  out.getOkCount = s_stats.getOkCount;
  out.getFailCount = s_stats.getFailCount;
  out.getLastDurationMs = s_stats.getLastDurationMs;
  out.getMaxDurationMs = s_stats.getMaxDurationMs;
  out.getLastCode = s_stats.getLastCode;
  out.getLastRssi = s_stats.getLastRssi;
}

void n3NetStatsResetPeriod() {
  s_stats = {};
}
