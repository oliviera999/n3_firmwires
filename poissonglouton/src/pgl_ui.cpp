#include "pgl_ui.h"

#include "config.h"
#include "pgl_display_board.h"

namespace {

constexpr uint32_t kColorBg = 0x0E1A22;
constexpr uint32_t kColorCard = 0x1A2D3A;
constexpr uint32_t kColorBorder = 0x2A4050;
constexpr uint32_t kColorText = 0xE8F4F8;
constexpr uint32_t kColorSuccess = 0x7CFC00;
constexpr uint32_t kColorWarn = 0xFFB347;
constexpr uint32_t kColorError = 0xFF6B6B;

lv_style_t styleCard;
lv_style_t styleTitle;
lv_style_t styleMuted;
lv_style_t styleBullet;
lv_style_t styleValueHero;
bool stylesReady = false;

const lv_font_t* fontLarge() {
  return &lv_font_montserrat_16;
}

const lv_font_t* fontNormal() {
  return &lv_font_montserrat_14;
}

const lv_font_t* fontSmall() {
  return &lv_font_montserrat_12;
}

void initStyles() {
  if (stylesReady) {
    return;
  }

#if LV_USE_THEME_DEFAULT
  lv_theme_default_init(nullptr,
                        lv_palette_main(LV_PALETTE_TEAL),
                        lv_palette_main(LV_PALETTE_CYAN),
                        true,
                        fontNormal());
#endif

  lv_style_init(&styleCard);
  lv_style_set_radius(&styleCard, PGL_UI_CARD_RADIUS);
  lv_style_set_bg_color(&styleCard, lv_color_hex(kColorCard));
  lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
  lv_style_set_border_color(&styleCard, lv_color_hex(kColorBorder));
  lv_style_set_border_width(&styleCard, 1);
  lv_style_set_border_opa(&styleCard, LV_OPA_COVER);
  lv_style_set_pad_all(&styleCard, PGL_UI_CARD_PAD);

  lv_style_init(&styleTitle);
  lv_style_set_text_font(&styleTitle, fontLarge());
  lv_style_set_text_color(&styleTitle, lv_palette_main(LV_PALETTE_TEAL));

  lv_style_init(&styleMuted);
  lv_style_set_text_font(&styleMuted, fontSmall());
  lv_style_set_text_opa(&styleMuted, LV_OPA_60);
  lv_style_set_text_color(&styleMuted, lv_color_hex(kColorText));

  lv_style_init(&styleBullet);
  lv_style_set_radius(&styleBullet, LV_RADIUS_CIRCLE);
  lv_style_set_border_width(&styleBullet, 0);
  lv_style_set_bg_opa(&styleBullet, LV_OPA_COVER);

  lv_style_init(&styleValueHero);
  lv_style_set_text_font(&styleValueHero, &lv_font_montserrat_20);
  lv_style_set_text_color(&styleValueHero, lv_color_hex(kColorText));

  stylesReady = true;
}

lv_obj_t* createCard(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &styleCard, 0);
  lv_obj_set_size(card, w, h);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

lv_obj_t* createLedWithCaption(lv_obj_t* parent, const char* caption, lv_coord_t x, lv_coord_t y) {
  lv_obj_t* led = lv_led_create(parent);
  lv_led_set_color(led, lv_color_hex(kColorText));
  lv_led_off(led);
  lv_obj_set_size(led, 10, 10);
  lv_obj_set_pos(led, x, y);

  lv_obj_t* lbl = lv_label_create(parent);
  lv_label_set_text(lbl, caption);
  lv_obj_add_style(lbl, &styleMuted, 0);
  lv_obj_set_pos(lbl, x + 14, y - 2);
  return led;
}

struct StatusRow {
  lv_obj_t* bullet = nullptr;
  lv_obj_t* label = nullptr;
  lv_obj_t* bar = nullptr;
};

StatusRow createStatusRow(lv_obj_t* parent, const char* title, bool withBar) {
  StatusRow row;
  lv_obj_t* cont = lv_obj_create(parent);
  lv_obj_remove_style_all(cont);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_height(cont, withBar ? 36 : 22);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  row.bullet = lv_obj_create(cont);
  lv_obj_remove_style_all(row.bullet);
  lv_obj_add_style(row.bullet, &styleBullet, 0);
  lv_obj_set_size(row.bullet, 8, 8);
  lv_obj_set_style_bg_color(row.bullet, lv_palette_main(LV_PALETTE_TEAL), 0);
  lv_obj_align(row.bullet, LV_ALIGN_LEFT_MID, 0, 0);

  row.label = lv_label_create(cont);
  lv_label_set_text(row.label, title);
  lv_obj_add_style(row.label, &styleMuted, 0);
  lv_obj_set_width(row.label, LV_PCT(85));
  lv_label_set_long_mode(row.label, LV_LABEL_LONG_DOT);
  lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 14, withBar ? -8 : 0);

