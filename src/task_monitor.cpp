#include "task_monitor.h"
#include "app_tasks.h"
#include "config.h"

namespace TaskMonitor {

// Helper pour récupérer les stats d'une tâche
static TaskStats getTaskStats(const char* name, TaskHandle_t handle) {
  TaskStats stats;
  stats.name = name;
  if (handle) {
    stats.state = eTaskGetState(handle);
    stats.highWaterMark = uxTaskGetStackHighWaterMark(handle);
  } else {
    stats.state = eDeleted;
    stats.highWaterMark = 0;
  }
  return stats;
}

Snapshot collectSnapshot() {
  Snapshot snap;
  AppTasks::Handles handles = AppTasks::getHandles();
  
  snap.sensor = getTaskStats("Sensor", handles.sensor);
  snap.web = getTaskStats("Web", handles.web);
  snap.automation = getTaskStats("Auto", handles.automation);
  snap.display = getTaskStats("Display", handles.display);
  
  return snap;
}

void logSnapshot(const Snapshot& s, const char* stage) {
  // Log compact en une ligne
  LOG_INFO("Tasks [%s] HWM: S=%u W=%u A=%u D=%u", 
           stage, 
           s.sensor.highWaterMark, 
           s.web.highWaterMark, 
           s.automation.highWaterMark, 
           s.display.highWaterMark);
}

void logDiff(const Snapshot& before, const Snapshot& after, const char* stage) {
  // Calculer les deltas uniquement si pertinent
  int dS = (int)after.sensor.highWaterMark - (int)before.sensor.highWaterMark;
  int dW = (int)after.web.highWaterMark - (int)before.web.highWaterMark;
  int dA = (int)after.automation.highWaterMark - (int)before.automation.highWaterMark;
  int dD = (int)after.display.highWaterMark - (int)before.display.highWaterMark;
  
  LOG_INFO("Tasks Diff [%s]: S=%d W=%d A=%d D=%d", stage, dS, dW, dA, dD);
}

bool detectAnomalies(const Snapshot& s, const char* stage) {
  bool anomaly = false;
  const uint32_t LOW_STACK_THRESHOLD = 100; // Mots
  
  auto check = [&](const TaskStats& t) {
    if (t.state != eDeleted && t.highWaterMark < LOW_STACK_THRESHOLD) {
      LOG_WARN("Task Alert [%s]: %s low stack (%u)", stage, t.name, t.highWaterMark);
      anomaly = true;
    }
  };
  
  check(s.sensor);
  check(s.web);
  check(s.automation);
  check(s.display);
  
  return anomaly;
}

} // namespace TaskMonitor
