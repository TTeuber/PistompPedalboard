// footswitch_control.h -- one place that turns a latched footswitch into effect
// state. (Named *_control to avoid colliding with pistomp-hal's footswitch.h,
// the hardware ADC driver class.)
//
// A footswitch (FS1..FS4) is "engaged" or not (PedalControls::fsEngaged). Every
// effect can bind to one footswitch (Effect::fsAssign) in normal or inverted mode
// (Effect::fsMode): normal = on when the switch is engaged, inverted = on when it
// is NOT. This helper pushes the latch out to the `enabled` flag of every effect
// bound to one switch.
//
// It is shared by the device UI (ui_controller.cpp) and the web server
// (web_server.cpp) so a tap on the hardware and a click in the browser go through
// the exact same logic -- there is no second copy to drift.

#pragma once

#include "chain.h"
#include "pedal_controls.h"

// Sync `enabled` for every effect bound to footswitch `fs` (0..3) from its
// current latched state. Safe on any non-audio thread (atomic stores only).
inline void applyFootswitch(Chain& chain, PedalControls& ctl, int fs) {
  if (fs < 0 || fs > 3) return;
  const bool engaged = ctl.fsEngaged[fs].load();
  for (auto* e : chain.effects())
    if (e->fsAssign.load() == fs)
      e->enabled.store(engaged ^ (e->fsMode.load() == 1));
}

// Flip footswitch `fs` and apply it. Returns the new latched state.
inline bool toggleFootswitch(Chain& chain, PedalControls& ctl, int fs) {
  if (fs < 0 || fs > 3) return false;
  const bool now = !ctl.fsEngaged[fs].load();
  ctl.fsEngaged[fs].store(now);
  applyFootswitch(chain, ctl, fs);
  return now;
}
