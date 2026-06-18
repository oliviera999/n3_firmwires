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
lv_style_t styleSection;
lv_style_t styleData;
lv_style_t styleChip;
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
  lv_style_set_text_color(&styleTitle, lv_color_hex(kColorText));

  lv_style_init(&styleSection);
  lv_style_set_text_font(&styleSection, fontSmall());
  lv_style_set_text_opa(&styleSection, LV_OPA_70);
  lv_style_set_text_color(&styleSection, lv_palette_lighten(LV_PALETTE_TEAL, 1));

  lv_style_init(&styleData);
  lv_style_set_text_font(&styleData, fontNormal());
  lv_style_set_text_opa(&styleData, LV_OPA_COVER);
  lv_style_set_text_color(&styleData, lv_color_hex(kColorText));

  lv_style_init(&styleChip);
  lv_style_set_text_font(&styleChip, fontSmall());
  lv_style_set_text_opa(&styleChip, LV_OPA_COVER);
  lv_style_set_text_color(&styleChip, lv_color_hex(kColorText));

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

lv_obj_t* createStatusChip(lv_obj_t* parent, const char* caption) {
  lv_obj_t* chip = lv_obj_create(parent);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(chip, 4, 0);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* led = lv_led_create(chip);
  lv_led_set_color(led, lv_color_hex(kColorText));
  lv_led_off(led);
  lv_obj_set_size(led, 10, 10);

  lv_obj_t* lbl = lv_label_create(chip);
  lv_label_set_text(lbl, caption);
  lv_obj_add_style(lbl, &styleChip, 0);

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
  lv_obj_set_height(cont, withBar ? 38 : 24);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  row.bullet = lv_obj_create(cont);
  lv_obj_remove_style_all(row.bullet);
  lv_obj_add_style(row.bullet, &styleBullet, 0);
  lv_obj_set_size(row.bullet, 8, 8);
  lv_obj_set_style_bg_color(row.bullet, lv_palette_main(LV_PALETTE_TEAL), 0);
  lv_obj_align(row.bullet, LV_ALIGN_LEFT_MID, 0, withBar ? -6 : 0);

  row.label = lv_label_create(cont);
  lv_label_set_text(row.label, title);
  lv_obj_add_style(row.label, &styleData, 0);
  lv_obj_set_width(row.label, LV_PCT(92));
  lv_label_set_long_mode(row.label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 12, withBar ? -8 : 0);

  if (withBar) {
    row.bar = lv_bar_create(cont);
    lv_bar_set_range(row.bar, 0, 100);
    lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(row.bar, LV_PCT(92), 6);
    lv_obj_align(row.bar, LV_ALIGN_BOTTOM_LEFT, 12, 0);
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
  lv_obj_set_width(h.labelTitle, PGL_SCREEN_W / 2);

  lv_obj_t* ledRow = lv_obj_create(header);
  lv_obj_remove_style_all(ledRow);
  lv_obj_set_size(ledRow, LV_SIZE_CONTENT, LV_PCT(100));
  lv_obj_set_flex_flow(ledRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ledRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ledRow, PGL_UI_HEADER_LED_GAP, 0);
  lv_obj_clear_flag(ledRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(ledRow, LV_ALIGN_RIGHT_MID, -PGL_UI_PAD_X, 0);

  h.ledIr = createStatusChip(ledRow, "IR");
  h.ledWifi = createStatusChip(ledRow, "WiFi");
  h.ledSrv = createStatusChip(ledRow, "Srv");
}

void buildCounterCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 4, 0);

  // Ligne titre + smiley
  lv_obj_t* topRow = lv_obj_create(card);
  lv_obj_remove_style_all(topRow);
  lv_obj_set_width(topRow, LV_PCT(100));
  lv_obj_set_height(topRow, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(topRow);
  lv_label_set_text(title, "Compteurs");
  lv_obj_add_style(title, &styleSection, 0);

  handles.labelSmiley = lv_label_create(topRow);
  lv_label_set_text(handles.labelSmiley, ":-)");
  lv_obj_add_style(handles.labelSmiley, &styleValueHero, 0);

  handles.labelTodayCaption = lv_label_create(card);
  lv_label_set_text(handles.labelTodayCaption, "AUJOURD'HUI");
  lv_obj_add_style(handles.labelTodayCaption, &styleSection, 0);

  handles.labelTodayValue = lv_label_create(card);
  lv_label_set_text(handles.labelTodayValue, "0");
  lv_obj_add_style(handles.labelTodayValue, &styleValueHero, 0);

  handles.labelTotal = lv_label_create(card);
  lv_label_set_text(handles.labelTotal, "Total: 0");
  lv_obj_add_style(handles.labelTotal, &styleData, 0);
}

void buildSensorCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Capteurs");
  lv_obj_add_style(title, &styleSection, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  handles.arcUs = createUsArc(card, PGL_UI_ARC_SIZE);
  lv_obj_align(handles.arcUs, LV_ALIGN_LEFT_MID, 0, 8);

  lv_obj_t* rightCol = lv_obj_create(card);
  lv_obj_remove_style_all(rightCol);
  lv_obj_set_width(rightCol, w - PGL_UI_ARC_SIZE - PGL_UI_CARD_PAD - 4);
  lv_obj_set_height(rightCol, h - PGL_UI_CARD_PAD * 2 - 14);
  lv_obj_align(rightCol, LV_ALIGN_RIGHT_MID, 0, 6);
  lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(rightCol, 6, 0);
  lv_obj_clear_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);

  handles.labelUs = lv_label_create(rightCol);
  lv_label_set_text(handles.labelUs, "US: -");
  lv_obj_add_style(handles.labelUs, &styleData, 0);
  lv_obj_set_width(handles.labelUs, LV_PCT(100));
  lv_label_set_long_mode(handles.labelUs, LV_LABEL_LONG_WRAP);

  handles.labelIr = lv_label_create(rightCol);
  lv_label_set_text(handles.labelIr, "IR: ...");
  lv_obj_add_style(handles.labelIr, &styleData, 0);
  lv_obj_set_width(handles.labelIr, LV_PCT(100));
  lv_label_set_long_mode(handles.labelIr, LV_LABEL_LONG_WRAP);
}

void buildAudioCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);
  handles.cardAudio = card;
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 2, 0);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Lecture");
  lv_obj_add_style(title, &styleSection, 0);

  handles.labelAudio = lv_label_create(card);
  lv_label_set_text(handles.labelAudio, "En attente");
  lv_obj_add_style(handles.labelAudio, &styleData, 0);
  lv_obj_set_width(handles.labelAudio, LV_PCT(100));
  lv_label_set_long_mode(handles.labelAudio, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void buildSystemCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, PglUiHandles& handles) {
  lv_obj_t* card = createCard(parent, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 4, 0);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Systeme");
  lv_obj_add_style(title, &styleSection, 0);

  StatusRow wifiRow = createStatusRow(card, "WiFi: ...", true);
  handles.labelWifi = wifiRow.label;
  handles.barWifi = wifiRow.bar;

  StatusRow srvRow = createStatusRow(card, "Srv: -", true);
  handles.labelServer = srvRow.label;
  handles.barQueue = srvRow.bar;
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
  const lv_coord_t counterH = (bodyH * PGL_UI_COUNTER_H_PCT) / 100;
  const lv_coord_t audioH = bodyH - counterH - PGL_UI_CARD_GAP;
  const lv_coord_t sensorH = (bodyH * PGL_UI_SENSOR_H_PCT) / 100;
  const lv_coord_t systemH = bodyH - sensorH - PGL_UI_CARD_GAP;

  buildCounterCard(lv_scr_act(), leftX, bodyY, leftW, counterH, h);
  buildAudioCard(lv_scr_act(), leftX, bodyY + counterH + PGL_UI_CARD_GAP, leftW, audioH, h);
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

  buildAudioCard(lv_scr_act(), x, y, cardW, PGL_UI_AUDIO_CARD_H, h);
  y += PGL_UI_AUDIO_CARD_H + PGL_UI_CARD_GAP;

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
  /* Police par defaut sur l'ecran racine (pattern lv_demo_widgets) */
  lv_obj_set_style_text_font(parent, fontNormal(), LV_PART_MAIN);

  PglUiHandles handles;
  buildHeader(parent, handles);

#if PGL_UI_PORTRAIT
  layoutPortrait(handles);
#else
  layoutLandscape(handles);
#endif

  return handles;
}
