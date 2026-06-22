// pedal_ui.cpp -- see pedal_ui.h.

#include "pedal_ui.h"

#include "chain.h"
#include "effect.h"
#include "pedal_controls.h"
#include "effects/tuner.h"

#include "lvgl.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
// --- normal status view ---
lv_obj_t* master_label = nullptr;
lv_obj_t* master_bar = nullptr;
lv_obj_t* chain_label = nullptr;
lv_obj_t* dsp_label = nullptr;
lv_obj_t* status_label = nullptr;

// --- tuner view (shown only while the tuner is engaged) ---
lv_obj_t* tuner_title = nullptr;
lv_obj_t* tuner_note = nullptr;   // big note name, e.g. "E"
lv_obj_t* tuner_cents = nullptr;  // symmetrical -50..+50 cents bar
lv_obj_t* tuner_info = nullptr;   // "+3 cents   82.4 Hz"

constexpr int MASTER_BAR_MAX = 200;  // masterLevel 0..2.0 -> 0..200%

const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                              "F#", "G",  "G#", "A",  "A#", "B"};

void show(lv_obj_t* o, bool v) {
  if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else   lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
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

  // --- tuner view (built once, hidden until engaged) ---
  tuner_title = lv_label_create(scr);
  lv_label_set_text(tuner_title, "TUNER");
  lv_obj_set_style_text_color(tuner_title, lv_color_hex(0x00C8FF), 0);
  lv_obj_align(tuner_title, LV_ALIGN_TOP_MID, 0, 10);

  tuner_note = lv_label_create(scr);
  lv_obj_set_style_text_font(tuner_note, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(tuner_note, lv_color_white(), 0);
  lv_obj_align(tuner_note, LV_ALIGN_CENTER, 0, -24);
  lv_label_set_text(tuner_note, "--");

  tuner_cents = lv_bar_create(scr);
  lv_obj_set_size(tuner_cents, 288, 18);
  lv_obj_align(tuner_cents, LV_ALIGN_CENTER, 0, 16);
  lv_bar_set_mode(tuner_cents, LV_BAR_MODE_SYMMETRICAL);  // fills out from centre
  lv_bar_set_range(tuner_cents, -50, 50);
  lv_bar_set_value(tuner_cents, 0, LV_ANIM_OFF);

  tuner_info = lv_label_create(scr);
  lv_obj_set_style_text_color(tuner_info, lv_color_hex(0x909090), 0);
  lv_obj_align(tuner_info, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_label_set_text(tuner_info, "");

  show(tuner_title, false);
  show(tuner_note, false);
  show(tuner_cents, false);
  show(tuner_info, false);
}

namespace {
void update_status(const Chain& chain, const PedalControls& ctl) {
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

void update_tuner(const fx::Tuner* t) {
  int note = t->noteIndex();
  if (note < 0) {  // no confident pitch yet
    lv_label_set_text(tuner_note, "--");
    lv_obj_set_style_text_color(tuner_note, lv_color_hex(0x707070), 0);
    lv_bar_set_value(tuner_cents, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tuner_cents, lv_color_hex(0x505050), LV_PART_INDICATOR);
    lv_label_set_text(tuner_info, "play a note...");
    return;
  }

  float cents = t->cents();
  bool inTune = std::fabs(cents) <= 5.0f;
  bool close  = std::fabs(cents) <= 15.0f;
  lv_color_t col = inTune ? lv_color_hex(0x30FF30)
                          : (close ? lv_color_hex(0xFFC000) : lv_color_hex(0xFF3030));

  char nm[8];
  snprintf(nm, sizeof nm, "%s%d", kNoteNames[note], t->octave());
  lv_label_set_text(tuner_note, nm);
  lv_obj_set_style_text_color(tuner_note, col, 0);

  lv_bar_set_value(tuner_cents, (int)lroundf(cents), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(tuner_cents, col, LV_PART_INDICATOR);

  char buf[48];
  snprintf(buf, sizeof buf, "%+d cents   %.1f Hz", (int)lroundf(cents), t->freqHz());
  lv_label_set_text(tuner_info, buf);
}
}  // namespace

void pedal_ui::update(const Chain& chain, const PedalControls& ctl) {
  // The tuner, when engaged, takes over the whole screen.
  const fx::Tuner* tuner = static_cast<const fx::Tuner*>(
      const_cast<Chain&>(chain).find("tuner"));
  bool tuning = tuner && tuner->engaged();

  show(master_label, !tuning);
  show(master_bar, !tuning);
  show(chain_label, !tuning);
  show(dsp_label, !tuning);
  show(status_label, !tuning);
  show(tuner_title, tuning);
  show(tuner_note, tuning);
  show(tuner_cents, tuning);
  show(tuner_info, tuning);

  if (tuning) update_tuner(tuner);
  else        update_status(chain, ctl);
}
