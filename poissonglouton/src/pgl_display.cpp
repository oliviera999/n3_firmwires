#include "pgl_display.h"

#include "config.h"
#include "pgl_log.h"

#if !PGL_HEADLESS

#include "pgl_ui.h"
#include "pgl_display_board.h"

#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

#if PGL_TOUCH_AXS15231B
#include "touch_axs15231b.h"
#endif

namespace {
constexpr int GFX_BL = PGL_GFX_BL;
constexpr uint16_t kScreenW = PGL_SCREEN_W;
constexpr uint16_t kScreenH = PGL_SCREEN_H;
constexpr uint32_t kLvglBufLines = 40;
constexpr uint8_t kQueueBarMaxEvents = 20;

#if PGL_PANEL_DRIVER_AXS15231B
// AXS15231B : transfert QSPI plein ecran ~50 ms — limiter pour eviter scintillement.
constexpr uint32_t kUiTickMs = 50;
constexpr uint32_t kPanelPushMinMs = 100;
#else
constexpr uint32_t kUiTickMs = 5;
constexpr uint32_t kPanelPushMinMs = 0;
#endif

bool gPanelPushPending = false;
uint32_t gLastPanelPushMs = 0;

#if PGL_DISPLAY_DEBUG
uint32_t gDispFlushCalls = 0;
uint32_t gDispFlushLast = 0;
uint32_t gDispPanelPush = 0;
uint32_t gDispPanelPushSkipped = 0;
uint32_t gDispLastStatsMs = 0;
#endif

Arduino_DataBus* bus = new Arduino_ESP32QSPI(
    PGL_QSPI_CS, PGL_QSPI_SCK, PGL_QSPI_D0, PGL_QSPI_D1, PGL_QSPI_D2, PGL_QSPI_D3);

#if PGL_PANEL_DRIVER_AXS15231B
#if defined(PGL_AX15231B_INIT_TYPE2) && PGL_AX15231B_INIT_TYPE2
Arduino_GFX* panel = new Arduino_AXS15231B(
    bus, GFX_NOT_DEFINED, 0, false, kScreenW, kScreenH, 0, 0, 0, 0,
    axs15231b_320480_type2_init_operations, sizeof(axs15231b_320480_type2_init_operations));
#else
Arduino_GFX* panel = new Arduino_AXS15231B(
    bus, GFX_NOT_DEFINED, 0, false, kScreenW, kScreenH, 0, 0, 0, 0,
    axs15231b_320480_type1_init_operations, sizeof(axs15231b_320480_type1_init_operations));
#endif
Arduino_GFX* gfx = new Arduino_Canvas(kScreenW, kScreenH, panel, 0, 0, 0);
#else
Arduino_GFX* panel = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);
Arduino_GFX* gfx = new Arduino_Canvas(kScreenW, kScreenH, panel);
#endif

lv_disp_draw_buf_t drawBuf;
lv_color_t* drawBuffer = nullptr;
PglUiHandles ui;
uint32_t lastUiUpdateMs = 0;
int lastWifiRssi_ = -100;
bool wifiConnected_ = false;

int mapRssiToBar(int rssi) {
  if (rssi <= -90) return 0;
  if (rssi >= -30) return 100;
  return (rssi + 90) * 100 / 60;
}

int mapQueueToBar(uint16_t pendingEvents) {
  if (pendingEvents >= kQueueBarMaxEvents) return 100;
  return (static_cast<int>(pendingEvents) * 100) / kQueueBarMaxEvents;
}

void blitLvglAreaToCanvas(int16_t x, int16_t y, uint16_t* colorP, int16_t w, int16_t h) {
#if LV_COLOR_16_SWAP
  gfx->draw16bitBeRGBBitmap(x, y, colorP, w, h);
#else
  gfx->draw16bitRGBBitmap(x, y, colorP, w, h);
#endif
}

void pushCanvasToPanel() {
  gfx->flush();
}

