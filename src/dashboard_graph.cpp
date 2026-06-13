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
void drawGraphScreenOn(DisplayT &display) {
  clearScreen(display);

  const int screenW = display.width();
  const int screenH = display.height();
  const int headerH = 24;
  const int footerH = 18;
  const int chartX = 6;
  const int chartY = headerH + 8;
  const int chartW = screenW - 12;
  const int chartH = screenH - headerH - footerH - 12;

  display.fillRect(0, 0, screenW, headerH, tft.color565(40, 40, 40));
  display.setTextColor(TFT_WHITE, tft.color565(40, 40, 40));
  display.setTextFont(2);
  display.drawString("Dashboard 2/3  Traffic", 8, 5);

  bool isOnline = (wanStatus == "online" || wanStatus == "none");
  display.setTextColor(isOnline ? panelColor(TFT_GREEN) : panelColor(TFT_RED), tft.color565(40, 40, 40));
  display.setTextDatum(TR_DATUM);
  display.drawString(isOnline ? "Online" : "Offline", screenW - 6, 5);
  display.setTextDatum(TL_DATUM);

  display.fillRoundRect(chartX, chartY, chartW, chartH, 6, tft.color565(16, 16, 16));

  const int gx = chartX + 4;
  const int gy = chartY + 4;
  const int gw = chartW - 8;
  const int gh = chartH - 8;

  float maxRxKbps = 1.0f;
  float maxTxKbps = 1.0f;
  for (int i = 0; i < kTrafficPoints; ++i) {
    if (wanRxHistory[i] > maxRxKbps) maxRxKbps = wanRxHistory[i];
    if (wanTxHistory[i] > maxTxKbps) maxTxKbps = wanTxHistory[i];
  }

  for (int i = 1; i < kTrafficPoints; ++i) {
    int x1 = gx + ((i - 1) * (gw - 1)) / (kTrafficPoints - 1);
    int x2 = gx + (i * (gw - 1)) / (kTrafficPoints - 1);

    int rxY1 = gy + gh - 1 - (int)((wanRxHistory[i - 1] / maxRxKbps) * (gh - 1));
    int rxY2 = gy + gh - 1 - (int)((wanRxHistory[i] / maxRxKbps) * (gh - 1));
    display.drawLine(x1, rxY1, x2, rxY2, panelColor(TFT_CYAN));

    int txY1 = gy + gh - 1 - (int)((wanTxHistory[i - 1] / maxTxKbps) * (gh - 1));
    int txY2 = gy + gh - 1 - (int)((wanTxHistory[i] / maxTxKbps) * (gh - 1));
    display.drawLine(x1, txY1, x2, txY2, panelColor(TFT_YELLOW));
  }

  display.setTextFont(2);
  display.setTextColor(panelColor(TFT_CYAN), tft.color565(16, 16, 16));
  display.drawString("RX", gx + 6, gy + 4);
  display.setTextColor(panelColor(TFT_YELLOW), tft.color565(16, 16, 16));
  display.drawString("TX", gx + 42, gy + 4);

  display.setTextFont(1);
  display.setTextColor(TFT_LIGHTGREY, tft.color565(16, 16, 16));
  display.drawString("RTT: " + wanDelay + "   Loss: " + wanLoss, gx + 6, gy + gh - 10);

  display.drawRoundRect(chartX, chartY, chartW, chartH, 6, TFT_DARKGREY);

  const int footerY = screenH - footerH;
  display.fillRect(0, footerY, screenW, footerH, tft.color565(24, 24, 24));
  display.setTextColor(TFT_LIGHTGREY, tft.color565(24, 24, 24));
  display.setTextFont(1);
  display.drawString("Host:" + String(pfSenseHost) + " | IP:" + WiFi.localIP().toString() + " | Uptime:" + lastUpdate, 6, footerY + 5);
}
} // namespace

void drawDashboardGraph() {
#if defined(BOARD_PROFILE_CYD_2432S028)
  drawGraphScreenOn(tft);
#else
  if (!sprite) {
    return;
  }
  sprite->fillSprite(TFT_BLACK);
  drawGraphScreenOn(*sprite);
  sprite->pushSprite(0, 0);
#endif
}