  if (withBar) {
    row.bar = lv_bar_create(cont);
    lv_bar_set_range(row.bar, 0, 100);
    lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(row.bar, LV_PCT(85), 6);
    lv_obj_align(row.bar, LV_ALIGN_BOTTOM_LEFT, 14, 0);
    lv_obj_set_style_bg_color(row.bar, lv_color_hex(kColorBorder), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row.bar, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
    lv_obj_set_style_radius(row.bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(row.bar, 3, LV_PART_INDICATOR);
  }

  return row;
}

lv_obj_t* createUsArc(lv_obj_t* parent, lv_coord_t size) {
  lv_obj_t* arc = lv_arc_create(parent);
  lv_arc_set_range(arc, 0, PGL_ULTRASON_MAX_VALID_CM);
  lv_arc_set_value(arc, 0);
  lv_arc_set_bg_angles(arc, 135, 45);
  lv_obj_set_size(arc, size, size);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, lv_color_hex(kColorBorder), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_TEAL), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
  return arc;
}

void buildHeader(lv_obj_t* parent, PglUiHandles& h) {
  lv_obj_t* header = lv_obj_create(parent);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, PGL_SCREEN_W, PGL_UI_HEADER_H);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  h.labelTitle = lv_label_create(header);
  lv_label_set_text(h.labelTitle, "Poisson Glouton");
  lv_obj_add_style(h.labelTitle, &styleTitle, 0);
  lv_obj_align(h.labelTitle, LV_ALIGN_LEFT_MID, PGL_UI_PAD_X, 0);

  const lv_coord_t ledY = (PGL_UI_HEADER_H - 10) / 2;
  lv_coord_t ledX = PGL_SCREEN_W - PGL_UI_PAD_X - 90;
  h.ledIr = createLedWithCaption(header, "IR", ledX, ledY);
  ledX += 30;
  h.ledWifi = createLedWithCaption(header, "WiFi", ledX, ledY);
  ledX += 36;
  h.ledSrv = createLedWithCaption(header, "Srv", ledX, ledY);
}

void buildCounterCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Compteurs");
  lv_obj_add_style(title, &styleMuted, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  handles.labelSmiley = lv_label_create(card);
  lv_label_set_text(handles.labelSmiley, ":-)");
  lv_obj_add_style(handles.labelSmiley, &styleValueHero, 0);
  lv_obj_align(handles.labelSmiley, LV_ALIGN_TOP_RIGHT, 0, 0);

  handles.labelTodayCaption = lv_label_create(card);
  lv_label_set_text(handles.labelTodayCaption, "AUJOURD'HUI");
  lv_obj_add_style(handles.labelTodayCaption, &styleMuted, 0);
  lv_obj_align(handles.labelTodayCaption, LV_ALIGN_TOP_LEFT, 0, 22);

  handles.labelTodayValue = lv_label_create(card);
  lv_label_set_text(handles.labelTodayValue, "0");
  lv_obj_add_style(handles.labelTodayValue, &styleValueHero, 0);
  lv_obj_align(handles.labelTodayValue, LV_ALIGN_LEFT_MID, 0, 8);

  handles.labelTotal = lv_label_create(card);
  lv_label_set_text(handles.labelTotal, "Total: 0");
  lv_obj_set_style_text_font(handles.labelTotal, fontNormal(), 0);
  lv_obj_set_style_text_color(handles.labelTotal, lv_color_hex(kColorText), 0);
  lv_obj_align(handles.labelTotal, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void buildSensorCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Capteurs");
  lv_obj_add_style(title, &styleMuted, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  handles.arcUs = createUsArc(card, PGL_UI_ARC_SIZE);
  lv_obj_align(handles.arcUs, LV_ALIGN_LEFT_MID, 0, 6);

  handles.labelUs = lv_label_create(card);
  lv_label_set_text(handles.labelUs, "US: —");
  lv_obj_add_style(handles.labelUs, &styleMuted, 0);
  lv_obj_align(handles.labelUs, LV_ALIGN_TOP_RIGHT, 0, 18);

  handles.labelIr = lv_label_create(card);
  lv_label_set_text(handles.labelIr, "IR: ...");
  lv_obj_add_style(handles.labelIr, &styleMuted, 0);
  lv_obj_align(handles.labelIr, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void buildSystemCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 4, 0);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Systeme");
  lv_obj_add_style(title, &styleMuted, 0);

  StatusRow wifiRow = createStatusRow(card, "WiFi: ...", true);
  handles.labelWifi = wifiRow.label;
  handles.barWifi = wifiRow.bar;

  StatusRow srvRow = createStatusRow(card, "Srv: —", true);
  handles.labelServer = srvRow.label;
  handles.barQueue = srvRow.bar;

  StatusRow audioRow = createStatusRow(card, "Son: —", false);
  handles.labelAudio = audioRow.label;
}

#if !PGL_UI_PORTRAIT
void layoutLandscape(PglUiHandles& h) {
  const lv_coord_t bodyY = PGL_UI_BODY_Y;
  const lv_coord_t bodyH = PGL_SCREEN_H - bodyY - PGL_UI_PAD_Y;
  const lv_coord_t contentW = PGL_SCREEN_W - 2 * PGL_UI_PAD_X;
  const lv_coord_t leftW = (contentW * PGL_UI_LEFT_W_PCT) / 100;
  const lv_coord_t rightW = contentW - leftW - PGL_UI_CARD_GAP;
  const lv_coord_t leftX = PGL_UI_PAD_X;
  const lv_coord_t rightX = leftX + leftW + PGL_UI_CARD_GAP;
  const lv_coord_t sensorH = (bodyH * 55) / 100;
  const lv_coord_t systemH = bodyH - sensorH - PGL_UI_CARD_GAP;

  buildCounterCard(lv_scr_act(), leftX, bodyY, leftW, bodyH, h);
  buildSensorCard(lv_scr_act(), rightX, bodyY, rightW, sensorH, h);
  buildSystemCard(lv_scr_act(), rightX, bodyY + sensorH + PGL_UI_CARD_GAP, rightW, systemH, h);
}
#endif

#if PGL_UI_PORTRAIT
void layoutPortrait(PglUiHandles& h) {
  const lv_coord_t bodyY = PGL_UI_BODY_Y;
  const lv_coord_t cardW = PGL_SCREEN_W - 2 * PGL_UI_PAD_X;
  const lv_coord_t x = PGL_UI_PAD_X;
  lv_coord_t y = bodyY;

  buildCounterCard(lv_scr_act(), x, y, cardW, PGL_UI_COUNTER_CARD_H, h);
  y += PGL_UI_COUNTER_CARD_H + PGL_UI_CARD_GAP;

  buildSensorCard(lv_scr_act(), x, y, cardW, PGL_UI_SENSOR_CARD_H, h);
  y += PGL_UI_SENSOR_CARD_H + PGL_UI_CARD_GAP;

  const lv_coord_t systemH = PGL_SCREEN_H - y - PGL_UI_PAD_Y;
  buildSystemCard(lv_scr_act(), x, y, cardW, systemH, h);
}
#endif

}  // namespace

void pglUiInitTheme() {
  initStyles();
}

void pglUiSetLed(lv_obj_t* led, PglUiLedState state) {
  if (!led) {
    return;
  }
  switch (state) {
    case PglUiLedState::Ok:
      lv_led_set_color(led, lv_color_hex(kColorSuccess));
      lv_led_on(led);
      break;
    case PglUiLedState::Warn:
      lv_led_set_color(led, lv_color_hex(kColorWarn));
      lv_led_on(led);
      break;
    case PglUiLedState::Error:
      lv_led_set_color(led, lv_color_hex(kColorError));
      lv_led_on(led);
      break;
    case PglUiLedState::Off:
    default:
      lv_led_set_color(led, lv_color_hex(kColorText));
      lv_led_off(led);
      break;
  }
}

PglUiHandles pglUiBuildDashboard(lv_obj_t* parent) {
  initStyles();

  lv_obj_set_style_bg_color(parent, lv_color_hex(kColorBg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

  PglUiHandles handles;
  buildHeader(parent, handles);

#if PGL_UI_PORTRAIT
  layoutPortrait(handles);
#else
  layoutLandscape(handles);
#endif

  return handles;
}
