// fx_registry.cpp -- registers every FX kind with the factory.
//
// The FX (middle) region is a fixed grid of slots the user fills at runtime,
// including duplicates of a kind -- so FX can't be singletons. Register every
// kind with the factory (the picker mints fresh instances on demand). Order
// here is the order they appear in the "Add FX" picker.
//
// The chain's fixed prefix/suffix singletons (tuner, input strip, amp, eq,
// output) are NOT registered here -- they are built directly in pedalboard.cpp.

#include "fx_registry.h"

#include "effects/ambient_delay.h"
#include "effects/analog_delay.h"
#include "effects/chorus.h"
#include "effects/delay.h"
#include "effects/detune.h"
#include "effects/dimension.h"
#include "effects/distortion.h"
#include "effects/flanger.h"
#include "effects/fuzz.h"
#include "effects/gold_drive.h"
#include "effects/hall_reverb.h"
#include "effects/multitap_delay.h"
#include "effects/octave.h"
#include "effects/overdrive.h"
#include "effects/phaser.h"
#include "effects/plate_reverb.h"
#include "effects/reverse_delay.h"
#include "effects/rotary.h"
#include "effects/shimmer_reverb.h"
#include "effects/sustainer.h"
#include "effects/swell.h"
#include "effects/tape_echo.h"
#include "effects/tremolo.h"
#include "effects/uni_vibe.h"
#include "effects/vibrato.h"
#include "fx_factory.h"

#include <memory>

void registerAllFx(FxFactory& fx) {
  fx.add("overdrive",  "Overdrive",  [] { return std::make_unique<fx::Overdrive>(); });
  fx.add("golddrive",  "Gold Drive", [] { return std::make_unique<fx::GoldDrive>(); });
  fx.add("distortion", "Distortion", [] { return std::make_unique<fx::Distortion>(); });
  fx.add("fuzz",       "Fuzz",       [] { return std::make_unique<fx::Fuzz>(); });
  fx.add("sustainer",  "Sustainer",  [] { return std::make_unique<fx::Sustainer>(); });
  fx.add("chorus",     "Chorus",     [] { return std::make_unique<fx::Chorus>(); });
  fx.add("dimension",  "Dimension",  [] { return std::make_unique<fx::Dimension>(); });
  fx.add("detune",     "Detune",     [] { return std::make_unique<fx::Detune>(); });
  fx.add("vibrato",    "Vibrato",    [] { return std::make_unique<fx::Vibrato>(); });
  fx.add("tremolo",    "Tremolo",    [] { return std::make_unique<fx::Tremolo>(); });
  fx.add("phaser",     "Phaser",     [] { return std::make_unique<fx::Phaser>(); });
  fx.add("vibe",       "Uni-Vibe",   [] { return std::make_unique<fx::UniVibe>(); });
  fx.add("flanger",    "Flanger",    [] { return std::make_unique<fx::Flanger>(); });
  fx.add("rotary",     "Rotary",     [] { return std::make_unique<fx::Rotary>(); });
  fx.add("octave",     "Octave",     [] { return std::make_unique<fx::Octave>(); });
  fx.add("swell",      "Auto-Swell", [] { return std::make_unique<fx::Swell>(); });
  fx.add("delay",      "Digital",    [] { return std::make_unique<fx::Delay>(); });
  fx.add("tape",       "Tape Echo",  [] { return std::make_unique<fx::TapeEcho>(); });
  fx.add("analog",     "Analog",     [] { return std::make_unique<fx::AnalogDelay>(); });
  fx.add("multitap",   "Multi-Tap",  [] { return std::make_unique<fx::MultiTap>(); });
  fx.add("reverse",    "Reverse",    [] { return std::make_unique<fx::ReverseDelay>(); });
  fx.add("ambient",    "Ambient",    [] { return std::make_unique<fx::AmbientDelay>(); });
  fx.add("hall",       "Hall",       [] { return std::make_unique<fx::Hall>(); });
  fx.add("plate",      "Plate",      [] { return std::make_unique<fx::Plate>(); });
  fx.add("shimmer",    "Shimmer",    [] { return std::make_unique<fx::Shimmer>(); });
}
