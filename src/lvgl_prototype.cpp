#include "lvgl_prototype.h"

#include <lvgl.h>
#include "globals.h"

namespace {
constexpr uint16_t kBufferLines = 24;
static lv_color_t drawBuf[DASHBOARD_WIDTH * kBufferLines];
static lv_disp_draw_buf_t dispDrawBuf;
static lv_disp_drv_t dispDrv;
static uint32_t lastTickMs = 0;

void flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&colorP->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void createPrototypeUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101318), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "LVGL Prototype v0.1");
  lv_obj_set_style_text_color(title, lv_color_hex(0xDDE7F2), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  lv_obj_t *pill = lv_label_create(screen);
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

  lv_obj_t *cpuLabel = lv_label_create(screen);
  lv_label_set_text(cpuLabel, "CPU");
  lv_obj_set_style_text_color(cpuLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(cpuLabel, LV_ALIGN_TOP_LEFT, 10, 40);

  lv_obj_t *cpuBar = lv_bar_create(screen);
  lv_obj_set_size(cpuBar, DASHBOARD_WIDTH - 90, 14);
  lv_obj_align(cpuBar, LV_ALIGN_TOP_LEFT, 54, 42);
  lv_bar_set_range(cpuBar, 0, 100);
  lv_bar_set_value(cpuBar, 37, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x2AC7D8), LV_PART_INDICATOR);

  lv_obj_t *ramLabel = lv_label_create(screen);
  lv_label_set_text(ramLabel, "RAM");
  lv_obj_set_style_text_color(ramLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(ramLabel, LV_ALIGN_TOP_LEFT, 10, 66);

  lv_obj_t *ramBar = lv_bar_create(screen);
  lv_obj_set_size(ramBar, DASHBOARD_WIDTH - 90, 14);
  lv_obj_align(ramBar, LV_ALIGN_TOP_LEFT, 54, 68);
  lv_bar_set_range(ramBar, 0, 100);
  lv_bar_set_value(ramBar, 62, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0xF5B942), LV_PART_INDICATOR);

  lv_obj_t *hint = lv_label_create(screen);
  lv_label_set_text(hint, "Branch: feature/lvgl-prototype");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x667084), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 10, -8);
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
  lastTickMs = millis();
}

void loopLvglPrototype() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastTickMs;
  lastTickMs = now;

  lv_tick_inc(elapsed);
  lv_timer_handler();
  delay(5);
}
