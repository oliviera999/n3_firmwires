#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TaskMonitor {

struct Entry {
  eTaskState state;
  uint32_t highWaterMarkWords;
};

struct Snapshot {
  Entry sensor;
  Entry web;
  Entry automation;
  Entry display;
};

Snapshot collectSnapshot();
void logSnapshot(const Snapshot& snapshot, const char* stage);
void logDiff(const Snapshot& before, const Snapshot& after, const char* stage);
bool detectAnomalies(const Snapshot& snapshot, const char* stage);

}  // namespace TaskMonitor


