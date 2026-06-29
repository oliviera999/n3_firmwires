#pragma once

#include <lvgl.h>

// Poignees widgets du tableau de bord LVGL (module pgl_ui).
struct PglUiHandles {
  lv_obj_t* labelTitle = nullptr;
  lv_obj_t* labelVersion = nullptr;
  lv_obj_t* ledIr = nullptr;
  lv_obj_t* ledPir = nullptr;  // pastille header PIR (cree seulement si PGL_ENABLE_PIR)
  lv_obj_t* ledWifi = nullptr;
  lv_obj_t* ledSrv = nullptr;
  lv_obj_t* labelSmiley = nullptr;
  lv_obj_t* labelTodayCaption = nullptr;
  lv_obj_t* labelTodayValue = nullptr;
  lv_obj_t* labelTotal = nullptr;
  lv_obj_t* arcUs = nullptr;
  lv_obj_t* labelUs = nullptr;
  lv_obj_t* labelIr = nullptr;
  lv_obj_t* labelPir = nullptr;  // ligne carte Capteurs PIR (cree seulement si PGL_ENABLE_PIR)
  lv_obj_t* labelWifi = nullptr;
  lv_obj_t* barWifi = nullptr;
  lv_obj_t* labelServer = nullptr;
  lv_obj_t* barQueue = nullptr;
  lv_obj_t* labelAudio = nullptr;
  lv_obj_t* cardAudio = nullptr;
};

enum class PglUiLedState : uint8_t {
  Off = 0,
  Ok,
  Warn,
  Error,
};

void pglUiInitTheme();
PglUiHandles pglUiBuildDashboard(lv_obj_t* parent);
void pglUiSetLed(lv_obj_t* led, PglUiLedState state);
