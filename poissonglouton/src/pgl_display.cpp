#include "pgl_display.h"

#include "config.h"
#include "pgl_log.h"

#if !PGL_HEADLESS

#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr int GFX_BL = 1;
constexpr uint16_t kScreenW = 480;
constexpr uint16_t kScreenH = 272;
constexpr uint32_t kLvglBufLines = 40;
constexpr int kPadX = 8;
constexpr int kColW = 228;
constexpr int kLeftX = kPadX;
constexpr int kRightX = kScreenW / 2 + 4;
constexpr int kHeaderH = 40;
constexpr int kBodyY = kHeaderH + 4;
constexpr int kRowH = 20;

Arduino_DataBus* bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
Arduino_GFX* panel = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);
Arduino_GFX* gfx = new Arduino_Canvas(kScreenW, kScreenH, panel);

lv_disp_draw_buf_t drawBuf;
lv_color_t* drawBuffer = nullptr;
lv_obj_t* labelTitle = nullptr;
lv_obj_t* labelHw = nullptr;
lv_obj_t* labelSmiley = nullptr;
lv_obj_t* labelTotal = nullptr;
lv_obj_t* labelToday = nullptr;
lv_obj_t* labelUs = nullptr;
lv_obj_t* labelServer = nullptr;
lv_obj_t* labelWifi = nullptr;
lv_obj_t* labelAudio = nullptr;
bool adminUnlockedState = false;
uint32_t lastUiUpdateMs = 0;

void styleLabelLight(lv_obj_t* obj, const lv_font_t* font) {
  lv_obj_set_style_text_color(obj, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
  if (font) {
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
  }
}

/** Colonne 0 = gauche (compteurs), 1 = droite (capteurs / réseau). */
void placeGridLabel(lv_obj_t* obj, uint8_t col, uint8_t row, const lv_font_t* font) {
  const int rowH = (col == 0) ? 22 : 28;
  const int x = (col == 0) ? kLeftX : kRightX;
  const int y = kBodyY + static_cast<int>(row) * rowH;
  styleLabelLight(obj, font);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_width(obj, kColW);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

void displayFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)&colorP->full, w, h);
  lv_disp_flush_ready(disp);
}

void flushUi() {
  lv_timer_handler();
  gfx->flush();
}
}  // namespace

void PglDisplay::refreshLabels() {
  lv_label_set_text(labelHw, hwLine_);
  lv_label_set_text_fmt(labelTotal, "Total: %lu", static_cast<unsigned long>(totalCount_));
  lv_label_set_text_fmt(labelToday, "Auj.: %lu", static_cast<unsigned long>(todayCount_));
  lv_label_set_text(labelUs, usLine_);
  lv_label_set_text(labelServer, serverLine_);
  lv_label_set_text(labelWifi, wifiLine_);
  lv_label_set_text(labelAudio, audioLine_);
}

