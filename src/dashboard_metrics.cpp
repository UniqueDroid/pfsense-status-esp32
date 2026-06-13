#include "dashboard.h"
#include "globals.h"
#include "boards/board_profile.h"
#include <WiFi.h>

namespace {
uint16_t panelColor(uint16_t logicalColor) {
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
  switch (logicalColor) {
    case TFT_RED: return TFT_BLUE;
    case TFT_BLUE: return TFT_RED;
    case TFT_YELLOW: return TFT_CYAN;
    case TFT_CYAN: return TFT_YELLOW;
    default: return logicalColor;
  }
#else
  return logicalColor;
#endif
}

template <typename DisplayT>
void clearScreen(DisplayT &display) {
  display.fillScreen(TFT_BLACK);
}

template <>
void clearScreen<TFT_eSprite>(TFT_eSprite &display) {
  display.fillSprite(TFT_BLACK);
}

template <typename DisplayT>
void drawBar(DisplayT &display, int x, int y, int w, int h, int percent, uint16_t color) {
  display.drawRect(x, y, w, h, TFT_DARKGREY);
  display.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
  int fill = (w - 2) * percent / 100;
  display.fillRect(x + 1, y + 1, fill, h - 2, color);
}

template <typename DisplayT>
void drawMetricsScreenOn(DisplayT &display) {
  clearScreen(display);

  const int screenW = display.width();
  const int screenH = display.height();
  const int headerH = 24;
  const int footerH = 18;

  display.fillRect(0, 0, screenW, headerH, tft.color565(40, 40, 40));
  display.setTextColor(TFT_WHITE, tft.color565(40, 40, 40));
  display.setTextFont(2);
  display.drawString("Dashboard 3/3  Metrics", 8, 5);

  bool isOnline = (wanStatus == "online" || wanStatus == "none");
  display.setTextColor(isOnline ? panelColor(TFT_GREEN) : panelColor(TFT_RED), tft.color565(40, 40, 40));
  display.setTextDatum(TR_DATUM);
  display.drawString(isOnline ? "Online" : "Offline", screenW - 6, 5);
  display.setTextDatum(TL_DATUM);

  const int left = 12;
  const int top = headerH + 6;
  const int barX = 70;
  const int barRight = screenW - 54;
  const int barW = barRight - barX;
  const int rowH = 24;
  const int barH = 12;

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);

  int y1 = top;
  display.drawString("CPU", left, y1);
  drawBar(display, barX, y1 + 4, barW, barH, cpuPercent, panelColor(TFT_CYAN));
  display.setTextDatum(TR_DATUM);
  display.drawString(String(cpuPercent) + "%", screenW - 8, y1);
  display.setTextDatum(TL_DATUM);

  int y2 = top + rowH;
  display.drawString("RAM", left, y2);
  drawBar(display, barX, y2 + 4, barW, barH, memPercent, panelColor(TFT_YELLOW));
  display.setTextDatum(TR_DATUM);
  display.drawString(String(memPercent) + "%", screenW - 8, y2);
  display.setTextDatum(TL_DATUM);

  int y3 = top + rowH * 2;
  display.drawString("TEMP", left, y3);
  drawBar(display, barX, y3 + 4, barW, barH, tempPercent, panelColor(TFT_RED));
  display.setTextDatum(TR_DATUM);
  display.drawString(tempValue, screenW - 8, y3);
  display.setTextDatum(TL_DATUM);

  const int infoY = top + rowH * 3 + 2;
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("RTT", left, infoY);
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.drawString(wanDelay, left + 42, infoY);

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("RTTsd", left + 102, infoY);
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.drawString(wanRttSd, left + 154, infoY);

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("Loss", left + 206, infoY);
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.drawString(wanLoss, left + 240, infoY);

  const int footerY = screenH - footerH;
  display.fillRect(0, footerY, screenW, footerH, tft.color565(24, 24, 24));
  display.setTextColor(TFT_LIGHTGREY, tft.color565(24, 24, 24));
  display.setTextFont(1);
  display.drawString("Host:" + String(pfSenseHost) + " | IP:" + WiFi.localIP().toString() + " | Uptime:" + lastUpdate, 6, footerY + 5);
}
} // namespace

void drawDashboardMetrics() {
#if defined(BOARD_PROFILE_CYD_2432S028)
  drawMetricsScreenOn(tft);
#else
  if (!sprite) {
    return;
  }
  sprite->fillSprite(TFT_BLACK);
  drawMetricsScreenOn(*sprite);
  sprite->pushSprite(0, 0);
#endif
}
