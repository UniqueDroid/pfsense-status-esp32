#include "lvgl_prototype.h"

#include <lvgl.h>
#include "globals.h"
#include "config_portal.h"
#include "pfsense_api.h"

namespace {
constexpr uint16_t kBufferLines = 24;
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
static lv_obj_t *pageTag = nullptr;
static lv_obj_t *pageOverview = nullptr;
static lv_obj_t *pageNet = nullptr;
static lv_obj_t *cpuBar = nullptr;
static lv_obj_t *ramBar = nullptr;
static lv_obj_t *tempBar = nullptr;
static lv_obj_t *cpuVal = nullptr;
static lv_obj_t *ramVal = nullptr;
static lv_obj_t *tempVal = nullptr;
static lv_obj_t *rttVal = nullptr;
static lv_obj_t *rttSdVal = nullptr;
static lv_obj_t *lossVal = nullptr;
static lv_obj_t *trafficVal = nullptr;
static lv_obj_t *uptimeVal = nullptr;

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

void updatePageVisibility() {
  if (!pageOverview || !pageNet) {
    return;
  }
  lv_obj_add_flag(pageOverview, pageIndex == 0 ? LV_OBJ_FLAG_HIDDEN : 0);
  lv_obj_add_flag(pageNet, pageIndex == 1 ? LV_OBJ_FLAG_HIDDEN : 0);
  if (pageIndex == 0) {
    lv_obj_clear_flag(pageOverview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pageNet, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(pageTag, "OVERVIEW");
  } else {
    lv_obj_clear_flag(pageNet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pageOverview, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(pageTag, "NETWORK");
  }
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

void refreshLiveDataUi() {
  bool isOnline = (wanStatus == "online" || wanStatus == "none");
  lv_label_set_text(pill, isOnline ? "ONLINE" : "OFFLINE");
  lv_obj_set_style_bg_color(pill, isOnline ? lv_color_hex(0x174E2E) : lv_color_hex(0x5A1E1E), 0);
  lv_obj_set_style_text_color(pill, isOnline ? lv_color_hex(0x83F7AF) : lv_color_hex(0xFFB3B3), 0);

  lv_bar_set_value(cpuBar, cpuPercent, LV_ANIM_OFF);
  lv_bar_set_value(ramBar, memPercent, LV_ANIM_OFF);
  lv_bar_set_value(tempBar, tempPercent, LV_ANIM_OFF);

  lv_label_set_text_fmt(cpuVal, "%d%%", cpuPercent);
  lv_label_set_text_fmt(ramVal, "%d%%", memPercent);
  lv_label_set_text(tempVal, tempValue.c_str());

  lv_label_set_text(rttVal, wanDelay.c_str());
  lv_label_set_text(rttSdVal, wanRttSd.c_str());
  lv_label_set_text(lossVal, wanLoss.c_str());

  lv_label_set_text_fmt(trafficVal, "RX %.1f kbps   TX %.1f kbps", wanRxHistory[kTrafficPoints - 1], wanTxHistory[kTrafficPoints - 1]);
  lv_label_set_text(uptimeVal, lastUpdate.c_str());
}

void createPrototypeUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101318), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "LVGL Prototype v0.2");
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

  pageTag = lv_label_create(screen);
  lv_label_set_text(pageTag, "OVERVIEW");
  lv_obj_set_style_text_color(pageTag, lv_color_hex(0x8E9BAC), 0);
  lv_obj_align(pageTag, LV_ALIGN_TOP_MID, 0, 8);

  pageOverview = lv_obj_create(screen);
  lv_obj_remove_style_all(pageOverview);
  lv_obj_set_size(pageOverview, DASHBOARD_WIDTH, DASHBOARD_HEIGHT - 26);
  lv_obj_align(pageOverview, LV_ALIGN_TOP_LEFT, 0, 26);
  lv_obj_set_style_bg_opa(pageOverview, LV_OPA_TRANSP, 0);

  pageNet = lv_obj_create(screen);
  lv_obj_remove_style_all(pageNet);
  lv_obj_set_size(pageNet, DASHBOARD_WIDTH, DASHBOARD_HEIGHT - 26);
  lv_obj_align(pageNet, LV_ALIGN_TOP_LEFT, 0, 26);
  lv_obj_set_style_bg_opa(pageNet, LV_OPA_TRANSP, 0);

  lv_obj_add_flag(pageNet, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *cpuLabel = lv_label_create(pageOverview);
  lv_label_set_text(cpuLabel, "CPU");
  lv_obj_set_style_text_color(cpuLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(cpuLabel, LV_ALIGN_TOP_LEFT, 10, 40);

  cpuBar = lv_bar_create(pageOverview);
  lv_obj_set_size(cpuBar, DASHBOARD_WIDTH - 90, 14);
  lv_obj_align(cpuBar, LV_ALIGN_TOP_LEFT, 54, 42);
  lv_bar_set_range(cpuBar, 0, 100);
  lv_bar_set_value(cpuBar, 37, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x2AC7D8), LV_PART_INDICATOR);

  lv_obj_t *ramLabel = lv_label_create(pageOverview);
  lv_label_set_text(ramLabel, "RAM");
  lv_obj_set_style_text_color(ramLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(ramLabel, LV_ALIGN_TOP_LEFT, 10, 66);

  ramBar = lv_bar_create(pageOverview);
  lv_obj_set_size(ramBar, DASHBOARD_WIDTH - 90, 14);
  lv_obj_align(ramBar, LV_ALIGN_TOP_LEFT, 54, 68);
  lv_bar_set_range(ramBar, 0, 100);
  lv_bar_set_value(ramBar, 62, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0xF5B942), LV_PART_INDICATOR);

  lv_obj_t *tempLabel = lv_label_create(pageOverview);
  lv_label_set_text(tempLabel, "TEMP");
  lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(tempLabel, LV_ALIGN_TOP_LEFT, 10, 92);

  tempBar = lv_bar_create(pageOverview);
  lv_obj_set_size(tempBar, DASHBOARD_WIDTH - 90, 14);
  lv_obj_align(tempBar, LV_ALIGN_TOP_LEFT, 54, 94);
  lv_bar_set_range(tempBar, 0, 100);
  lv_bar_set_value(tempBar, 32, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(tempBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(tempBar, lv_color_hex(0xE66565), LV_PART_INDICATOR);

  cpuVal = createValueLabel(pageOverview, "0%", DASHBOARD_WIDTH - 34, 40, lv_color_hex(0xDDE7F2));
  ramVal = createValueLabel(pageOverview, "0%", DASHBOARD_WIDTH - 34, 66, lv_color_hex(0xDDE7F2));
  tempVal = createValueLabel(pageOverview, "-", DASHBOARD_WIDTH - 34, 92, lv_color_hex(0xDDE7F2));

  lv_obj_t *rttLabel = createValueLabel(pageNet, "RTT", 10, 40, lv_color_hex(0xA8B3C0));
  (void)rttLabel;
  rttVal = createValueLabel(pageNet, "-", 72, 40, lv_color_hex(0xF5B942));

  lv_obj_t *rttsdLabel = createValueLabel(pageNet, "RTTsd", 10, 64, lv_color_hex(0xA8B3C0));
  (void)rttsdLabel;
  rttSdVal = createValueLabel(pageNet, "-", 72, 64, lv_color_hex(0xF5B942));

  lv_obj_t *lossLabel = createValueLabel(pageNet, "Loss", 10, 88, lv_color_hex(0xA8B3C0));
  (void)lossLabel;
  lossVal = createValueLabel(pageNet, "-", 72, 88, lv_color_hex(0xF5B942));

  trafficVal = createValueLabel(pageNet, "RX 0.0 kbps   TX 0.0 kbps", 10, 116, lv_color_hex(0xDDE7F2));
  uptimeVal = createValueLabel(pageNet, "00:00:00", 10, 138, lv_color_hex(0x8E9BAC));

  lv_obj_t *hint = lv_label_create(screen);
  lv_label_set_text(hint, "Branch: feature/lvgl-prototype");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x667084), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 10, -8);

  refreshLiveDataUi();
}
}  // namespace

void initLvglPrototype() {
  lv_init();

  lv_disp_draw_buf_init(&dispDrawBuf, drawBuf, nullptr, DASHBOARD_WIDTH * kBufferLines);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = DASHBOARD_WIDTH;
  dispDrv.ver_res = DASHBOARD_HEIGHT;
  dispDrv.flush_cb = flushCallback;
  dispDrv.draw_buf = &dispDrawBuf;
  lv_disp_drv_register(&dispDrv);

  createPrototypeUi();
  applyBrightness();
  lastUpperState = digitalRead(14);
  lastLowerState = digitalRead(0);
  lastUiRefreshMs = millis();
  lastTickMs = millis();
}

void loopLvglPrototype() {
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

  if (lastUpperState == LOW && upperState == HIGH && (now - lastButtonMs) >= 180) {
    pageIndex = (pageIndex + 1) % 2;
    updatePageVisibility();
    lastButtonMs = now;
  }
  if (lastLowerState == HIGH && lowerState == LOW && (now - lastButtonMs) >= 180) {
    cycleBrightness();
    lastButtonMs = now;
  }
  lastUpperState = upperState;
  lastLowerState = lowerState;

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected) {
    if (now - lastWifiRetry >= 10000) {
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

  if ((now - lastUiRefreshMs) >= 250) {
    refreshLiveDataUi();
    lastUiRefreshMs = now;
  }

  lv_tick_inc(elapsed);
  lv_timer_handler();
  delay(5);
}
