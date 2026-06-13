#include "dashboard.h"
#include "globals.h"
#include "boards/board_profile.h"
#include <WiFi.h>

namespace {
uint16_t panelColor(uint16_t logicalColor) {
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
  // LilyGO T-Display-S3 panel wiring on this setup needs channel remapping.
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
} // namespace

template <typename DisplayT>
void drawBar(DisplayT &display, int x, int y, int w, int h, int percent, uint16_t color) {
  display.drawRect(x, y, w, h, TFT_DARKGREY);
  display.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
  int fill = (w - 2) * percent / 100;
  display.fillRect(x + 1, y + 1, fill, h - 2, color);
}

template <typename DisplayT>
void clearDashboard(DisplayT &display) {
  display.fillScreen(TFT_BLACK);
}

template <>
void clearDashboard<TFT_eSprite>(TFT_eSprite &display) {
  display.fillSprite(TFT_BLACK);
}

#if defined(BOARD_PROFILE_CYD_2432S028)
template <>
void clearDashboard<TFT_eSPI>(TFT_eSPI &display) {
  const int screenW = display.width();
  const int screenH = display.height();
  display.fillRect(0, 24, screenW, screenH - 24 - 18, TFT_BLACK);
}
#endif

template <typename DisplayT>
void drawDashboardOn(DisplayT &display) {
  clearDashboard(display);

  const int screenW = display.width();
  const int screenH = display.height();
  const int headerH = 24;
  const int footerH = 18;
  const int contentTop = headerH + 4;
  const int contentBottom = screenH - footerH - 1;

#if defined(BOARD_PROFILE_CYD_2432S028)
  const int leftX = 4;
  const int leftLabelX = 4;
  const int leftBarX = 38;
  const int leftValueX = screenW - 32;
  const int leftBarW = leftValueX - leftBarX - 10;
  const int topRowY = contentTop + 2;
  const int rowGap = 30;
  const int statusX = 4;
  const int statusY = 122;
  const int statusW = screenW - 8;
  const int statusH = 44;
  const int graphX = 4;
  const int graphY = statusY + statusH + 8;
  const int graphW = screenW - 8;
  int graphH = contentBottom - graphY + 1;
  if (graphH < 92) {
    graphH = 92;
  }
#else
  const int leftX = 10;
  const int leftLabelX = 10;
  const int leftBarX = 52;
  const int leftBarW = 102;
  const int leftValueX = 160;
  const int topRowY = contentTop + 2;
  const int rowGap = (DASHBOARD_HEIGHT >= 220) ? 30 : 22;
  const int statusX = 198;
  const int statusY = contentTop + 2;
  const int statusW = 114;
  const int statusH = (DASHBOARD_HEIGHT >= 220) ? 52 : 38;
  const int graphX = statusX;
  const int graphY = statusY + statusH + 8;
  const int graphW = statusW;
  int graphH = contentBottom - graphY + 1;
  if (graphH < 28) {
    graphH = 28;
  }
#endif

  // Header
  display.fillRect(0, 0, screenW, headerH, tft.color565(40, 40, 40));
  display.setTextColor(TFT_WHITE, tft.color565(40, 40, 40));
  display.setTextFont(2);
  display.drawString("pfSense Firewall Status", 8, 5);

  // Left metrics layout
  int metricRowY1 = topRowY;
  int metricRowY2 = metricRowY1 + rowGap;
  int metricRowY3 = metricRowY2 + rowGap;

  display.setTextFont(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);

  // CPU
  uint16_t cpuColor = TFT_CYAN;
  display.drawString("CPU", leftLabelX, metricRowY1);
  drawBar(display, leftBarX, metricRowY1 + 2, leftBarW, 12, cpuPercent, panelColor(cpuColor));
  display.drawString(String(cpuPercent) + "%", leftValueX, metricRowY1 - 1);

  // RAM
  display.drawString("RAM", leftLabelX, metricRowY2);
  drawBar(display, leftBarX, metricRowY2 + 2, leftBarW, 12, memPercent, panelColor(TFT_YELLOW));
  display.drawString(String(memPercent) + "%", leftValueX, metricRowY2 - 1);

  // TEMP
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("TEMP", leftLabelX, metricRowY3);
  drawBar(display, leftBarX, metricRowY3 + 2, leftBarW, 12, tempPercent, panelColor(TFT_RED));
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString(tempValue, leftValueX, metricRowY3 - 1);

  // Network info
  int networkRowY = contentBottom - 30;
  if (networkRowY < metricRowY3 + 16) {
    networkRowY = metricRowY3 + 16;
  }
  int lossRowY = networkRowY + 14;
  if (lossRowY > contentBottom - 2) {
    lossRowY = contentBottom - 2;
    networkRowY = lossRowY - 14;
  }

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("RTT", 10, networkRowY);
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.drawString(wanDelay, 42, networkRowY);
  if (wanRttSd != "-") {
    int x = 42 + display.textWidth(wanDelay + " ");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString("RTTsd", x, networkRowY);
    x += display.textWidth("RTTsd ");
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.drawString(wanRttSd, x, networkRowY);
  }

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("Loss", 10, lossRowY);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString(wanLoss, 42, lossRowY);

  // Status box
  bool isOnline = (wanStatus == "online" || wanStatus == "none");
  uint16_t statusColor = isOnline ? panelColor(TFT_GREEN) : panelColor(TFT_RED);

  display.drawRoundRect(statusX, statusY, statusW, statusH, 6, TFT_DARKGREY);
  display.setTextColor(statusColor, TFT_BLACK);
  if (statusH >= 50) {
    display.setTextFont(4);
    display.setTextSize(1);
  } else {
    display.setTextFont(2);
    display.setTextSize(2);
  }
  String onlineLabel = isOnline ? "Online" : "Offline";
  const int statusCenterX = statusX + (statusW / 2);
  const int statusCenterY = statusY + (statusH / 2);
  display.setTextDatum(MC_DATUM);
  display.drawString(onlineLabel, statusCenterX, statusCenterY);
  display.setTextSize(1);
  display.setTextDatum(TL_DATUM);

  // Traffic graph
  if (graphH > 18) {
    display.fillRoundRect(graphX, graphY, graphW, graphH, 6, tft.color565(16, 16, 16));
    int gx = graphX + 4;
    int gy = graphY + 4;
    int gw = graphW - 8;
    int gh = graphH - 8;

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

    display.setTextFont(1);
    display.setTextColor(panelColor(TFT_CYAN), tft.color565(16, 16, 16));
    display.drawString("R", gx + 2, gy + 2);
    display.setTextColor(panelColor(TFT_YELLOW), tft.color565(16, 16, 16));
    display.drawString("T", gx + 9, gy + 2);

    display.drawRoundRect(graphX, graphY, graphW, graphH, 6, TFT_DARKGREY);
  }
#if defined(BOARD_PROFILE_CYD_2432S028)
  // Footer summary for portrait layout
  const int footerY = screenH - footerH;
  display.fillRect(0, footerY, screenW, 18, tft.color565(24, 24, 24));
  display.setTextColor(TFT_LIGHTGREY, tft.color565(24, 24, 24));
  display.setTextFont(1);
  display.drawString("Host: " + String(pfSenseHost), 6, footerY + 4);
  if (screenW >= 260) {
    display.drawString(WiFi.localIP().toString(), 6, footerY + 11);
  }
#else
  // Footer
  const int footerY = screenH - footerH;
  display.fillRect(0, footerY, screenW, footerH, tft.color565(24, 24, 24));
  display.setTextColor(TFT_LIGHTGREY, tft.color565(24, 24, 24));
  display.setTextFont(1);
  // First line: Host and IP
  display.drawString("H:" + String(pfSenseHost) + "  I:" + WiFi.localIP().toString(), 6, footerY + 1);
  // Second line: Uptime
  display.drawString("Up:" + lastUpdate, 6, footerY + 9);
#endif
}

void drawDashboardMain() {
#if defined(BOARD_PROFILE_CYD_2432S028)
  drawDashboardOn(tft);
#else
  if (!sprite) {
    return;
  }
  sprite->fillSprite(TFT_BLACK);
  drawDashboardOn(*sprite);
  sprite->pushSprite(0, 0);
#endif
}

void drawDashboard() {
  drawDashboardMain();
}
