#include "pgl_display.h"

#include "config.h"
#include "pgl_log.h"

#if !PGL_HEADLESS

#include <Arduino_GFX_Library.h>
#include <lvgl.h>

namespace {
constexpr int GFX_BL = 1;
constexpr uint16_t kScreenW = 480;
constexpr uint16_t kScreenH = 272;

Arduino_DataBus* bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
Arduino_GFX* panel = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);
Arduino_GFX* gfx = new Arduino_Canvas(kScreenW, kScreenH, panel);

lv_disp_draw_buf_t drawBuf;
lv_color_t* drawBuffer = nullptr;
lv_obj_t* labelMain = nullptr;
lv_obj_t* labelSub = nullptr;
lv_obj_t* smiley = nullptr;
bool adminUnlockedState = false;
uint32_t lastUiUpdateMs = 0;

void displayFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)&colorP->full, w, h);
  lv_disp_flush_ready(disp);
}

}

void PglDisplay::begin() {
  PGL_LOG("Display: init NV3041A %ux%u QSPI (CS45 SCK47 D0-3:21,48,40,39) BL=%d",
          kScreenW, kScreenH, GFX_BL);
  if (!gfx->begin()) {
    PGL_LOG("Display: ERREUR gfx->begin() — ecran peut rester noir");
  } else {
    PGL_LOG_V("Display: gfx->begin() OK");
  }
  gfx->fillScreen(0x0000);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  PGL_LOG_V("Display: backlight GPIO%d=HIGH", GFX_BL);

  lv_init();
  drawBuffer = static_cast<lv_color_t*>(heap_caps_malloc(sizeof(lv_color_t) * (kScreenW * 20), MALLOC_CAP_8BIT));
  if (!drawBuffer) {
    PGL_LOG("Display: ERREUR allocation buffer LVGL (%u lignes)", kScreenW * 20);
  } else {
    PGL_LOG_V("Display: buffer LVGL %u octets", static_cast<unsigned int>(sizeof(lv_color_t) * kScreenW * 20));
  }
  lv_disp_draw_buf_init(&drawBuf, drawBuffer, nullptr, kScreenW * 20);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kScreenW;
  dispDrv.ver_res = kScreenH;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0E1A22), LV_PART_MAIN);
  smiley = lv_label_create(lv_scr_act());
  lv_label_set_text(smiley, ":-)");
  lv_obj_set_style_text_font(smiley, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_align(smiley, LV_ALIGN_CENTER, 0, -50);

  labelMain = lv_label_create(lv_scr_act());
  lv_label_set_text(labelMain, "Poisson Glouton");
  lv_obj_set_style_text_font(labelMain, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_align(labelMain, LV_ALIGN_CENTER, 0, 10);

  labelSub = lv_label_create(lv_scr_act());
  lv_label_set_text(labelSub, "Pret a recycler !");
  lv_obj_set_style_text_font(labelSub, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_align(labelSub, LV_ALIGN_CENTER, 0, 56);
  PGL_LOG("Display: UI idle « Poisson Glouton » affichee");
}

void PglDisplay::update() {
  if (millis() - lastUiUpdateMs < 5) return;
  lastUiUpdateMs = millis();
  lv_timer_handler();
  gfx->flush();
}

void PglDisplay::onBottleCount(uint32_t totalCount, uint32_t todayCount) {
  PGL_LOG_V("Display: animation merci total=%lu today=%lu",
            static_cast<unsigned long>(totalCount),
            static_cast<unsigned long>(todayCount));
  lv_label_set_text(smiley, "^_^");
  lv_label_set_text_fmt(labelMain, "Bravo ! Total: %lu", static_cast<unsigned long>(totalCount));
  lv_label_set_text_fmt(labelSub, "Aujourd'hui: %lu", static_cast<unsigned long>(todayCount));
}

void PglDisplay::showIdle() {
  lv_label_set_text(smiley, ":-)");
  lv_label_set_text(labelMain, "Poisson Glouton");
  if (!adminUnlockedState) {
    lv_label_set_text(labelSub, "Pret a recycler !");
  }
}

void PglDisplay::sleepBacklight() {
  digitalWrite(GFX_BL, LOW);
  PGL_LOG_V("Display: backlight GPIO%d=LOW", GFX_BL);
}

bool PglDisplay::adminUnlocked() const {
  return adminUnlockedState;
}

#else

void PglDisplay::begin() {}
void PglDisplay::update() {}
void PglDisplay::onBottleCount(uint32_t, uint32_t) {}
void PglDisplay::showIdle() {}
void PglDisplay::sleepBacklight() {}
bool PglDisplay::adminUnlocked() const { return false; }

#endif
