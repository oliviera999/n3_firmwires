// Harnais OTA périodique + écran OLED mutualisé (voir n3_ota_ui.h).
// Corps extrait verbatim de n3pp/src/main.cpp et msp/src/main.cpp (copies
// identiques hors titre d'écran et URLs) — tranche T4.2 du chantier core
// shared. Aucun changement observable : mêmes écrans, mêmes logs série,
// même cadence.
#include "n3_ota_ui.h"
#include "n3_ota_ui_logic.h"

#include <stdio.h>
#include <string.h>

static void renderOtaScreen(N3OtaUiContext& ctx, const char* statusLine, uint8_t percent) {
  Adafruit_SSD1306* display = ctx.cfg.display;
  if (display == nullptr || ctx.cfg.displayOk == nullptr || !*ctx.cfg.displayOk) return;

  const int screenWidth = display->width();
  display->clearDisplay();
  display->setTextColor(WHITE);
  display->setTextSize(1);
  display->setCursor(0, 0);
  display->println(ctx.cfg.title ? ctx.cfg.title : "OTA");
  display->setCursor(0, 10);
  display->println(statusLine ? statusLine : "En cours");

  display->setCursor(0, 20);
  display->print(ctx.currentVersion[0] ? ctx.currentVersion : ctx.cfg.firmwareVersion);
  display->print(" -> ");
  display->println(ctx.remoteVersion[0] ? ctx.remoteVersion : "?");

  display->drawRect(0, 36, screenWidth, 10, WHITE);
  const int fillWidth = n3OtaUiProgressFillWidth(screenWidth, percent);
  if (fillWidth > 0) {
    display->fillRect(1, 37, fillWidth, 8, WHITE);
  }

  display->setCursor(0, 50);
  display->print("Progression: ");
  display->print(percent);
  display->println("%");
  display->display();
}

static void otaUiStartCallback(const char* currentVersion, const char* remoteVersion,
                               const char* firmwareUrl, void* userData) {
  (void)firmwareUrl;
  N3OtaUiContext& ctx = *static_cast<N3OtaUiContext*>(userData);
  snprintf(ctx.currentVersion, sizeof(ctx.currentVersion), "%s",
           currentVersion ? currentVersion : ctx.cfg.firmwareVersion);
  snprintf(ctx.remoteVersion, sizeof(ctx.remoteVersion), "%s",
           remoteVersion ? remoteVersion : "?");
  ctx.displayedPercent = 255;
  renderOtaScreen(ctx, "MAJ disponible", 0);
}

static void otaUiEndCallback(bool success, const char* details, void* userData) {
  N3OtaUiContext& ctx = *static_cast<N3OtaUiContext*>(userData);
  if (success) {
    renderOtaScreen(ctx, "Succes - reboot", 100);
    return;
  }

  const bool upToDate = n3OtaUiEndIsUpToDate(details);
  renderOtaScreen(ctx, upToDate ? "Deja a jour" : "Echec OTA",
                  n3OtaUiEndPercent(upToDate, ctx.displayedPercent));
}

static void otaUiProgressCallback(int current, int total, uint8_t percent, void* userData) {
  (void)current;
  (void)total;
  N3OtaUiContext& ctx = *static_cast<N3OtaUiContext*>(userData);
  if (percent == ctx.displayedPercent) return;
  ctx.displayedPercent = percent;
  renderOtaScreen(ctx, "Telechargement", percent);
}

void n3OtaUiInit(N3OtaUiContext& ctx, const N3OtaUiConfig& cfg) {
  ctx.cfg = cfg;
  ctx.currentVersion[0] = '\0';
  ctx.remoteVersion[0] = '\0';
  ctx.displayedPercent = 255;
}

static bool runOtaCheck(N3OtaUiContext& ctx) {
  const N3OtaConfig otaConfig = {
      ctx.cfg.useTestUrl ? ctx.cfg.testMetadataUrl : ctx.cfg.prodMetadataUrl,
      ctx.cfg.firmwareVersion, -1, nullptr,
      otaUiStartCallback, otaUiEndCallback,
      &ctx, otaUiProgressCallback
  };
  return n3OtaCheck(otaConfig);
}

bool n3OtaUiCheckNow(N3OtaUiContext& ctx) {
  return runOtaCheck(ctx);
}

void n3OtaUiMaybePeriodicCheck(N3OtaUiContext& ctx, const char* reason) {
  uint32_t* elapsed = ctx.cfg.elapsedSecondsRtc;
  if (elapsed == nullptr) return;

  if (!OtaPeriodic::due(*elapsed, ctx.cfg.intervalSeconds)) {
    const uint32_t remaining = OtaPeriodic::remainingSeconds(*elapsed, ctx.cfg.intervalSeconds);
    Serial.printf("[OTA] check 2h ignore (%s), restant=%lu s\n",
                  reason ? reason : "n/a",
                  static_cast<unsigned long>(remaining));
    return;
  }

  Serial.printf("[OTA] check 2h declenche (%s)\n", reason ? reason : "n/a");
  runOtaCheck(ctx);
  *elapsed = 0;
}

void n3OtaUiAccumulateElapsed(N3OtaUiContext& ctx, int elapsedSeconds) {
  if (elapsedSeconds <= 0) return;
  uint32_t* elapsed = ctx.cfg.elapsedSecondsRtc;
  if (elapsed == nullptr) return;
  *elapsed = OtaPeriodic::accumulate(*elapsed, ctx.cfg.intervalSeconds,
                                     static_cast<uint32_t>(elapsedSeconds));
}
