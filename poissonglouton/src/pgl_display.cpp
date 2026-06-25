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

#if PGL_TOUCH_AXS15231B
#include "touch_axs15231b.h"
#endif

namespace {
constexpr int GFX_BL = PGL_GFX_BL;
constexpr uint16_t kScreenW = PGL_SCREEN_W;
constexpr uint16_t kScreenH = PGL_SCREEN_H;
constexpr uint32_t kLvglBufLines = 40;
constexpr uint8_t kQueueBarMaxEvents = 20;

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
// Vrai dès que LVGL a redessiné dans le canvas depuis le dernier push panneau.
// Évite un transfert QSPI plein écran à chaque tour quand rien ne change.
bool canvasDirty = false;
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

void displayFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  // Aligné LVGL_Widgets : LV_COLOR_16_SWAP=1 => draw16bitBeRGBBitmap (QSPI NV3041A)
#if LV_COLOR_16_SWAP
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t*)&colorP->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)&colorP->full, w, h);
#endif
  canvasDirty = true;
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
  // Ne pousser le canvas vers le panneau QSPI que si LVGL a redessiné
  // quelque chose (canvasDirty positionné dans displayFlush).
  if (canvasDirty) {
    gfx->flush();
    canvasDirty = false;
  }
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
  PGL_LOG("Display: init AXS15231B %ux%u (%s) QSPI BL=%d",
          kScreenW, kScreenH, PGL_DISPLAY_BOARD_NAME, GFX_BL);
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
    snprintf(hwLine_, sizeof(hwLine_), "Ecran: ECHEC | IR: ...");
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
  snprintf(hwLine_, sizeof(hwLine_), "Ecran: init | IR: ...");
  ready_ = gfxOk_ && lvglOk_;
  refreshLabels();

  for (int i = 0; i < 8; ++i) {
    flushUi();
    delay(5);
  }
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
  if (millis() - lastUiUpdateMs < 5) return;
  lastUiUpdateMs = millis();
  flushUi();
}

void PglDisplay::setCounter(uint32_t totalCount, uint32_t todayCount) {
  totalCount_ = totalCount;
  todayCount_ = todayCount;
  refreshLabels();
}

void PglDisplay::onBottleCount(uint32_t totalCount, uint32_t todayCount) {
  totalCount_ = totalCount;
  todayCount_ = todayCount;
  lastCountAnimMs_ = millis();
  if (ui.labelSmiley) {
    lv_label_set_text(ui.labelSmiley, "^_^");
  }
  refreshLabels();
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
  if (!sensorPresent && distanceCm == 0) {
    snprintf(usLine_, sizeof(usLine_), "US: capteur absent");
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, 0);
      lv_obj_set_style_arc_color(ui.arcUs, lv_color_hex(0xFFB347), LV_PART_INDICATOR);
    }
  } else if (distanceCm == 0) {
    snprintf(usLine_, sizeof(usLine_), "US: hors portee (> %ucm)", PGL_ULTRASON_MAX_VALID_CM);
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, 0);
      lv_obj_set_style_arc_color(ui.arcUs, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
    }
  } else if (distanceCm <= PGL_ULTRASON_TRIGGER_CM) {
    snprintf(usLine_, sizeof(usLine_), "US: %u cm  [ZONE]", distanceCm);
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, distanceCm);
      lv_obj_set_style_arc_color(ui.arcUs, lv_color_hex(0x7CFC00), LV_PART_INDICATOR);
    }
    if (ui.labelUs) {
      lv_obj_set_style_text_color(ui.labelUs, lv_color_hex(0x7CFC00), LV_PART_MAIN);
    }
  } else {
    snprintf(usLine_, sizeof(usLine_), "US: %u cm", distanceCm);
    if (ui.arcUs) {
      lv_arc_set_value(ui.arcUs, distanceCm);
      lv_obj_set_style_arc_color(ui.arcUs, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
    }
    if (ui.labelUs) {
      lv_obj_set_style_text_color(ui.labelUs, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
    }
  }
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
  const char* disp = displayOk ? "OK" : "ECHEC";
  const char* ir;
  if (!irPresent) {
    ir = "absent";
  } else if (irObstacle) {
    ir = "obstacle";
  } else {
    ir = "libre";
  }
  snprintf(hwLine_, sizeof(hwLine_), "Ecran: %s | IR: %s", disp, ir);

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
  (void)disp;
}

#else

void PglDisplay::begin() {
  PGL_LOG("Display: desactive (env headless)");
}
void PglDisplay::update() {}
void PglDisplay::onBottleCount(uint32_t, uint32_t) {}
void PglDisplay::setCounter(uint32_t, uint32_t) {}
void PglDisplay::setWifiInfo(const char*, wl_status_t, int) {}
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

#endif
