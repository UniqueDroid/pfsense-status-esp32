#include "lvgl_dashboard.h"

#include <lvgl.h>
#include <WiFi.h>

#include "config_portal.h"
#include "globals.h"
#include "pfsense_api.h"

namespace {
constexpr uint16_t kBufferLines = 24;
constexpr uint32_t kButtonDebounceMsLocal = 180;
constexpr uint32_t kUiRefreshMs = 250;
constexpr uint32_t kWifiRetryMsLocal = 10000;

static lv_color_t drawBuf[DASHBOARD_WIDTH * kBufferLines];
static lv_disp_draw_buf_t dispDrawBuf;
static lv_disp_drv_t dispDrv;

static uint32_t lastTickMs = 0;
static uint32_t lastUiRefreshMs = 0;
static uint32_t lastWifiRetry = 0;
static uint32_t lastButtonMs = 0;

static bool lastUpperState = HIGH;
static bool lastLowerState = HIGH;
static uint8_t pageIndex = 0;
static uint8_t brightnessIndex = 3;
static const uint8_t brightnessLevels[] = {40, 96, 160, 255};

static lv_obj_t *pill = nullptr;
static lv_obj_t *wifiIcon = nullptr;
static lv_obj_t *pages[4] = {nullptr, nullptr, nullptr, nullptr};

static lv_obj_t *cpuBar = nullptr;
static lv_obj_t *ramBar = nullptr;
static lv_obj_t *tempBar = nullptr;
static lv_obj_t *cpuVal = nullptr;
static lv_obj_t *ramVal = nullptr;
static lv_obj_t *tempVal = nullptr;

static lv_obj_t *mainStatusCard = nullptr;
static lv_obj_t *mainStatusVal = nullptr;
static lv_obj_t *mainRttVal = nullptr;
static lv_obj_t *mainRttSdLabel = nullptr;
static lv_obj_t *mainRttSdVal = nullptr;
static lv_obj_t *mainLossVal = nullptr;
static lv_obj_t *mainHostVal = nullptr;
static lv_obj_t *mainIpVal = nullptr;
static lv_obj_t *mainChartRxVal = nullptr;
static lv_obj_t *mainChartTxVal = nullptr;
static lv_obj_t *mainTrafficChart = nullptr;
static lv_chart_series_t *mainRxSeries = nullptr;
static lv_chart_series_t *mainTxSeries = nullptr;

static lv_obj_t *chartRxVal = nullptr;
static lv_obj_t *chartTxVal = nullptr;
static lv_obj_t *trafficChart = nullptr;
static lv_chart_series_t *rxSeries = nullptr;
static lv_chart_series_t *txSeries = nullptr;

static lv_obj_t *rttVal = nullptr;
static lv_obj_t *rttSdVal = nullptr;
static lv_obj_t *lossVal = nullptr;
static lv_obj_t *uptimeVal = nullptr;
static lv_obj_t *hostVal = nullptr;

static lv_obj_t *bigCpuBar = nullptr;
static lv_obj_t *bigCpuVal = nullptr;
static lv_obj_t *bigRamBar = nullptr;
static lv_obj_t *bigRamVal = nullptr;
static lv_obj_t *bigTempBar = nullptr;
static lv_obj_t *bigTempVal = nullptr;
static lv_obj_t *bigRttVal = nullptr;

static lv_obj_t *footerHostVal = nullptr;
static lv_obj_t *footerIpVal = nullptr;
// ESP32 / WiFi info screen labels
static lv_obj_t *espInfoSsid = nullptr;
static lv_obj_t *espInfoIp = nullptr;
static lv_obj_t *espInfoMac = nullptr;
static lv_obj_t *espInfoRssi = nullptr;
static lv_obj_t *espInfoGw = nullptr;
static lv_obj_t *espInfoSubnet = nullptr;
static lv_obj_t *espInfoChip = nullptr;
static lv_obj_t *espInfoCpu = nullptr;
static lv_obj_t *espInfoHeap = nullptr;
static lv_obj_t *espInfoFlash = nullptr;
static lv_obj_t *espInfoUptime = nullptr;
// Button long-press state
static uint32_t upperPressStart = 0;
static bool upperIsDown = false;
static uint32_t lowerPressStart = 0;
static bool lowerIsDown = false;
constexpr uint32_t kLongPressMs = 600;
constexpr int kScrollStep = 24;

String formatUptimeLocal(uint32_t uptimeSeconds) {
  uint32_t hours = uptimeSeconds / 3600;
  uint32_t minutes = (uptimeSeconds % 3600) / 60;
  uint32_t seconds = uptimeSeconds % 60;
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
  return String(buffer);
}

void applyBrightness() {
#if defined(TFT_BL)
  analogWrite(TFT_BL, brightnessLevels[brightnessIndex]);
#endif
}

void cycleBrightness() {
  if (brightnessIndex == 0) {
    brightnessIndex = 3;
  } else {
    brightnessIndex--;
  }
  applyBrightness();
}

void flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&colorP->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

lv_obj_t *createValueLabel(lv_obj_t *parent, const char *text, int x, int y, lv_color_t color) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_color(obj, color, 0);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
  lv_obj_align(obj, LV_ALIGN_TOP_LEFT, x, y);
  return obj;
}

