#include "task_monitor.h"

#include <Arduino.h>

#include "app_tasks.h"
#include "event_log.h"
#include "project_config.h"

namespace {

const char* taskStateToString(eTaskState state) {
  switch (state) {
    case eRunning:    return "running";
    case eReady:      return "ready";
    case eBlocked:    return "blocked";
    case eSuspended:  return "suspended";
    case eDeleted:    return "deleted";
    default:          return "unknown";
  }
}

constexpr uint32_t toBytes(uint32_t words) {
  return words * sizeof(StackType_t);
}

void logTaskEntry(const char* stage,
                  const char* name,
                  const TaskMonitor::Entry& entry,
                  uint32_t configuredWords) {
  uint32_t configuredBytes = toBytes(configuredWords);
  uint32_t hwmBytes = toBytes(entry.highWaterMarkWords);
  uint32_t usedBytes = configuredBytes > hwmBytes ? configuredBytes - hwmBytes : 0;
  Serial.printf("[TaskMonitor] %s %s state=%s hwm=%u words (%u bytes) used=%u/%u bytes\n",
                stage,
                name,
                taskStateToString(entry.state),
                entry.highWaterMarkWords,
                hwmBytes,
                usedBytes,
                configuredBytes);
}

}  // namespace

namespace TaskMonitor {

Snapshot collectSnapshot() {
  Snapshot snapshot{};
  AppTasks::Handles handles = AppTasks::getHandles();

  snapshot.sensor.state = handles.sensor ? eTaskGetState(handles.sensor) : eDeleted;
  snapshot.sensor.highWaterMarkWords = handles.sensor ? uxTaskGetStackHighWaterMark(handles.sensor) : 0;

  snapshot.web.state = handles.web ? eTaskGetState(handles.web) : eDeleted;
  snapshot.web.highWaterMarkWords = handles.web ? uxTaskGetStackHighWaterMark(handles.web) : 0;

  snapshot.automation.state = handles.automation ? eTaskGetState(handles.automation) : eDeleted;
  snapshot.automation.highWaterMarkWords = handles.automation ? uxTaskGetStackHighWaterMark(handles.automation) : 0;

  snapshot.display.state = handles.display ? eTaskGetState(handles.display) : eDeleted;
  snapshot.display.highWaterMarkWords = handles.display ? uxTaskGetStackHighWaterMark(handles.display) : 0;

  return snapshot;
}

void logSnapshot(const Snapshot& snapshot, const char* stage) {
  logTaskEntry(stage, "sensorTask", snapshot.sensor, TaskConfig::SENSOR_TASK_STACK_SIZE);
  logTaskEntry(stage, "webTask", snapshot.web, TaskConfig::WEB_TASK_STACK_SIZE);
  logTaskEntry(stage, "autoTask", snapshot.automation, TaskConfig::AUTOMATION_TASK_STACK_SIZE);
  logTaskEntry(stage, "displayTask", snapshot.display, TaskConfig::DISPLAY_TASK_STACK_SIZE);

  char summary[196];
  snprintf(summary,
           sizeof(summary),
           "Task snapshot %s | sensor=%u web=%u auto=%u display=%u",
           stage,
           snapshot.sensor.highWaterMarkWords,
           snapshot.web.highWaterMarkWords,
           snapshot.automation.highWaterMarkWords,
           snapshot.display.highWaterMarkWords);
  EventLog::add(summary);
}

void logDiff(const Snapshot& before, const Snapshot& after, const char* stage) {
  int32_t deltaSensor = static_cast<int32_t>(after.sensor.highWaterMarkWords) -
                        static_cast<int32_t>(before.sensor.highWaterMarkWords);
  int32_t deltaWeb = static_cast<int32_t>(after.web.highWaterMarkWords) -
                     static_cast<int32_t>(before.web.highWaterMarkWords);
  int32_t deltaAuto = static_cast<int32_t>(after.automation.highWaterMarkWords) -
                      static_cast<int32_t>(before.automation.highWaterMarkWords);
  int32_t deltaDisplay = static_cast<int32_t>(after.display.highWaterMarkWords) -
                         static_cast<int32_t>(before.display.highWaterMarkWords);

  Serial.printf("[TaskMonitor] %s diff sensor=%d web=%d auto=%d display=%d (words)\n",
                stage,
                deltaSensor,
                deltaWeb,
                deltaAuto,
                deltaDisplay);

  char summary[196];
  snprintf(summary,
           sizeof(summary),
           "Task diff %s | sensor=%d web=%d auto=%d display=%d",
           stage,
           deltaSensor,
           deltaWeb,
           deltaAuto,
           deltaDisplay);
  EventLog::add(summary);
}

bool detectAnomalies(const Snapshot& snapshot, const char* stage) {
  bool hasAnomaly = false;

  auto checkEntry = [&](const char* name, const Entry& entry, uint32_t stackWords) {
    if (entry.state == eDeleted || entry.state == eInvalid) {
      char msg[160];
      snprintf(msg,
               sizeof(msg),
               "Task anomaly %s %s state=%s",
               stage,
               name,
               taskStateToString(entry.state));
      EventLog::add(msg);
      Serial.println(msg);
      hasAnomaly = true;
    }
    if (entry.highWaterMarkWords == 0) {
      char msg[160];
      snprintf(msg,
               sizeof(msg),
               "Task anomaly %s %s stack exhausted (configured=%u words)",
               stage,
               name,
               stackWords);
      EventLog::add(msg);
      Serial.println(msg);
      hasAnomaly = true;
    }
  };

  checkEntry("sensorTask", snapshot.sensor, TaskConfig::SENSOR_TASK_STACK_SIZE);
  checkEntry("webTask", snapshot.web, TaskConfig::WEB_TASK_STACK_SIZE);
  checkEntry("autoTask", snapshot.automation, TaskConfig::AUTOMATION_TASK_STACK_SIZE);
  checkEntry("displayTask", snapshot.display, TaskConfig::DISPLAY_TASK_STACK_SIZE);

  return hasAnomaly;
}

}  // namespace TaskMonitor


