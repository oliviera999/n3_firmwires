#include "status_bar_renderer.h"

#include "display_view.h"
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

namespace {

uint8_t wifiBars(int8_t rssi) {
  if (rssi >= -60) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -82) return 1;
  return 0;
}

void drawArrow(Adafruit_SSD1306& disp, bool up, int x, int y, int8_t state) {
  bool filled = (state > 0);
  bool failed = (state < 0);

  disp.fillRect(x - 1, y - 7, 8, 8, BLACK);

  if (up) {
    if (filled)
      disp.fillTriangle(x + 3, y - 6, x, y, x + 6, y, WHITE);
    else
      disp.drawTriangle(x + 3, y - 6, x, y, x + 6, y, WHITE);

    if (filled)
      disp.fillRect(x + 2, y - 2, 2, 2, WHITE);
    else
      disp.drawRect(x + 2, y - 2, 2, 2, WHITE);
  } else {
    if (filled)
      disp.fillTriangle(x + 3, y, x, y - 6, x + 6, y - 6, WHITE);
    else
      disp.drawTriangle(x + 3, y, x, y - 6, x + 6, y - 6, WHITE);

    if (filled)
      disp.fillRect(x + 2, y - 4, 2, 2, WHITE);
    else
      disp.drawRect(x + 2, y - 4, 2, 2, WHITE);
  }

  if (failed) {
    disp.drawLine(x, y - 6, x + 6, y, WHITE);
    disp.drawLine(x, y, x + 6, y - 6, WHITE);
  }
}

}  // namespace

void StatusBarRenderer::draw(DisplayView& view, Adafruit_SSD1306& disp, const StatusBarParams& params) {
  // Icône enveloppe (notification mail)
  const int ex = 0;
  const int ey = 56;
  disp.fillRect(ex, ey, 12, 8, BLACK);
  if (params.mailBlink) {
    disp.drawRect(ex, ey, 12, 8, WHITE);
    disp.drawLine(ex, ey, ex + 6, ey + 4, WHITE);
    disp.drawLine(ex + 6, ey + 4, ex + 12, ey, WHITE);
  }

  if (params.otaOverlayActive) {
    char percentStr[12];
    snprintf(percentStr, sizeof(percentStr), "MAJ: %u%%", params.otaPercent);
    view.printClipped(100, 0, percentStr, 1);
  }

  drawArrow(disp, true, 14, 63, params.sendState);
  drawArrow(disp, false, 26, 63, params.recvState);

  const int tx = 38;
  const int ty = 63;
  disp.fillRect(tx - 1, ty - 7, 40, 8, BLACK);

  if (params.tideDir != 0) {
    if (params.tideDir > 0) {
      disp.drawLine(tx + 3, ty - 6, tx + 3, ty, WHITE);
      disp.drawLine(tx, ty - 3, tx + 6, ty - 3, WHITE);
    } else {
      disp.drawLine(tx, ty - 3, tx + 6, ty - 3, WHITE);
    }
  } else {
    disp.drawLine(tx, ty - 4, tx + 6, ty - 4, WHITE);
    disp.drawLine(tx, ty - 2, tx + 6, ty - 2, WHITE);
  }

  view.printClipped(tx + 10, ty - 7, String(params.diffValue), 1);

  const int bx = 108;
  const int by = 62;
  disp.fillRect(bx - 1, by - 6, 16, 8, BLACK);
  uint8_t bars = wifiBars(params.rssi);
  for (uint8_t i = 0; i < 4; ++i) {
    if (i < bars)
      disp.fillRect(bx + i * 3, by - (i * 2), 2, i * 2 + 2, WHITE);
    else
      disp.drawRect(bx + i * 3, by - (i * 2), 2, i * 2 + 2, WHITE);
  }

  if (params.rssi < -120) {
    disp.drawLine(bx - 1, by - 6, bx + 13, by + 1, WHITE);
    disp.drawLine(bx - 1, by + 1, bx + 13, by - 6, WHITE);
  } else {
    const int rx = 88;
    const int ry = 56;
    disp.fillRect(rx, ry, 20, 8, BLACK);
    char rbuf[8];
    snprintf(rbuf, sizeof(rbuf), "%d", static_cast<int>(params.rssi));
    view.printClipped(rx, ry, rbuf, 1);
  }
}












