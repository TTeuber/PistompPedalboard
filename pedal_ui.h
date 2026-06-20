// pedal_ui.h -- the LCD's LVGL status screen for the pedalboard.
//
// Deliberately read-only status this phase (the web UI does the editing): the
// chain overview (which effects are on), master level, DSP load, and a big
// LIVE/BYPASS banner. build() once, update() each UI frame -- UI thread only
// (LVGL isn't thread-safe). Mirrors nam/nam_ui.h.

#pragma once

class Chain;
struct PedalControls;

namespace pedal_ui {
void build(const char* subtitle, const Chain& chain);
void update(const Chain& chain, const PedalControls& ctl);
}  // namespace pedal_ui