void PglDisplay::begin() {
  PGL_LOG("Display: init NV3041A %ux%u QSPI (CS45 SCK47 D0-3:21,48,40,39) BL=%d",
          kScreenW, kScreenH, GFX_BL);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  PGL_LOG_V("Display: backlight GPIO%d=HIGH", GFX_BL);

  gfxOk_ = gfx->begin();
  if (!gfxOk_) {
    PGL_LOG("Display: ERREUR gfx->begin() — panneau NV3041A non repondu");
  } else {
    PGL_LOG("Display: gfx->begin() OK (NV3041A %ux%u)", kScreenW, kScreenH);
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

  lv_disp_draw_buf_init(&drawBuf, drawBuffer, nullptr, bufPixels);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kScreenW;
  dispDrv.ver_res = kScreenH;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0E1A22), LV_PART_MAIN);

  labelTitle = lv_label_create(lv_scr_act());
  lv_label_set_text(labelTitle, "Poisson Glouton");
  styleLabelLight(labelTitle, &lv_font_montserrat_16);
  lv_obj_align(labelTitle, LV_ALIGN_TOP_MID, 0, 4);

  labelHw = lv_label_create(lv_scr_act());
  styleLabelLight(labelHw, &lv_font_montserrat_12);
  lv_obj_set_width(labelHw, kScreenW - 16);
  lv_label_set_long_mode(labelHw, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(labelHw, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(labelHw, LV_ALIGN_TOP_MID, 0, 22);

  labelSmiley = lv_label_create(lv_scr_act());
  lv_label_set_text(labelSmiley, ":-)");
  placeGridLabel(labelSmiley, 0, 0, &lv_font_montserrat_20);

  labelTotal = lv_label_create(lv_scr_act());
  placeGridLabel(labelTotal, 0, 1, &lv_font_montserrat_16);

  labelToday = lv_label_create(lv_scr_act());
  placeGridLabel(labelToday, 0, 2, &lv_font_montserrat_14);

  labelUs = lv_label_create(lv_scr_act());
  placeGridLabel(labelUs, 1, 0, &lv_font_montserrat_14);

  labelServer = lv_label_create(lv_scr_act());
  placeGridLabel(labelServer, 1, 1, &lv_font_montserrat_12);

  labelWifi = lv_label_create(lv_scr_act());
  placeGridLabel(labelWifi, 1, 2, &lv_font_montserrat_12);

  labelAudio = lv_label_create(lv_scr_act());
  placeGridLabel(labelAudio, 1, 3, &lv_font_montserrat_12);

  snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: initialisation...");
  snprintf(audioLine_, sizeof(audioLine_), "Son: —");
  snprintf(usLine_, sizeof(usLine_), "US: —");
  snprintf(serverLine_, sizeof(serverLine_), "Srv: en attente");
  snprintf(hwLine_, sizeof(hwLine_), "Ecran: init | IR: ...");
  ready_ = gfxOk_ && lvglOk_;
  refreshLabels();

  for (int i = 0; i < 8; ++i) {
    flushUi();
    delay(5);
  }
  if (ready_) {
    PGL_LOG("Display: FONCTIONNEL — tableau de bord actif (%ux%u)", kScreenW, kScreenH);
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
  lv_label_set_text(labelSmiley, "^_^");
  refreshLabels();
  PGL_LOG_V("Display: +1 total=%lu today=%lu", static_cast<unsigned long>(totalCount),
            static_cast<unsigned long>(todayCount));
}

void PglDisplay::setWifiInfo(const char* ssid, wl_status_t status, int rssi) {
  if (status == WL_CONNECTED && ssid && ssid[0] != '\0') {
    snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: %s (%d dBm)", ssid, rssi);
  } else {
    snprintf(wifiLine_, sizeof(wifiLine_), "WiFi: deconnecte");
  }
  lv_label_set_text(labelWifi, wifiLine_);
}

void PglDisplay::setUltrasonDistance(uint16_t distanceCm, bool sensorPresent) {
  if (!sensorPresent && distanceCm == 0) {
    snprintf(usLine_, sizeof(usLine_), "US: capteur absent");
  } else if (distanceCm == 0) {
    snprintf(usLine_, sizeof(usLine_), "US: hors portee (> %ucm)", PGL_ULTRASON_MAX_VALID_CM);
  } else if (distanceCm <= PGL_ULTRASON_TRIGGER_CM) {
    snprintf(usLine_, sizeof(usLine_), "US: %u cm  [ZONE]", distanceCm);
    lv_obj_set_style_text_color(labelUs, lv_color_hex(0x7CFC00), LV_PART_MAIN);
  } else {
    snprintf(usLine_, sizeof(usLine_), "US: %u cm", distanceCm);
    lv_obj_set_style_text_color(labelUs, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
  }
  lv_label_set_text(labelUs, usLine_);
}

void PglDisplay::setServerStatus(const PglServerCommStatus& status, uint16_t pendingEvents) {
  char postBuf[12];
  char hbBuf[12];
  if (status.lastPostHttp == 0) {
    snprintf(postBuf, sizeof(postBuf), "—");
  } else if (status.lastPostHttp == -1) {
    snprintf(postBuf, sizeof(postBuf), "WiFi");
  } else {
    snprintf(postBuf, sizeof(postBuf), "%d", status.lastPostHttp);
  }
  if (status.lastHeartbeatHttp == 0) {
    snprintf(hbBuf, sizeof(hbBuf), "—");
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
  lv_label_set_text(labelServer, serverLine_);
  if (status.lastPostHttp >= 200 && status.lastPostHttp < 300) {
    lv_obj_set_style_text_color(labelServer, lv_color_hex(0x7CFC00), LV_PART_MAIN);
  } else if (status.lastPostHttp > 0 || status.lastHeartbeatHttp > 0) {
    lv_obj_set_style_text_color(labelServer, lv_color_hex(0xFFB347), LV_PART_MAIN);
  } else {
    lv_obj_set_style_text_color(labelServer, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
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
    snprintf(audioLine_, sizeof(audioLine_), "Son: [%s]", reason ? reason : "?");
  } else {
    snprintf(audioLine_, sizeof(audioLine_), "Son: %s", file);
  }
  lv_label_set_text(labelAudio, audioLine_);
}

void PglDisplay::showAudioIdle() {
  snprintf(audioLine_, sizeof(audioLine_), "Son: —");
  lv_label_set_text(labelAudio, audioLine_);
}

void PglDisplay::tickSmileyIdle() {
  if (lastCountAnimMs_ == 0) return;
  if ((millis() - lastCountAnimMs_) > 3000) {
    lv_label_set_text(labelSmiley, ":-)");
    lastCountAnimMs_ = 0;
  }
}

void PglDisplay::showIdle() {
  tickSmileyIdle();
}

void PglDisplay::sleepBacklight() {
  digitalWrite(GFX_BL, LOW);
  PGL_LOG_V("Display: backlight GPIO%d=LOW", GFX_BL);
}

bool PglDisplay::adminUnlocked() const {
  return adminUnlockedState;
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
  if (!labelHw) {
    return;
  }
  lv_label_set_text(labelHw, hwLine_);
  if (displayOk && irPresent) {
    lv_obj_set_style_text_color(labelHw, lv_color_hex(0x7CFC00), LV_PART_MAIN);
  } else if (!displayOk || !irPresent) {
    lv_obj_set_style_text_color(labelHw, lv_color_hex(0xFFB347), LV_PART_MAIN);
  } else {
    lv_obj_set_style_text_color(labelHw, lv_color_hex(0xE8F4F8), LV_PART_MAIN);
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
void PglDisplay::setUltrasonDistance(uint16_t, bool) {}
void PglDisplay::setServerStatus(const PglServerCommStatus&, uint16_t) {}
void PglDisplay::showAudioPlaying(const char*, const char*) {}
void PglDisplay::showAudioIdle() {}
void PglDisplay::tickSmileyIdle() {}
void PglDisplay::showIdle() {}
void PglDisplay::sleepBacklight() {}
bool PglDisplay::adminUnlocked() const { return false; }
bool PglDisplay::isReady() const { return false; }
void PglDisplay::setHardwareStatus(bool, bool, bool) {}

#endif