void tryPushCanvasToPanel(bool force = false) {
  if (!gPanelPushPending && !force) {
    return;
  }
  const uint32_t now = millis();
  if (!force && kPanelPushMinMs > 0 && (now - gLastPanelPushMs) < kPanelPushMinMs) {
#if PGL_DISPLAY_DEBUG
    ++gDispPanelPushSkipped;
#endif
    return;
  }
  gPanelPushPending = false;
  gLastPanelPushMs = now;
#if PGL_DISPLAY_DEBUG
  const uint32_t t0 = now;
  ++gDispPanelPush;
#endif
  pushCanvasToPanel();
#if PGL_DISPLAY_DEBUG
  if (gDispPanelPush <= 12 || (gDispPanelPush % 30 == 0)) {
    PGL_LOG_DISP("panel push #%lu %lums (skipped=%lu)",
                 static_cast<unsigned long>(gDispPanelPush),
                 static_cast<unsigned long>(millis() - t0),
                 static_cast<unsigned long>(gDispPanelPushSkipped));
  }
#endif
}

#if PGL_DISPLAY_DEBUG
void logFramebufferSample(const char* tag) {
  auto* canvas = static_cast<Arduino_Canvas*>(gfx);
  uint16_t* fb = canvas ? canvas->getFramebuffer() : nullptr;
  if (!fb) {
    PGL_LOG_DISP("%s: framebuffer NULL", tag);
    return;
  }
  const size_t pixels = static_cast<size_t>(kScreenW) * kScreenH;
  const bool inPsram = esp_ptr_external_ram(fb);
  PGL_LOG_DISP("%s: fb=%p psram=%d px=%u [0]=%04x mid=%04x end=%04x",
               tag,
               static_cast<void*>(fb),
               inPsram ? 1 : 0,
               static_cast<unsigned>(pixels),
               fb[0],
               fb[pixels / 2],
               fb[pixels - 1]);
}

void runDisplaySelfTest() {
  PGL_LOG_DISP("Self-test: bandes R/V/B/blanc (%ux%u) avant LVGL", kScreenW, kScreenH);
  const int16_t bandH = static_cast<int16_t>(kScreenH / 4);
  gfx->fillScreen(0x0000);
  gfx->fillRect(0, 0, kScreenW, bandH, 0xF800);
  gfx->fillRect(0, bandH, kScreenW, bandH, 0x07E0);
  gfx->fillRect(0, bandH * 2, kScreenW, bandH, 0x001F);
  gfx->fillRect(0, bandH * 3, kScreenW, kScreenH - bandH * 3, 0xFFFF);
  const uint32_t t0 = millis();
  pushCanvasToPanel();
  PGL_LOG_DISP("Self-test: panel push %lums", static_cast<unsigned long>(millis() - t0));
  logFramebufferSample("Self-test");
  delay(2500);
}

void maybeLogDisplayStats() {
  const uint32_t now = millis();
  if (now - gDispLastStatsMs < 30000) {
    return;
  }
  gDispLastStatsMs = now;
  PGL_LOG_DISP("Stats 30s: flush=%lu last=%lu push=%lu skip=%lu heap=%lu psram=%lu",
               static_cast<unsigned long>(gDispFlushCalls),
               static_cast<unsigned long>(gDispFlushLast),
               static_cast<unsigned long>(gDispPanelPush),
               static_cast<unsigned long>(gDispPanelPushSkipped),
               static_cast<unsigned long>(ESP.getFreeHeap()),
               static_cast<unsigned long>(ESP.getFreePsram()));
}
#endif

void displayFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  blitLvglAreaToCanvas(area->x1, area->y1, reinterpret_cast<uint16_t*>(&colorP->full),
                       static_cast<int16_t>(w), static_cast<int16_t>(h));

  const bool isLast = lv_disp_flush_is_last(disp);
#if PGL_DISPLAY_DEBUG
  ++gDispFlushCalls;
  if (gDispFlushCalls <= 24 || (gDispFlushCalls % 60 == 0)) {
    PGL_LOG_DISP("flush #%lu area (%ld,%ld)-(%ld,%ld) %lux%lu last=%d",
                 static_cast<unsigned long>(gDispFlushCalls),
                 static_cast<long>(area->x1),
                 static_cast<long>(area->y1),
                 static_cast<long>(area->x2),
                 static_cast<long>(area->y2),
                 static_cast<unsigned long>(w),
                 static_cast<unsigned long>(h),
                 isLast ? 1 : 0);
  }
