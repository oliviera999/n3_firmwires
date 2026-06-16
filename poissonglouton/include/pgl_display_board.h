#pragma once

// Configuration materielle ecran — selectionnee par build flag PlatformIO.
// pgl-s3-display  -> PGL_BOARD_JC4827W543
// pgl-s3-jc3248   -> PGL_BOARD_JC3248W535

#if defined(PGL_BOARD_JC3248W535) && PGL_BOARD_JC3248W535

#define PGL_DISPLAY_BOARD_NAME "JC3248W535"
#define PGL_SCREEN_W 320
#define PGL_SCREEN_H 480
#define PGL_PANEL_DRIVER_AXS15231B 1
#define PGL_PANEL_IPS 0
#define PGL_CANVAS_USE_ROTATION_ARGS 1
#define PGL_LV_FLUSH_USE_BERGB 1
#define PGL_TOUCH_AXS15231B 1
#define PGL_UI_PORTRAIT 1

// Layout UI portrait (cartes empilees)
#define PGL_UI_PAD_X 8
#define PGL_UI_PAD_Y 6
#define PGL_UI_HEADER_H 52
#define PGL_UI_BODY_Y (PGL_UI_HEADER_H + 4)
#define PGL_UI_CARD_PAD 6
#define PGL_UI_CARD_RADIUS 8
#define PGL_UI_CARD_GAP 6
#define PGL_UI_COUNTER_CARD_H 148
#define PGL_UI_SENSOR_CARD_H 128
#define PGL_UI_ARC_SIZE 72

#elif defined(PGL_BOARD_JC4827W543) && PGL_BOARD_JC4827W543

#define PGL_DISPLAY_BOARD_NAME "JC4827W543"
#define PGL_SCREEN_W 480
#define PGL_SCREEN_H 272
#define PGL_PANEL_DRIVER_NV3041A 1
#define PGL_PANEL_IPS 1
#define PGL_CANVAS_USE_ROTATION_ARGS 0
#define PGL_LV_FLUSH_USE_BERGB 0
#define PGL_TOUCH_AXS15231B 0
#define PGL_UI_PORTRAIT 0

// Layout UI paysage (cartes 2 colonnes)
#define PGL_UI_PAD_X 6
#define PGL_UI_PAD_Y 6
#define PGL_UI_HEADER_H 36
#define PGL_UI_BODY_Y (PGL_UI_HEADER_H + 2)
#define PGL_UI_CARD_PAD 6
#define PGL_UI_CARD_RADIUS 8
#define PGL_UI_CARD_GAP 6
#define PGL_UI_LEFT_W_PCT 52
#define PGL_UI_ARC_SIZE 64

#else
#error "Build display : definir -DPGL_BOARD_JC4827W543=1 ou -DPGL_BOARD_JC3248W535=1"
#endif

// Bus QSPI commun aux deux modules Guition
#define PGL_GFX_BL 1
#define PGL_QSPI_CS 45
#define PGL_QSPI_SCK 47
#define PGL_QSPI_D0 21
#define PGL_QSPI_D1 48
#define PGL_QSPI_D2 40
#define PGL_QSPI_D3 39