void setPage(uint8_t index) {
  pageIndex = index % 4;
  for (int i = 0; i < 4; ++i) {
    if (i == pageIndex) {
      lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  // Page 0 has its own status card; hide global pill, shift WiFi icon to far right.
  if (pageIndex == 0) {
    lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
    if (mainHostVal) lv_obj_add_flag(mainHostVal, LV_OBJ_FLAG_HIDDEN);
    if (mainIpVal) lv_obj_add_flag(mainIpVal, LV_OBJ_FLAG_HIDDEN);
    if (wifiIcon) lv_obj_align(wifiIcon, LV_ALIGN_TOP_RIGHT, -10, 8);
  } else {
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_HIDDEN);
    if (mainHostVal) lv_obj_clear_flag(mainHostVal, LV_OBJ_FLAG_HIDDEN);
    if (mainIpVal) lv_obj_clear_flag(mainIpVal, LV_OBJ_FLAG_HIDDEN);
    if (wifiIcon) lv_obj_align(wifiIcon, LV_ALIGN_TOP_RIGHT, -90, 8);
  }
}

void refreshChartData() {
  float maxVal = 1.0f;
  for (int i = 0; i < kTrafficPoints; ++i) {
    if (wanRxHistory[i] > maxVal) maxVal = wanRxHistory[i];
    if (wanTxHistory[i] > maxVal) maxVal = wanTxHistory[i];
  }

  for (int i = 0; i < kTrafficPoints; ++i) {
    int rx = static_cast<int>((wanRxHistory[i] / maxVal) * 100.0f);
    int tx = static_cast<int>((wanTxHistory[i] / maxVal) * 100.0f);
    if (rxSeries) {
      rxSeries->y_points[i] = rx;
    }
    if (txSeries) {
      txSeries->y_points[i] = tx;
    }
    if (mainRxSeries) {
      mainRxSeries->y_points[i] = rx;
    }
    if (mainTxSeries) {
      mainTxSeries->y_points[i] = tx;
    }
  }
  if (trafficChart) {
    lv_chart_refresh(trafficChart);
  }
  if (mainTrafficChart) {
    lv_chart_refresh(mainTrafficChart);
  }
}

void refreshLiveDataUi() {
  bool isOnline = (wanStatus == "online" || wanStatus == "none");
  lv_label_set_text(pill, isOnline ? "ONLINE" : "OFFLINE");
  lv_obj_set_style_bg_color(pill, isOnline ? lv_color_hex(0x174E2E) : lv_color_hex(0x5A1E1E), 0);
  lv_obj_set_style_text_color(pill, isOnline ? lv_color_hex(0x83F7AF) : lv_color_hex(0xFFB3B3), 0);

  if (mainStatusVal) {
    lv_label_set_text(mainStatusVal, isOnline ? "Online" : "Offline");
    lv_obj_set_style_text_color(mainStatusVal, isOnline ? lv_color_hex(0x83F7AF) : lv_color_hex(0xFFB3B3), 0);
  }
  if (mainStatusCard) {
    lv_obj_set_style_bg_color(mainStatusCard, isOnline ? lv_color_hex(0x174E2E) : lv_color_hex(0x5A1E1E), 0);
    lv_obj_set_style_border_color(mainStatusCard, isOnline ? lv_color_hex(0x2A9D63) : lv_color_hex(0xC85B5B), 0);
  }

  lv_bar_set_value(cpuBar, cpuPercent, LV_ANIM_OFF);
  lv_bar_set_value(ramBar, memPercent, LV_ANIM_OFF);
  lv_bar_set_value(tempBar, tempPercent, LV_ANIM_OFF);
  lv_label_set_text_fmt(cpuVal, "%d%%", cpuPercent);
  lv_label_set_text_fmt(ramVal, "%d%%", memPercent);
  lv_label_set_text(tempVal, tempValue.c_str());

  refreshChartData();
  lv_label_set_text_fmt(chartRxVal, "RX %.1f kbps", wanRxHistory[kTrafficPoints - 1]);
  lv_label_set_text_fmt(chartTxVal, "TX %.1f kbps", wanTxHistory[kTrafficPoints - 1]);
  if (mainChartRxVal) {
    lv_label_set_text_fmt(mainChartRxVal, "R %.1f", wanRxHistory[kTrafficPoints - 1]);
  }
  if (mainChartTxVal) {
    lv_label_set_text_fmt(mainChartTxVal, "T %.1f", wanTxHistory[kTrafficPoints - 1]);
  }

  lv_label_set_text(rttVal, wanDelay.c_str());
  lv_label_set_text(rttSdVal, wanRttSd.c_str());
  lv_label_set_text(lossVal, wanLoss.c_str());
  lv_label_set_text(uptimeVal, lastUpdate.c_str());
  lv_label_set_text(hostVal, pfSenseHost);

  if (mainRttVal) {
    lv_label_set_text(mainRttVal, wanDelay.c_str());
  }
  if (mainRttSdVal) {
    lv_label_set_text(mainRttSdVal, wanRttSd.c_str());
  }
  bool hasRttSd = (wanRttSd != "-");
  if (mainRttSdLabel) {
    if (hasRttSd) {
      lv_obj_clear_flag(mainRttSdLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(mainRttSdLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (mainRttSdVal) {
    if (hasRttSd) {
      lv_obj_clear_flag(mainRttSdVal, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(mainRttSdVal, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (mainLossVal) {
    lv_label_set_text(mainLossVal, wanLoss.c_str());
  }
  if (mainHostVal) {
    lv_label_set_text_fmt(mainHostVal, "Host: %s", pfSenseHost);
  }
  if (mainIpVal) {
    lv_label_set_text_fmt(mainIpVal, "IP: %s", WiFi.localIP().toString().c_str());
  }

  if (bigCpuVal) {
    lv_label_set_text_fmt(bigCpuVal, "%d%%", cpuPercent);
  }
  if (bigCpuBar) {
    lv_bar_set_value(bigCpuBar, cpuPercent, LV_ANIM_OFF);
  }
  if (bigRamVal) {
    lv_label_set_text_fmt(bigRamVal, "%d%%", memPercent);
  }
  if (bigRamBar) {
    lv_bar_set_value(bigRamBar, memPercent, LV_ANIM_OFF);
  }
  if (bigTempVal) {
    lv_label_set_text(bigTempVal, tempValue.c_str());
  }
  if (bigTempBar) {
    lv_bar_set_value(bigTempBar, tempPercent, LV_ANIM_OFF);
  }
  if (bigRttVal) {
    lv_label_set_text(bigRttVal, wanDelay.c_str());
  }

  // Update WiFi icon color based on connection state
  bool wifiConnectedNow = (WiFi.status() == WL_CONNECTED);
  if (wifiIcon) {
    lv_obj_set_style_text_color(wifiIcon,
      wifiConnectedNow ? lv_color_hex(0x83F7AF) : lv_color_hex(0xFF6B6B), 0);
  }
  // Update global footer
  if (footerHostVal) {
    lv_label_set_text_fmt(footerHostVal, "Host: %s", pfSenseHost);
  }
  if (footerIpVal) {
    lv_label_set_text_fmt(footerIpVal, "IP: %s", WiFi.localIP().toString().c_str());
  }
  // Update ESP & WiFi info screen (screen 4)
  if (espInfoSsid)   lv_label_set_text_fmt(espInfoSsid,   "%s", WiFi.SSID().c_str());
  if (espInfoIp)     lv_label_set_text_fmt(espInfoIp,     "%s", WiFi.localIP().toString().c_str());
  if (espInfoMac)    lv_label_set_text_fmt(espInfoMac,    "%s", WiFi.macAddress().c_str());
  if (espInfoRssi)   lv_label_set_text_fmt(espInfoRssi,   "%d dBm", (int)WiFi.RSSI());
  if (espInfoGw)     lv_label_set_text_fmt(espInfoGw,     "%s", WiFi.gatewayIP().toString().c_str());
  if (espInfoSubnet) lv_label_set_text_fmt(espInfoSubnet, "%s", WiFi.subnetMask().toString().c_str());
  if (espInfoHeap)   lv_label_set_text_fmt(espInfoHeap,   "%lu B", (unsigned long)ESP.getFreeHeap());
  if (espInfoUptime) lv_label_set_text_fmt(espInfoUptime, "%s", lastUpdate.c_str());
}

void createUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101318), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "pfSense Firewall Status");
  lv_obj_set_style_text_color(title, lv_color_hex(0xDDE7F2), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  pill = lv_label_create(screen);
  lv_label_set_text(pill, "ONLINE");
  lv_obj_set_style_bg_color(pill, lv_color_hex(0x174E2E), 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(pill, lv_color_hex(0x83F7AF), 0);
  lv_obj_set_style_pad_left(pill, 10, 0);
  lv_obj_set_style_pad_right(pill, 10, 0);
  lv_obj_set_style_pad_top(pill, 4, 0);
  lv_obj_set_style_pad_bottom(pill, 4, 0);
  lv_obj_set_style_radius(pill, 10, 0);
  lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -10, 7);

  wifiIcon = lv_label_create(screen);
  lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifiIcon, lv_color_hex(0x83F7AF), 0);
  lv_obj_set_style_text_font(wifiIcon, &lv_font_montserrat_14, 0);
  lv_obj_align(wifiIcon, LV_ALIGN_TOP_RIGHT, -90, 8);

  for (int i = 0; i < 4; ++i) {
    pages[i] = lv_obj_create(screen);
    lv_obj_remove_style_all(pages[i]);
    lv_obj_set_size(pages[i], DASHBOARD_WIDTH, DASHBOARD_HEIGHT - 28);
    lv_obj_align(pages[i], LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, 0);
  }
  // Screen 4 (pages[3]): enable vertical scrolling for ESP info
  lv_obj_set_scroll_dir(pages[3], LV_DIR_VER);
  lv_obj_set_scrollbar_mode(pages[3], LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *cpuLabel = lv_label_create(pages[0]);
  lv_label_set_text(cpuLabel, "CPU");
  lv_obj_set_style_text_color(cpuLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(cpuLabel, LV_ALIGN_TOP_LEFT, 10, 2);

  cpuBar = lv_bar_create(pages[0]);
  lv_obj_set_size(cpuBar, 102, 12);
  lv_obj_align(cpuBar, LV_ALIGN_TOP_LEFT, 52, 4);
  lv_bar_set_range(cpuBar, 0, 100);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x2AC7D8), LV_PART_INDICATOR);

  lv_obj_t *ramLabel = lv_label_create(pages[0]);
  lv_label_set_text(ramLabel, "RAM");
  lv_obj_set_style_text_color(ramLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(ramLabel, LV_ALIGN_TOP_LEFT, 10, 24);

  ramBar = lv_bar_create(pages[0]);
  lv_obj_set_size(ramBar, 102, 12);
  lv_obj_align(ramBar, LV_ALIGN_TOP_LEFT, 52, 26);
  lv_bar_set_range(ramBar, 0, 100);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0xF5B942), LV_PART_INDICATOR);

  lv_obj_t *tempLabel = lv_label_create(pages[0]);
  lv_label_set_text(tempLabel, "TEMP");
  lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(tempLabel, LV_ALIGN_TOP_LEFT, 10, 46);

  tempBar = lv_bar_create(pages[0]);
  lv_obj_set_size(tempBar, 102, 12);
  lv_obj_align(tempBar, LV_ALIGN_TOP_LEFT, 52, 48);
  lv_bar_set_range(tempBar, 0, 100);
  lv_obj_set_style_bg_color(tempBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(tempBar, lv_color_hex(0xE66565), LV_PART_INDICATOR);

  cpuVal = createValueLabel(pages[0], "0%", 160, 1, lv_color_hex(0xDDE7F2));
  ramVal = createValueLabel(pages[0], "0%", 160, 23, lv_color_hex(0xDDE7F2));
  tempVal = createValueLabel(pages[0], "-", 160, 45, lv_color_hex(0xDDE7F2));

  createValueLabel(pages[0], "RTT", 10, 93, lv_color_hex(0xA8B3C0));
  mainRttVal = createValueLabel(pages[0], "-", 42, 93, lv_color_hex(0xF5B942));
  mainRttSdLabel = createValueLabel(pages[0], "RTTsd", 94, 93, lv_color_hex(0xDDE7F2));
  mainRttSdVal = createValueLabel(pages[0], "-", 146, 93, lv_color_hex(0xF5B942));

  createValueLabel(pages[0], "Loss", 10, 107, lv_color_hex(0xA8B3C0));
  mainLossVal = createValueLabel(pages[0], "-", 42, 107, lv_color_hex(0xDDE7F2));

  mainStatusCard = lv_obj_create(pages[0]);
  lv_obj_set_size(mainStatusCard, 114, 38);
  lv_obj_align(mainStatusCard, LV_ALIGN_TOP_LEFT, 198, 2);
  lv_obj_set_style_radius(mainStatusCard, 6, 0);
  lv_obj_set_style_bg_color(mainStatusCard, lv_color_hex(0x174E2E), 0);
  lv_obj_set_style_border_width(mainStatusCard, 2, 0);
  lv_obj_set_style_border_color(mainStatusCard, lv_color_hex(0x4A545F), 0);
  lv_obj_set_style_pad_all(mainStatusCard, 0, 0);
  lv_obj_set_style_text_font(mainStatusCard, &lv_font_montserrat_14, 0);

  mainStatusVal = lv_label_create(mainStatusCard);
  lv_label_set_text(mainStatusVal, "Online");
  lv_obj_set_style_text_color(mainStatusVal, lv_color_hex(0x83F7AF), 0);
  lv_obj_center(mainStatusVal);

  mainTrafficChart = lv_chart_create(pages[0]);
  lv_obj_set_size(mainTrafficChart, 114, 76);
  lv_obj_align(mainTrafficChart, LV_ALIGN_TOP_LEFT, 198, 48);
  lv_chart_set_type(mainTrafficChart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(mainTrafficChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_point_count(mainTrafficChart, kTrafficPoints);
  lv_obj_set_style_bg_color(mainTrafficChart, lv_color_hex(0x171C24), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(mainTrafficChart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_line_color(mainTrafficChart, lv_color_hex(0x293342), LV_PART_MAIN);
  lv_obj_set_style_border_color(mainTrafficChart, lv_color_hex(0x4A545F), LV_PART_MAIN);
  lv_obj_set_style_border_width(mainTrafficChart, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(mainTrafficChart, 6, LV_PART_MAIN);
  mainRxSeries = lv_chart_add_series(mainTrafficChart, lv_color_hex(0x2AC7D8), LV_CHART_AXIS_PRIMARY_Y);
  mainTxSeries = lv_chart_add_series(mainTrafficChart, lv_color_hex(0xF5B942), LV_CHART_AXIS_PRIMARY_Y);

  mainChartRxVal = createValueLabel(pages[0], "R 0.0", 202, 52, lv_color_hex(0x2AC7D8));
  mainChartTxVal = createValueLabel(pages[0], "T 0.0", 254, 52, lv_color_hex(0xF5B942));

  mainHostVal = createValueLabel(pages[0], "Host: -", 8, 123, lv_color_hex(0x8E9BAC));
  mainIpVal = createValueLabel(pages[0], "IP: -", 188, 123, lv_color_hex(0x8E9BAC));

  trafficChart = lv_chart_create(pages[1]);
  lv_obj_set_size(trafficChart, DASHBOARD_WIDTH - 20, 90);
  lv_obj_align(trafficChart, LV_ALIGN_TOP_LEFT, 10, 14);
  lv_chart_set_type(trafficChart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(trafficChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_point_count(trafficChart, kTrafficPoints);
  lv_obj_set_style_bg_color(trafficChart, lv_color_hex(0x171C24), LV_PART_MAIN);
  lv_obj_set_style_line_color(trafficChart, lv_color_hex(0x293342), LV_PART_MAIN);
  rxSeries = lv_chart_add_series(trafficChart, lv_color_hex(0x2AC7D8), LV_CHART_AXIS_PRIMARY_Y);
  txSeries = lv_chart_add_series(trafficChart, lv_color_hex(0xF5B942), LV_CHART_AXIS_PRIMARY_Y);

  chartRxVal = createValueLabel(pages[1], "RX 0.0 kbps", 10, 112, lv_color_hex(0x2AC7D8));
  chartTxVal = createValueLabel(pages[1], "TX 0.0 kbps", 150, 112, lv_color_hex(0xF5B942));

  lv_obj_t *card1 = lv_obj_create(pages[2]);
  lv_obj_set_size(card1, 150, 56);
  lv_obj_align(card1, LV_ALIGN_TOP_LEFT, 8, 8);
  lv_obj_set_style_radius(card1, 8, 0);
  lv_obj_set_style_bg_color(card1, lv_color_hex(0x171C24), 0);
  lv_obj_set_style_border_color(card1, lv_color_hex(0x2C3645), 0);
  lv_obj_set_style_border_width(card1, 1, 0);
  lv_obj_set_style_pad_all(card1, 6, 0);
  createValueLabel(card1, "CPU", 0, 0, lv_color_hex(0x8E9BAC));
  bigCpuBar = lv_bar_create(card1);
  lv_obj_set_size(bigCpuBar, 138, 10);
  lv_obj_align(bigCpuBar, LV_ALIGN_TOP_LEFT, 0, 14);
  lv_bar_set_range(bigCpuBar, 0, 100);
  lv_obj_set_style_bg_color(bigCpuBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bigCpuBar, lv_color_hex(0x2AC7D8), LV_PART_INDICATOR);
  bigCpuVal = createValueLabel(card1, "0%", 110, 28, lv_color_hex(0xDDE7F2));
  lv_obj_set_style_text_font(bigCpuVal, &lv_font_montserrat_14, 0);

  lv_obj_t *card2 = lv_obj_create(pages[2]);
  lv_obj_set_size(card2, 150, 56);
  lv_obj_align(card2, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_set_style_radius(card2, 8, 0);
  lv_obj_set_style_bg_color(card2, lv_color_hex(0x171C24), 0);
  lv_obj_set_style_border_color(card2, lv_color_hex(0x2C3645), 0);
  lv_obj_set_style_border_width(card2, 1, 0);
  lv_obj_set_style_pad_all(card2, 6, 0);
  createValueLabel(card2, "RAM", 0, 0, lv_color_hex(0x8E9BAC));
  bigRamBar = lv_bar_create(card2);
  lv_obj_set_size(bigRamBar, 138, 10);
  lv_obj_align(bigRamBar, LV_ALIGN_TOP_LEFT, 0, 14);
  lv_bar_set_range(bigRamBar, 0, 100);
  lv_obj_set_style_bg_color(bigRamBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bigRamBar, lv_color_hex(0xF5B942), LV_PART_INDICATOR);
  bigRamVal = createValueLabel(card2, "0%", 110, 28, lv_color_hex(0xDDE7F2));
  lv_obj_set_style_text_font(bigRamVal, &lv_font_montserrat_14, 0);

  lv_obj_t *card3 = lv_obj_create(pages[2]);
  lv_obj_set_size(card3, 150, 56);
  lv_obj_align(card3, LV_ALIGN_TOP_LEFT, 8, 72);
  lv_obj_set_style_radius(card3, 8, 0);
  lv_obj_set_style_bg_color(card3, lv_color_hex(0x171C24), 0);
  lv_obj_set_style_border_color(card3, lv_color_hex(0x2C3645), 0);
  lv_obj_set_style_border_width(card3, 1, 0);
  lv_obj_set_style_pad_all(card3, 6, 0);
  createValueLabel(card3, "TEMP", 0, 0, lv_color_hex(0x8E9BAC));
  bigTempBar = lv_bar_create(card3);
  lv_obj_set_size(bigTempBar, 138, 10);
  lv_obj_align(bigTempBar, LV_ALIGN_TOP_LEFT, 0, 14);
  lv_bar_set_range(bigTempBar, 0, 100);
  lv_obj_set_style_bg_color(bigTempBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bigTempBar, lv_color_hex(0xE66565), LV_PART_INDICATOR);
  bigTempVal = createValueLabel(card3, "-", 110, 28, lv_color_hex(0xDDE7F2));
  lv_obj_set_style_text_font(bigTempVal, &lv_font_montserrat_14, 0);

  lv_obj_t *card4 = lv_obj_create(pages[2]);
  lv_obj_set_size(card4, 150, 56);
  lv_obj_align(card4, LV_ALIGN_TOP_RIGHT, -8, 72);
  lv_obj_set_style_radius(card4, 8, 0);
  lv_obj_set_style_bg_color(card4, lv_color_hex(0x171C24), 0);
  lv_obj_set_style_border_color(card4, lv_color_hex(0x2C3645), 0);
  lv_obj_set_style_border_width(card4, 1, 0);
  lv_obj_set_style_pad_all(card4, 8, 0);
  createValueLabel(card4, "RTT", 0, 0, lv_color_hex(0x8E9BAC));
  bigRttVal = createValueLabel(card4, "-", 0, 20, lv_color_hex(0xF5B942));
  lv_obj_set_style_text_font(bigRttVal, &lv_font_montserrat_14, 0);

  // Screen 4 (pages[3]): scrollable ESP32 & WiFi device info
  {
    int ry = 4;
    const int rowH = 18;
    // WiFi section header
    lv_obj_t *wHdr = lv_label_create(pages[3]);
    lv_label_set_text(wHdr, "WiFi");
    lv_obj_set_style_text_color(wHdr, lv_color_hex(0x2AC7D8), 0);
    lv_obj_set_style_text_font(wHdr, &lv_font_montserrat_14, 0);
    lv_obj_align(wHdr, LV_ALIGN_TOP_LEFT, 8, ry); ry += rowH;
    auto row = [&](const char *key, lv_obj_t *&val) {
      lv_obj_t *kl = lv_label_create(pages[3]);
      lv_label_set_text(kl, key);
      lv_obj_set_style_text_color(kl, lv_color_hex(0x8E9BAC), 0);
      lv_obj_set_style_text_font(kl, &lv_font_montserrat_14, 0);
      lv_obj_align(kl, LV_ALIGN_TOP_LEFT, 8, ry);
      val = lv_label_create(pages[3]);
      lv_label_set_text(val, "-");
      lv_obj_set_style_text_color(val, lv_color_hex(0xDDE7F2), 0);
      lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
      lv_obj_align(val, LV_ALIGN_TOP_LEFT, 115, ry);
      ry += rowH;
    };
    row("SSID:",    espInfoSsid);
    row("IP:",      espInfoIp);
    row("MAC:",     espInfoMac);
    row("RSSI:",    espInfoRssi);
    row("Gateway:", espInfoGw);
    row("Subnet:",  espInfoSubnet);
    // ESP32 section header
    lv_obj_t *eHdr = lv_label_create(pages[3]);
    lv_label_set_text(eHdr, "ESP32");
    lv_obj_set_style_text_color(eHdr, lv_color_hex(0xF5B942), 0);
    lv_obj_set_style_text_font(eHdr, &lv_font_montserrat_14, 0);
    lv_obj_align(eHdr, LV_ALIGN_TOP_LEFT, 8, ry); ry += rowH;
    row("Chip:",      espInfoChip);
    row("CPU:",       espInfoCpu);
    row("Free Heap:", espInfoHeap);
    row("Flash:",     espInfoFlash);
    row("Uptime:",    espInfoUptime);
    // Static values set once
    lv_label_set_text_fmt(espInfoChip,  "%s r%d", ESP.getChipModel(), (int)ESP.getChipRevision());
    lv_label_set_text_fmt(espInfoCpu,   "%u MHz", ESP.getCpuFreqMHz());
    lv_label_set_text_fmt(espInfoFlash, "%lu MB", (unsigned long)(ESP.getFlashChipSize() / (1024UL * 1024UL)));
  }

  // Keep these for page 2 labels/live text updates.
  rttVal = createValueLabel(pages[1], "-", 0, 0, lv_color_hex(0xF5B942));
  rttSdVal = createValueLabel(pages[1], "-", 0, 0, lv_color_hex(0xF5B942));
  lossVal = createValueLabel(pages[1], "-", 0, 0, lv_color_hex(0xF5B942));
  hostVal = createValueLabel(pages[1], "-", 0, 0, lv_color_hex(0xDDE7F2));
  uptimeVal = createValueLabel(pages[1], "00:00:00", 0, 0, lv_color_hex(0xDDE7F2));
  lv_obj_add_flag(rttVal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(rttSdVal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lossVal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(hostVal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(uptimeVal, LV_OBJ_FLAG_HIDDEN);

  // Global footer on all screens
  lv_obj_t *footer = lv_obj_create(screen);
  lv_obj_remove_style_all(footer);
  lv_obj_set_size(footer, DASHBOARD_WIDTH, 16);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(footer, lv_color_hex(0x181F27), 0);

  footerHostVal = createValueLabel(footer, "Host: -", 6, 2, lv_color_hex(0x8E9BAC));
  footerIpVal   = createValueLabel(footer, "IP: -",   168, 2, lv_color_hex(0x8E9BAC));

  setPage(0);
  refreshLiveDataUi();
}
}  // namespace

void initLvglDashboard() {
  lv_init();

  lv_disp_draw_buf_init(&dispDrawBuf, drawBuf, nullptr, DASHBOARD_WIDTH * kBufferLines);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = DASHBOARD_WIDTH;
  dispDrv.ver_res = DASHBOARD_HEIGHT;
  dispDrv.flush_cb = flushCallback;
  dispDrv.draw_buf = &dispDrawBuf;
  lv_disp_drv_register(&dispDrv);

#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
  pinMode(0, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  createUi();
  applyBrightness();
  lastUpperState = digitalRead(14);
  lastLowerState = digitalRead(0);
  lastUiRefreshMs = millis();
  lastTickMs = millis();
}

void loopLvglDashboard() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastTickMs;
  lastTickMs = now;

  wm.process();
  if (wm.server) {
    wm.server->handleClient();
  }

  if (shouldSaveConfig) {
    shouldSaveConfig = false;
    persistFirewallConfig();
    delay(250);
    ESP.restart();
  }

  bool upperState = digitalRead(14);
  bool lowerState = digitalRead(0);

  // Upper button (GPIO14, active LOW)
  if (lastUpperState == HIGH && upperState == LOW) {
    upperPressStart = now;
    upperIsDown = true;
  }
  if (lastUpperState == LOW && upperState == HIGH && upperIsDown) {
    if ((now - upperPressStart) < kLongPressMs && (now - lastButtonMs) >= kButtonDebounceMsLocal) {
      setPage((pageIndex + 1) % 4);
      lastButtonMs = now;
    }
    upperIsDown = false;
  }
  if (upperIsDown && upperState == LOW && (now - upperPressStart) >= kLongPressMs) {
    if (pageIndex == 3 && (now - lastButtonMs) >= 180) {
      lv_obj_scroll_by(pages[3], 0, -kScrollStep, LV_ANIM_OFF);
      lastButtonMs = now;
    }
  }

  // Lower button (GPIO0, active LOW)
  if (lastLowerState == HIGH && lowerState == LOW) {
    lowerPressStart = now;
    lowerIsDown = true;
  }
  if (lastLowerState == LOW && lowerState == HIGH && lowerIsDown) {
    if ((now - lowerPressStart) < kLongPressMs && (now - lastButtonMs) >= kButtonDebounceMsLocal) {
      cycleBrightness();
      lastButtonMs = now;
    }
    lowerIsDown = false;
  }
  if (lowerIsDown && lowerState == LOW && (now - lowerPressStart) >= kLongPressMs) {
    if (pageIndex == 3 && (now - lastButtonMs) >= 180) {
      lv_obj_scroll_by(pages[3], 0, kScrollStep, LV_ANIM_OFF);
      lastButtonMs = now;
    }
  }

  lastUpperState = upperState;
  lastLowerState = lowerState;

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected) {
    if (now - lastWifiRetry >= kWifiRetryMsLocal) {
      lastWifiRetry = now;
      WiFi.reconnect();
    }
    wanStatus = "offline";
  }

  if (wifiConnected && (now - lastPoll) >= kPollMs) {
    lastPoll = now;
    fetchGatewayStatus();
    fetchWanTraffic();
    fetchSystemStatus();
    lastUpdate = formatUptimeLocal(now / 1000);
  }

  if ((now - lastUiRefreshMs) >= kUiRefreshMs) {
    refreshLiveDataUi();
    lastUiRefreshMs = now;
  }

  lv_tick_inc(elapsed);
  lv_timer_handler();
  delay(5);
}