#endif

  if (isLast) {
#if PGL_DISPLAY_DEBUG
    ++gDispFlushLast;
#endif
    gPanelPushPending = true;
#if !PGL_PANEL_DRIVER_AXS15231B
    tryPushCanvasToPanel(true);
#endif
  }
  lv_disp_flush_ready(disp);
}

#if PGL_TOUCH_AXS15231B
void touchpadRead(lv_indev_drv_t* /*indev*/, lv_indev_data_t* data) {
  if (touch_axs_has_signal()) {
    if (touch_axs_touched()) {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touch_axs_last_x;
      data->point.y = touch_axs_last_y;
    } else if (touch_axs_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}
#endif

void flushUi() {
  lv_timer_handler();
  tryPushCanvasToPanel(false);
#if PGL_PANEL_DRIVER_AXS15231B
  // Pousser la derniere frame en attente si le throttle a retarde le push.
  if (gPanelPushPending &&
      (millis() - gLastPanelPushMs) >= (kPanelPushMinMs * 3)) {
    tryPushCanvasToPanel(true);
  }
#endif
#if PGL_DISPLAY_DEBUG
  maybeLogDisplayStats();
#endif
}
}  // namespace

void PglDisplay::refreshLabels() {
  if (ui.labelTodayValue) {
    lv_label_set_text_fmt(ui.labelTodayValue, "%lu", static_cast<unsigned long>(todayCount_));
  }
  if (ui.labelTotal) {
    lv_label_set_text_fmt(ui.labelTotal, "Total: %lu", static_cast<unsigned long>(totalCount_));
  }
  if (ui.labelUs) {
    lv_label_set_text(ui.labelUs, usLine_);
  }
  if (ui.labelServer) {
    lv_label_set_text(ui.labelServer, serverLine_);
  }
  if (ui.labelWifi) {
    lv_label_set_text(ui.labelWifi, wifiLine_);
  }
  if (ui.labelAudio) {
    lv_label_set_text(ui.labelAudio, audioLine_);
  }
}

void PglDisplay::begin() {
#if PGL_PANEL_DRIVER_AXS15231B
#if defined(PGL_AX15231B_INIT_TYPE2) && PGL_AX15231B_INIT_TYPE2
  PGL_LOG("Display: init AXS15231B %ux%u (%s) init=type2 QSPI BL=%d",
          kScreenW, kScreenH, PGL_DISPLAY_BOARD_NAME, GFX_BL);
#else
  PGL_LOG("Display: init AXS15231B %ux%u (%s) init=type1 QSPI BL=%d",
          kScreenW, kScreenH, PGL_DISPLAY_BOARD_NAME, GFX_BL);
#endif
  PGL_LOG_DISP("LVGL buf=%u lignes push_throttle=%ums ui_tick=%ums",
               kLvglBufLines, kPanelPushMinMs, kUiTickMs);
#else
  PGL_LOG("Display: init NV3041A %ux%u (%s) QSPI BL=%d",
          kScreenW, kScreenH, PGL_DISPLAY_BOARD_NAME, GFX_BL);
#endif

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  PGL_LOG_V("Display: backlight GPIO%d=HIGH", GFX_BL);

  gfxOk_ = gfx->begin();
  if (!gfxOk_) {
    PGL_LOG("Display: ERREUR gfx->begin() — panneau non repondu");
  } else {
    PGL_LOG("Display: gfx->begin() OK (%ux%u)", kScreenW, kScreenH);
  }
  gfx->fillScreen(0x0000);

#if PGL_DISPLAY_DEBUG
  runDisplaySelfTest();
#endif

  lv_init();
  const size_t bufPixels = static_cast<size_t>(kScreenW) * kLvglBufLines;
  drawBuffer = static_cast<lv_color_t*>(heap_caps_malloc(sizeof(lv_color_t) * bufPixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!drawBuffer) {
    drawBuffer = static_cast<lv_color_t*>(heap_caps_malloc(sizeof(lv_color_t) * bufPixels, MALLOC_CAP_8BIT));
  }
  if (!drawBuffer) {
    PGL_LOG("Display: ERREUR allocation buffer LVGL");
    lvglOk_ = false;
    ready_ = false;
    return;
  }
  lvglOk_ = true;
  PGL_LOG_V("Display: buffer LVGL %u octets (%u lignes)", static_cast<unsigned int>(sizeof(lv_color_t) * bufPixels),
            kLvglBufLines);
  PGL_LOG_V("Display: heap libre %u octets", static_cast<unsigned int>(ESP.getFreeHeap()));

  lv_disp_draw_buf_init(&drawBuf, drawBuffer, nullptr, bufPixels);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kScreenW;
  dispDrv.ver_res = kScreenH;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

#if PGL_TOUCH_AXS15231B
  touch_axs_init(static_cast<int16_t>(kScreenW), static_cast<int16_t>(kScreenH), gfx->getRotation());
  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchpadRead;
  lv_indev_drv_register(&indevDrv);
  PGL_LOG_V("Display: tactile AXS15231B I2C init (SDA=%d SCL=%d)", PGL_TOUCH_AXS_SDA, PGL_TOUCH_AXS_SCL);
#endif

  pglUiInitTheme();
  ui = pglUiBuildDashboard(lv_scr_act());

  snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: recherche...");
  snprintf(audioLine_, sizeof(audioLine_), "En attente");
  snprintf(usLine_, sizeof(usLine_), "US: -");
  snprintf(serverLine_, sizeof(serverLine_), "Srv: en attente");
  ready_ = gfxOk_ && lvglOk_;
  refreshLabels();

  for (int i = 0; i < 8; ++i) {
    flushUi();
    delay(5);
  }
  tryPushCanvasToPanel(true);
  if (ready_) {
    PGL_LOG("Display: FONCTIONNEL — dashboard LVGL actif (%ux%u %s)", kScreenW, kScreenH, PGL_DISPLAY_BOARD_NAME);
    PGL_LOG_V("Display: heap libre apres UI %u octets", static_cast<unsigned int>(ESP.getFreeHeap()));
  } else {
    PGL_LOG("Display: NON FONCTIONNEL (gfx=%s lvgl=%s)",
            gfxOk_ ? "OK" : "ECHEC",
            lvglOk_ ? "OK" : "ECHEC");
  }
}

void PglDisplay::update() {
  if (millis() - lastUiUpdateMs < kUiTickMs) return;
  lastUiUpdateMs = millis();
#if PGL_TOUCH_AXS15231B
  if (touch_axs_has_signal()) {
    wakeBacklight();
  }
#endif
  flushUi();
}

void PglDisplay::setCounter(uint32_t totalCount, uint32_t todayCount) {
  totalCount_ = totalCount;
  todayCount_ = todayCount;
  refreshLabels();
}

void PglDisplay::onBottleCount(uint32_t totalCount, uint32_t todayCount) {
  setCounter(totalCount, todayCount);
  lastCountAnimMs_ = millis();
  if (ui.labelSmiley) {
    lv_label_set_text(ui.labelSmiley, "^_^");
  }
  PGL_LOG_V("Display: +1 total=%lu today=%lu", static_cast<unsigned long>(totalCount),
            static_cast<unsigned long>(todayCount));
}

void PglDisplay::setWifiInfo(const char* ssid, wl_status_t status, int rssi) {
  wifiConnected_ = (status == WL_CONNECTED && ssid && ssid[0] != '\0');
  if (wifiConnected_) {
    lastWifiRssi_ = rssi;
    snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: %s (%d dBm)", ssid, rssi);
    pglUiSetLed(ui.ledWifi, PglUiLedState::Ok);
    if (ui.barWifi) {
      lv_bar_set_value(ui.barWifi, mapRssiToBar(rssi), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(ui.barWifi, lv_color_hex(0x7CFC00), LV_PART_INDICATOR);
    }
  } else {
    setWifiOffline();
    return;
  }
  if (ui.labelWifi) {
    lv_label_set_text(ui.labelWifi, wifiLine_);
  }
}

void PglDisplay::setWifiSearching() {
  wifiConnected_ = false;
  snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: recherche...");
  pglUiSetLed(ui.ledWifi, PglUiLedState::Warn);
  if (ui.barWifi) {
    lv_bar_set_value(ui.barWifi, 0, LV_ANIM_OFF);
  }
  if (ui.labelWifi) {
    lv_label_set_text(ui.labelWifi, wifiLine_);
  }
}

void PglDisplay::setWifiProgress(const PglWifiDiag& diag) {
  wifiConnected_ = false;
  strncpy(wifiLine_, diag.line, sizeof(wifiLine_) - 1);
  wifiLine_[sizeof(wifiLine_) - 1] = '\0';
  const PglUiLedState led =
      diag.connecting ? PglUiLedState::Warn : PglUiLedState::Error;
  pglUiSetLed(ui.ledWifi, led);
  if (ui.barWifi) {
    lv_bar_set_value(ui.barWifi, 0, LV_ANIM_OFF);
  }
  if (ui.labelWifi) {
    lv_label_set_text(ui.labelWifi, wifiLine_);
  }
}

void PglDisplay::setWifiConnecting() {
  wifiConnected_ = false;
  snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: connexion...");
  pglUiSetLed(ui.ledWifi, PglUiLedState::Warn);
  if (ui.barWifi) {
    lv_bar_set_value(ui.barWifi, 0, LV_ANIM_OFF);
  }
  if (ui.labelWifi) {
    lv_label_set_text(ui.labelWifi, wifiLine_);
  }
}

void PglDisplay::setWifiOffline() {
  wifiConnected_ = false;
  snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: hors ligne");
  pglUiSetLed(ui.ledWifi, PglUiLedState::Error);
  if (ui.barWifi) {
    lv_bar_set_value(ui.barWifi, 0, LV_ANIM_OFF);
  }
  if (ui.labelWifi) {
    lv_label_set_text(ui.labelWifi, wifiLine_);
  }
}

void PglDisplay::setUltrasonDistance(uint16_t distanceCm, bool sensorPresent) {
  char newLine[48];
  if (!sensorPresent && distanceCm == 0) {
    snprintf(newLine, sizeof(newLine), "US: pas d'echo");
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, 0);
      lv_obj_set_style_arc_color(ui.arcUs, lv_color_hex(0xFFB347), LV_PART_INDICATOR);
    }
  } else if (distanceCm == 0) {
    snprintf(newLine, sizeof(newLine), "US: hors portee (> %ucm)", PGL_ULTRASON_MAX_VALID_CM);
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, 0);
      lv_obj_set_style_arc_color(ui.arcUs, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
    }
  } else if (distanceCm <= PGL_ULTRASON_TRIGGER_CM) {
    snprintf(newLine, sizeof(newLine), "US: %u cm  [ZONE]", distanceCm);
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, distanceCm);
      lv_obj_set_style_arc_color(ui.arcUs, lv_color_hex(0x7CFC00), LV_PART_INDICATOR);
    }
    if (ui.labelUs) {
      lv_obj_set_style_text_color(ui.labelUs, lv_color_hex(0x7CFC00), LV_PART_MAIN);
    }
  } else {
    snprintf(newLine, sizeof(newLine), "US: %u cm", distanceCm);
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, distanceCm);
      lv_obj_set_style_arc_color(ui.arcUs, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
    }
    if (ui.labelUs) {
      lv_obj_set_style_text_color(ui.labelUs, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
    }
  }
  if (strcmp(usLine_, newLine) == 0) {
    return;
  }
  strncpy(usLine_, newLine, sizeof(usLine_) - 1);
  usLine_[sizeof(usLine_) - 1] = '\0';
  if (ui.labelUs) {
    lv_label_set_text(ui.labelUs, usLine_);
  }
}

