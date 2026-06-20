// pedal_ui.cpp -- see pedal_ui.h.

#include "pedal_ui.h"

#include "chain.h"
#include "effect.h"
#include "pedal_controls.h"

#include "lvgl.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
lv_obj_t* master_label = nullptr;
lv_obj_t* master_bar = nullptr;
lv_obj_t* chain_label = nullptr;
lv_obj_t* dsp_label = nullptr;
lv_obj_t* status_label = nullptr;

constexpr int MASTER_BAR_MAX = 200;  // masterLevel 0..2.0 -> 0..200%
}  // namespace

void pedal_ui::build(const char* subtitle, const Chain& chain) {
  (void)chain;
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "PISTOMP PEDALBOARD");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00C8FF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  lv_obj_t* sub = lv_label_create(scr);
  lv_label_set_text(sub, subtitle);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x909090), 0);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(sub, 300);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 26);

  master_label = lv_label_create(scr);
  lv_obj_set_style_text_color(master_label, lv_color_white(), 0);
  lv_obj_align(master_label, LV_ALIGN_TOP_LEFT, 16, 50);
  master_bar = lv_bar_create(scr);
  lv_obj_set_size(master_bar, 288, 12);
  lv_obj_align(master_bar, LV_ALIGN_TOP_LEFT, 16, 70);
  lv_bar_set_range(master_bar, 0, MASTER_BAR_MAX);

  chain_label = lv_label_create(scr);
  lv_obj_set_style_text_color(chain_label, lv_color_hex(0xC0C0C0), 0);
  lv_label_set_long_mode(chain_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(chain_label, 288);
  lv_obj_align(chain_label, LV_ALIGN_TOP_LEFT, 16, 92);

  dsp_label = lv_label_create(scr);
  lv_obj_set_style_text_color(dsp_label, lv_color_hex(0x909090), 0);
  lv_obj_align(dsp_label, LV_ALIGN_TOP_LEFT, 16, 150);

  status_label = lv_label_create(scr);
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_28, 0);
  lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_label_set_text(status_label, "LIVE");
}

void pedal_ui::update(const Chain& chain, const PedalControls& ctl) {
  char buf[96];

  int masterPct = (int)lroundf(ctl.masterLevel.load() * 100.0f);
  snprintf(buf, sizeof buf, "Master: %d%%", masterPct);
  lv_label_set_text(master_label, buf);
  lv_bar_set_value(master_bar, masterPct, LV_ANIM_OFF);

  // One line: "Drive* > Amp > Reverb*"  (* = engaged), in chain order.
  std::string line;
  for (const auto& fx : chain.effects()) {
    if (!line.empty()) line += " > ";
    line += fx->name();
    if (fx->enabled.load()) line += "*";
  }
  lv_label_set_text(chain_label, line.c_str());

  unsigned load = ctl.dspPermille.load();
  unsigned xr = ctl.xruns.load();
  snprintf(buf, sizeof buf, "DSP %u.%u%%   xruns %u", load / 10, load % 10, xr);
  lv_label_set_text(dsp_label, buf);

  bool bypassed = ctl.bypassed.load();
  lv_label_set_text(status_label, bypassed ? "BYPASS" : "LIVE");
  lv_obj_set_style_text_color(
      status_label, bypassed ? lv_color_hex(0xFF3030) : lv_color_hex(0x30FF30), 0);
}