void PglDisplay::setServerStatus(const PglServerCommStatus& status, uint16_t pendingEvents) {
  char postBuf[12];
  char hbBuf[12];
  if (status.lastPostHttp == 0) {
    snprintf(postBuf, sizeof(postBuf), "-");
  } else if (status.lastPostHttp == -1) {
    snprintf(postBuf, sizeof(postBuf), "WiFi");
  } else {
    snprintf(postBuf, sizeof(postBuf), "%d", status.lastPostHttp);
  }
  if (status.lastHeartbeatHttp == 0) {
    snprintf(hbBuf, sizeof(hbBuf), "-");
  } else if (status.lastHeartbeatHttp == -1) {
    snprintf(hbBuf, sizeof(hbBuf), "WiFi");
  } else {
    snprintf(hbBuf, sizeof(hbBuf), "%d", status.lastHeartbeatHttp);
  }
  snprintf(serverLine_, sizeof(serverLine_),
           "Srv: d=%s hb=%s q=%u",
           postBuf,
           hbBuf,
           static_cast<unsigned int>(pendingEvents));

  if (ui.barQueue) {
    lv_bar_set_value(ui.barQueue, mapQueueToBar(pendingEvents), LV_ANIM_OFF);
    if (pendingEvents > 0) {
      lv_obj_set_style_bg_color(ui.barQueue, lv_color_hex(0xFFB347), LV_PART_INDICATOR);
    } else if (status.lastPostHttp >= 200 && status.lastPostHttp < 300) {
      lv_obj_set_style_bg_color(ui.barQueue, lv_color_hex(0x7CFC00), LV_PART_INDICATOR);
    } else {
      lv_obj_set_style_bg_color(ui.barQueue, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
    }
  }

  if (status.lastPostHttp >= 200 && status.lastPostHttp < 300) {
    pglUiSetLed(ui.ledSrv, PglUiLedState::Ok);
    if (ui.labelServer) {
      lv_obj_set_style_text_color(ui.labelServer, lv_color_hex(0x7CFC00), LV_PART_MAIN);
    }
  } else if (status.lastPostHttp > 0 || status.lastHeartbeatHttp > 0) {
    pglUiSetLed(ui.ledSrv, PglUiLedState::Warn);
    if (ui.labelServer) {
      lv_obj_set_style_text_color(ui.labelServer, lv_color_hex(0xFFB347), LV_PART_MAIN);
    }
  } else {
    pglUiSetLed(ui.ledSrv, PglUiLedState::Off);
    if (ui.labelServer) {
      lv_obj_set_style_text_color(ui.labelServer, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
    }
  }

  if (ui.labelServer) {
    lv_label_set_text(ui.labelServer, serverLine_);
  }
}

void PglDisplay::showAudioPlaying(const char* reason, const char* mp3Path) {
  const char* file = mp3Path;
  if (file && file[0] != '\0') {
    const char* slash = strrchr(file, '/');
    if (slash && slash[1] != '\0') {
      file = slash + 1;
    }
  }
  if (!file || file[0] == '\0') {
    snprintf(audioLine_, sizeof(audioLine_), "[%s]", reason ? reason : "?");
  } else {
    snprintf(audioLine_, sizeof(audioLine_), "%s", file);
  }
  if (ui.labelAudio) {
    lv_label_set_text(ui.labelAudio, audioLine_);
    lv_obj_set_style_text_color(ui.labelAudio, lv_color_hex(0x7CFC00), LV_PART_MAIN);
  }
  if (ui.cardAudio) {
    lv_obj_set_style_border_color(ui.cardAudio, lv_color_hex(0x7CFC00), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.cardAudio, 2, LV_PART_MAIN);
  }
}

void PglDisplay::showAudioIdle() {
  snprintf(audioLine_, sizeof(audioLine_), "En attente");
  if (ui.labelAudio) {
    lv_label_set_text(ui.labelAudio, audioLine_);
    lv_obj_set_style_text_color(ui.labelAudio, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
  }
  if (ui.cardAudio) {
    lv_obj_set_style_border_color(ui.cardAudio, lv_color_hex(0x2A4050), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.cardAudio, 1, LV_PART_MAIN);
  }
}

void PglDisplay::tickSmileyIdle() {
  if (lastCountAnimMs_ == 0) return;
  if ((millis() - lastCountAnimMs_) > 3000) {
    if (ui.labelSmiley) {
      lv_label_set_text(ui.labelSmiley, ":-)");
    }
    lastCountAnimMs_ = 0;
  }
}

void PglDisplay::showIdle() {
  tickSmileyIdle();
}

void PglDisplay::sleepBacklight() {
  // Garde anti-redondance : ne pilote le GPIO que sur transition ON->OFF.
  if (!backlightOn_) return;
  digitalWrite(GFX_BL, LOW);
  backlightOn_ = false;
  PGL_LOG_V("Display: backlight GPIO%d=LOW", GFX_BL);
}

void PglDisplay::wakeBacklight() {
  // Methode symetrique de sleepBacklight() : ne rallume que sur transition OFF->ON.
  if (backlightOn_) return;
  digitalWrite(GFX_BL, HIGH);
  backlightOn_ = true;
  PGL_LOG_V("Display: backlight GPIO%d=HIGH", GFX_BL);
}

bool PglDisplay::isReady() const {
  return ready_;
}

void PglDisplay::setHardwareStatus(bool displayOk, bool irPresent, bool irObstacle) {
  const char* ir;
  if (!irPresent) {
    ir = "absent";
  } else if (irObstacle) {
    ir = "obstacle";
  } else {
    ir = "libre";
  }

  if (ui.labelIr) {
    lv_label_set_text_fmt(ui.labelIr, "IR: %s", ir);
    if (!irPresent) {
      lv_obj_set_style_text_color(ui.labelIr, lv_color_hex(0xFFB347), LV_PART_MAIN);
      pglUiSetLed(ui.ledIr, PglUiLedState::Warn);
    } else if (irObstacle) {
      lv_obj_set_style_text_color(ui.labelIr, lv_color_hex(0x7CFC00), LV_PART_MAIN);
      pglUiSetLed(ui.ledIr, PglUiLedState::Ok);
    } else {
      lv_obj_set_style_text_color(ui.labelIr, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
      pglUiSetLed(ui.ledIr, PglUiLedState::Ok);
    }
  }

  if (ui.labelTitle) {
    if (!displayOk) {
      lv_obj_set_style_text_color(ui.labelTitle, lv_color_hex(0xFFB347), LV_PART_MAIN);
    } else {
      lv_obj_set_style_text_color(ui.labelTitle, lv_palette_main(LV_PALETTE_TEAL), LV_PART_MAIN);
    }
  }
}

void PglDisplay::setPirStatus(bool pirPresent, bool pirMotion) {
  // ui.labelPir / ui.ledPir ne sont crees que si PGL_ENABLE_PIR (cf. pgl_ui) :
  // les gardes nullptr rendent cette methode inerte quand le PIR est desactive.
  const char* pir;
  if (!pirPresent) {
    pir = "absent";
  } else if (pirMotion) {
    pir = "mouvement";
  } else {
    pir = "libre";
  }

  if (ui.labelPir) {
    lv_label_set_text_fmt(ui.labelPir, "PIR: %s", pir);
    if (!pirPresent) {
      lv_obj_set_style_text_color(ui.labelPir, lv_color_hex(0xFFB347), LV_PART_MAIN);
      pglUiSetLed(ui.ledPir, PglUiLedState::Warn);
    } else if (pirMotion) {
      lv_obj_set_style_text_color(ui.labelPir, lv_color_hex(0x7CFC00), LV_PART_MAIN);
      pglUiSetLed(ui.ledPir, PglUiLedState::Ok);
    } else {
      lv_obj_set_style_text_color(ui.labelPir, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
      pglUiSetLed(ui.ledPir, PglUiLedState::Ok);
    }
  }
}

#else

void PglDisplay::begin() {
  PGL_LOG("Display: desactive (env headless)");
}
void PglDisplay::update() {}
void PglDisplay::onBottleCount(uint32_t, uint32_t) {}
void PglDisplay::setCounter(uint32_t, uint32_t) {}
void PglDisplay::setWifiInfo(const char*, wl_status_t, int) {}
void PglDisplay::setWifiProgress(const PglWifiDiag&) {}
void PglDisplay::setWifiSearching() {}
void PglDisplay::setWifiConnecting() {}
void PglDisplay::setWifiOffline() {}
void PglDisplay::setUltrasonDistance(uint16_t, bool) {}
void PglDisplay::setServerStatus(const PglServerCommStatus&, uint16_t) {}
void PglDisplay::showAudioPlaying(const char*, const char*) {}
void PglDisplay::showAudioIdle() {}
void PglDisplay::tickSmileyIdle() {}
void PglDisplay::showIdle() {}
void PglDisplay::sleepBacklight() {}
void PglDisplay::wakeBacklight() {}
bool PglDisplay::isReady() const { return false; }
void PglDisplay::setHardwareStatus(bool, bool, bool) {}
void PglDisplay::setPirStatus(bool, bool) {}

#endif
