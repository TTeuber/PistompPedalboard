// pedalboard.cpp -- the worship pedalboard: a multi-effect chain + web UI.
//
// Built on the proven nam_amp.cpp skeleton (ALSA + SCHED_FIFO audio, lock-free
// atomics, the demo hardware drivers). The single NAM model call becomes a full
// effect Chain, and a cpp-httplib WebServer thread becomes the primary control
// surface (edit params from a phone/laptop on the network).
//
// Domains, sharing state only through atomics:
//   AUDIO (RT)  -- capture mono guitar, run the chain, master + bypass, play
//   out. INPUT (~1k) -- one encoder = master level, footswitch = global bypass.
//   UI (~50Hz)  -- LVGL status + NeoPixel.
//   WEB         -- REST over the chain's Param/PedalControls atomics.
//
// Build:  scripts/build.sh pedalboard
// Deploy: rsync the binary + web/ + rigs/ + presets/ + setlists/ + a .nam to
//         the Pi, run with sudo:
//   ssh -t pistomp@pistomp.local 'sudo ~/app/pedalboard
//   "/home/pistomp/app/SuperReverbNAM/...sm57.nam"' then open
//   http://pistomp.local:8080 from any device on the LAN.

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include "chain.h"
#include "effects/amp_nam.h"
#include "effects/chorus.h"
#include "effects/comp.h"
#include "effects/delay.h"
#include "effects/distortion.h"
#include "effects/eq.h"
#include "effects/flanger.h"
#include "effects/fuzz.h"
#include "effects/gate.h"
#include "effects/input_gain.h"
#include "effects/octave.h"
#include "effects/output_gain.h"
#include "effects/overdrive.h"
#include "effects/phaser.h"
#include "effects/reverb.h"
#include "effects/swell.h"
#include "effects/tremolo.h"
#include "effects/tuner.h"
#include "effects/vibrato.h"
#include "fx_factory.h"
#include "manifest.h"
#include "pedal_controls.h"
#include "rigs.h"
#include "ui/ui_controller.h"
#include "ui_events.h"
#include "web_server.h"

#include "audio_io.h"
#include "board.h"
#include "encoder.h"
#include "footswitch.h"
#include "gpio_button.h"
#include "ili9341.h"
#include "leds.h"
#include "lvgl_display.h"
#include "realtime.h"
#include "sim_menu.h"

#include "lvgl.h"
#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h> // _NSGetExecutablePath (exe_dir on macOS)
#endif

// ---- audio config ----------------------------------------------------------
// Device/rate/period/priority now default inside AudioIO (AudioConfig). The
// sample rate and block size are runtime values (the macOS settings window can
// change them live, see the Settings… menu), so the chain.prepare()/DSP-load
// budget read them off the live AudioConfig rather than a compile-time
// constant. MAX_FRAMES bounds the per-channel float work buffers regardless of
// block size.
static const int MAX_FRAMES = 8192;

// ---- pi-Stomp v3 control behaviour -----------------------------------------
// The WIRING (which GPIO/ADC each control rides, the nav-switch threshold, the
// ch7-vs-ch4 gotcha) now lives in the HAL: see pistomp-hal/board_v3.h, vended
// through Board (which also owns the shared SPI0 lock). Only the app-level
// roles and gesture timings live here. All input is routed through the
// UiController via the UiEvent queue -- the input thread never touches LVGL or
// the chain directly.
//
// Roles: the NAV encoder drives the on-screen cursor (push = select, hold =
// quit to the launcher); enc1 push = Back; enc1/2/3 turns edit a pedal's
// params, and enc3 doubles as master/output level everywhere else. Footswitches
// FS0..3 map 1:1 to NeoPixels 0..3; a tap toggles the effects bound to it
// (assignment lives in the UI), and holding FS3 opens the tuner (FS3 acts on
// RELEASE so a hold doesn't also fire a tap).
static const std::chrono::milliseconds FS_TUNER_HOLD{2000}; // hold FS3 -> tuner
static const std::chrono::milliseconds QUIT_HOLD{2000}; // hold NAV -> launcher
static const std::chrono::milliseconds FS_COMBO_WINDOW{45}; // catch a two-foot stomp

static const int WEB_PORT = 8080;

// --- shared state -----------------------------------------------------------
static PedalControls g_ctl;
static Chain g_chain;
static FxFactory g_fx;          // mints FX instances for the grid picker
static EventQueue<64> g_events; // input thread -> UI thread (lock-free SPSC)
static pistomp::Board g_board;  // owns the SPI0 lock + the v3 control wiring

// Per-channel float work buffers (file scope, prefaulted before RT). The
// interleaved S16 wire buffer now lives inside AudioIO.
static float g_L[MAX_FRAMES];
static float g_R[MAX_FRAMES];

// Smoothed master fader, carried across audio blocks. The fader's raw atomic
// jumps in discrete steps as the web slider drags; applying it as a flat
// per-block multiplier steps the waveform and clicks. One-pole ramp it per
// sample (~20 ms) toward the target so a swept fader glides. Matches the
// in/output-trim smoothing in effects/{input,output}_gain.h.
static float g_master = 1.0f;

static void on_sigint(int) { g_ctl.running.store(false); }

// ---------------- THE INPUT DOMAIN (~1 kHz) ----------------
// Reads the encoders/switches and POSTS UiEvents -- it never touches LVGL or
// the chain. The UI thread drains the queue and decides what each event means.
static void input_loop() {
  Encoder navEnc, enc1, enc2, enc3;
  GpioButton enc1Btn;
  Footswitch navSw, fs[4];
  bool fsOk = true;
  for (int i = 0; i < 4; i++)
    fsOk &= g_board.openFootswitch(fs[i], i);
  if (!g_board.openNavEncoder(navEnc, "pb_nav") ||
      !g_board.openEnc1(enc1, "pb_enc1") ||
      !g_board.openEnc2(enc2, "pb_enc2") ||
      !g_board.openEnc3(enc3, "pb_enc3") ||
      !g_board.openEnc1Button(enc1Btn, "pb_enc1_sw") ||
      !g_board.openNavSwitch(navSw) || !fsOk) {
    fprintf(stderr, "input init failed (GPIO/SPI in use? run with sudo?)\n");
    g_ctl.running.store(false);
    return;
  }

  // Nav push: short press = NavSelect, hold = quit. Arm only after a release so
  // the launch press (still held from the menu) can't fire select/quit
  // instantly.
  bool navArmed = false, navHolding = false, navQuitFired = false,
       navDownPrev = false;
  std::chrono::steady_clock::time_point navStart;

  // Footswitches drive taps, the FS3 tuner-hold, and two-switch rig combos. A
  // tap is DEFERRED by FS_COMBO_WINDOW so a near-simultaneous two-foot stomp
  // registers as a combo (FS1+FS2 = previous rig, FS3+FS4 = next rig) instead of
  // toggling both effects. FS3 keeps its dual role: tap on release, hold -> tuner.
  bool fsDown[4] = {false, false, false, false};
  bool fsDownPrev[4] = {false, false, false, false};
  bool fsPending[4] = {false, false, false, false};   // tap awaiting window/combo
  bool fsConsumed[4] = {false, false, false, false};  // suppressed: went to a combo
  std::chrono::steady_clock::time_point fsStart[4];
  bool fs3HoldFired = false;

  while (g_ctl.running.load()) {
    if (int d = navEnc.poll())
      g_events.push({UiEvent::NavRotate, (int8_t)d});
    if (int d = enc1.poll())
      g_events.push({UiEvent::Enc1Turn, (int8_t)d});
    if (int d = enc2.poll())
      g_events.push({UiEvent::Enc2Turn, (int8_t)d});
    if (int d = enc3.poll())
      g_events.push({UiEvent::Enc3Turn, (int8_t)d});
    if (enc1Btn.poll_pressed_edge())
      g_events.push({UiEvent::Back, 0});

    auto fsNow = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; i++)
      fsDown[i] = fs[i].is_pressed();

    // Press edges: arm a deferred tap.
    for (int i = 0; i < 4; i++)
      if (fsDown[i] && !fsDownPrev[i]) {
        fsStart[i] = fsNow;
        fsPending[i] = true;
        fsConsumed[i] = false;
        if (i == 3) fs3HoldFired = false;
      }

    // Combos: both members of a pair still pending -> step the rig and eat both
    // taps. Pairs are (FS1,FS2) = previous rig and (FS3,FS4) = next rig.
    for (int p = 0; p < 4; p += 2) {
      int a = p, b = p + 1;
      if (fsDown[a] && fsDown[b] && fsPending[a] && fsPending[b]) {
        g_events.push({UiEvent::RigStep, (int8_t)(p == 0 ? -1 : +1)});
        fsPending[a] = fsPending[b] = false;
        fsConsumed[a] = fsConsumed[b] = true;
      }
    }

    // FS0..2: a tap fires once the combo window lapses (still held) or on an
    // earlier release; never when a combo already consumed it.
    for (int i = 0; i < 3; i++) {
      if (fsPending[i] && fsNow - fsStart[i] >= FS_COMBO_WINDOW) {
        g_events.push({UiEvent::Footswitch, (int8_t)i});
        fsPending[i] = false;
      }
      if (!fsDown[i] && fsDownPrev[i]) {
        if (fsPending[i]) g_events.push({UiEvent::Footswitch, (int8_t)i});
        fsPending[i] = false;
        fsConsumed[i] = false;
      }
    }

    // FS3 is dual-function: tap on RELEASE, hold (FS_TUNER_HOLD) -> tuner. The
    // combo window only gates combo eligibility; after it lapses FS3 waits for
    // the hold or the release.
    if (fsDown[3]) {
      if (fsPending[3] && fsNow - fsStart[3] >= FS_COMBO_WINDOW) fsPending[3] = false;
      if (!fs3HoldFired && !fsConsumed[3] && fsNow - fsStart[3] >= FS_TUNER_HOLD) {
        g_events.push({UiEvent::FsHold, 3}); // hold -> tuner
        fs3HoldFired = true;
      }
    } else if (fsDownPrev[3]) {
      if (!fs3HoldFired && !fsConsumed[3])
        g_events.push({UiEvent::Footswitch, 3}); // tap on release
      fsPending[3] = false;
      fsConsumed[3] = false;
    }

    for (int i = 0; i < 4; i++)
      fsDownPrev[i] = fsDown[i];

    bool navDown = navSw.is_pressed();
    if (!navDown) {
      if (navDownPrev && navArmed && !navQuitFired)
        g_events.push({UiEvent::NavSelect, 0});
      navArmed = true;
      navHolding = false;
      navQuitFired = false;
    } else {
      if (!navDownPrev) {
        navHolding = true;
        navStart = std::chrono::steady_clock::now();
      }
      if (navArmed && navHolding && !navQuitFired &&
          std::chrono::steady_clock::now() - navStart >= QUIT_HOLD) {
        g_ctl.running.store(false); // clean exit -> launcher shows the menu
        navQuitFired = true;
        break;
      }
    }
    navDownPrev = navDown;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  navEnc.close();
  enc1.close();
  enc2.close();
  enc3.close();
  enc1Btn.close();
  navSw.close();
  for (int i = 0; i < 4; i++)
    fs[i].close();
}

// Resolve a path next to the executable (so web/ + rigs/ + presets/ + setlists/
// are found regardless of the cwd ssh drops us in).
static std::filesystem::path exe_dir() {
  char buf[4096];
#if defined(__APPLE__)
  // macOS has no /proc; ask dyld for the executable path instead.
  uint32_t sz = sizeof(buf);
  if (_NSGetExecutablePath(buf, &sz) != 0)
    return std::filesystem::current_path();
  return std::filesystem::weakly_canonical(std::filesystem::path(buf))
      .parent_path();
#else
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0)
    return std::filesystem::current_path();
  buf[n] = '\0';
  return std::filesystem::path(buf).parent_path();
#endif
}

// The RT DSP callback, parameterised by the per-block time budget (period/rate)
// so it can be rebuilt when the macOS settings window changes rate or block
// size. Captures only file-scope state (g_ctl/g_chain/g_L/g_R), so it stays
// allocation- and lock-free on the audio thread. in[0] is the mono guitar; fan
// it to stereo, run the chain unless bypassed, apply master, clamp downstream.
static pistomp::AudioCallback makeAudioCallback(double budget_s) {
  return [budget_s](const float *const *in, float *const *out, int n) {
    const float master = g_ctl.masterLevel.load(std::memory_order_relaxed);
    const bool bypass = g_ctl.bypassed.load(std::memory_order_relaxed);
    // Per-sample smoothing coefficient for the master ramp. budget_s spans n
    // frames, so per-sample dt = budget_s/n; tau = 20 ms.
    const float mCoef =
        (float)std::exp(-budget_s / (double(n > 0 ? n : 1) * 0.020));

    for (int f = 0; f < n; f++) {
      g_L[f] = in[0][f];
      g_R[f] = in[0][f];
    }

    // Input level peak-hold for the meter LEDs, both audio inputs. On hardware
    // the meters read a pre-gain analog detector (ADC ch6/7); this digital peak
    // is the simulator's level source. UI thread reads-and-clears each slot.
    float pk[2] = {0.0f, 0.0f};
    for (int f = 0; f < n; f++) {
      float a0 = in[0][f];
      if (a0 < 0)
        a0 = -a0;
      if (a0 > pk[0])
        pk[0] = a0;
      float a1 = in[1][f];
      if (a1 < 0)
        a1 = -a1;
      if (a1 > pk[1])
        pk[1] = a1;
    }
    for (int c = 0; c < 2; c++) {
      float cur = g_ctl.inPeak[c].load(std::memory_order_relaxed);
      while (pk[c] > cur && !g_ctl.inPeak[c].compare_exchange_weak(
                                cur, pk[c], std::memory_order_relaxed)) {
      }
      // Mirror into the web's own peak-hold (separate consumer; see
      // PedalControls). Same lock-free max, independent read-and-clear.
      float curW = g_ctl.inPeakWeb[c].load(std::memory_order_relaxed);
      while (pk[c] > curW && !g_ctl.inPeakWeb[c].compare_exchange_weak(
                                 curW, pk[c], std::memory_order_relaxed)) {
      }
    }

    if (!bypass) {
      auto t0 = std::chrono::steady_clock::now();
      g_chain.process(g_L, g_R, n);
      auto t1 = std::chrono::steady_clock::now();
      g_ctl.dspPermille.store(
          (unsigned)(1000.0 * std::chrono::duration<double>(t1 - t0).count() /
                     budget_s),
          std::memory_order_relaxed);
    } else {
      g_ctl.dspPermille.store(0, std::memory_order_relaxed);
    }

    float opk[2] = {0.0f, 0.0f};
    for (int f = 0; f < n; f++) {
      g_master = mCoef * g_master + (1.0f - mCoef) * master;
      out[0][f] = g_L[f] * g_master;
      out[1][f] = g_R[f] * g_master;
      float o0 = out[0][f];
      if (o0 < 0)
        o0 = -o0;
      if (o0 > opk[0])
        opk[0] = o0;
      float o1 = out[1][f];
      if (o1 < 0)
        o1 = -o1;
      if (o1 > opk[1])
        opk[1] = o1;
    }
    // Output level peak-hold for the web meter, post-master. UI reads-and-clears.
    for (int c = 0; c < 2; c++) {
      float cur = g_ctl.outPeak[c].load(std::memory_order_relaxed);
      while (opk[c] > cur && !g_ctl.outPeak[c].compare_exchange_weak(
                                 cur, opk[c], std::memory_order_relaxed)) {
      }
    }
  };
}

// --- audio settings persistence (macOS simulator) ---------------------------
// The Settings… window's device/rate/buffer choice is remembered in a small
// JSON file next to the executable, so it survives relaunch. Empty device names
// mean "follow the system default". Unknown/unplugged devices fall back to the
// default inside AudioIO, so a stale file never blocks startup.
static std::filesystem::path audioCfgPath(const std::filesystem::path &base) {
  return base / "audio_settings.json";
}
static void loadAudioSettings(const std::filesystem::path &p,
                              pistomp::AudioConfig &cfg) {
  std::ifstream f(p);
  if (!f)
    return;
  try {
    nlohmann::json doc;
    f >> doc;
    cfg.captureName = doc.value("input", std::string{});
    cfg.playbackName = doc.value("output", std::string{});
    cfg.rate = doc.value("rate", cfg.rate);
    cfg.wantPeriod = doc.value("buffer", cfg.wantPeriod);
  } catch (const std::exception &) { /* corrupt file -> keep defaults */
  }
}
static void saveAudioSettings(const std::filesystem::path &p,
                              const pistomp::AudioConfig &cfg) {
  nlohmann::json doc;
  doc["input"] = cfg.captureName;
  doc["output"] = cfg.playbackName;
  doc["rate"] = cfg.rate;
  doc["buffer"] = cfg.wantPeriod;
  std::ofstream f(p);
  if (f)
    f << doc.dump(2) << '\n';
}

int main(int argc, char **argv) {
  std::string modelPath = "NAM_Models/Amp.nam";
  if (argc > 1)
    modelPath = argv[1];

  std::filesystem::path base = exe_dir();
  std::string webDir = (base / "web").string();
  std::string rigDir = (base / "rigs").string();         // whole-chain rigs
  std::string presetDir = (base / "presets").string();   // per-pedal presets
  std::string setlistDir = (base / "setlists").string(); // ordered rig lists

  // Stamp any rig/setlist/preset still on the pre-sync schema with a stable id +
  // timestamps, migrate legacy setlist rig references, and (re)build the library
  // index. Idempotent: a one-time pass on first launch, a no-op thereafter.
  manifest::rebuild(base.string());

  // --- load + prewarm the NAM model (normal thread; malloc fine here) ---
  printf("Loading NAM model: %s\n", modelPath.c_str());
  std::unique_ptr<nam::DSP> model;
  try {
    model = nam::get_dsp(std::filesystem::path(modelPath));
  } catch (const std::exception &e) {
    fprintf(stderr,
            "failed to load model (%s) -- continuing with a clean amp\n",
            e.what());
  }
  std::string ampName = model ? std::filesystem::path(modelPath).stem().string()
                              : "Clean (no model)";

  // --- build the chain in the device UI's literal-linear order:
  //       INPUT (level -> gate -> comp) -> FX (drive,chorus,delay,reverb) ->
  //       OUTPUT (amp -> eq)
  //     plus the master/output level applied at the very end in the audio
  //     thread. Each effect carries its UI Section so the Input/FX/Output pages
  //     can list "their" pedals. Order lives here in data; the FX middle
  //     becomes runtime- reorderable in phase 2. ---
  // Tuner taps the dry guitar at the very front; off by default, zero cost
  // until engaged. Hidden section: it's a full-screen takeover, not a listed
  // pedal. Kept as a typed pointer so the UI loop can run its (non-RT) pitch
  // detection.
  fx::Tuner *tuner =
      static_cast<fx::Tuner *>(g_chain.add(std::make_unique<fx::Tuner>()));
  tuner->section = Section::Hidden;
  g_chain.add(std::make_unique<fx::InputGain>())->section = Section::Input;
  g_chain.add(std::make_unique<fx::Gate>())->section = Section::Input;
  g_chain.add(std::make_unique<fx::Comp>())->section = Section::Input;
  g_chain.add(std::make_unique<fx::AmpNam>(model.get(), ampName))->section =
      Section::Output;
  g_chain.add(std::make_unique<fx::EQ>())->section = Section::Output;
  g_chain.add(std::make_unique<fx::OutputGain>())->section = Section::Output;

  // The FX (middle) region is a fixed grid of slots the user fills at runtime,
  // including duplicates of a kind -- so FX can't be singletons. Register every
  // kind with the factory (the picker mints fresh instances on demand). Order
  // here is the order they appear in the "Add FX" picker.
  g_fx.add("overdrive",  "Overdrive",  [] { return std::make_unique<fx::Overdrive>(); });
  g_fx.add("distortion", "Distortion", [] { return std::make_unique<fx::Distortion>(); });
  g_fx.add("fuzz",       "Fuzz",       [] { return std::make_unique<fx::Fuzz>(); });
  g_fx.add("chorus",     "Chorus",     [] { return std::make_unique<fx::Chorus>(); });
  g_fx.add("vibrato",    "Vibrato",    [] { return std::make_unique<fx::Vibrato>(); });
  g_fx.add("tremolo",    "Tremolo",    [] { return std::make_unique<fx::Tremolo>(); });
  g_fx.add("phaser",     "Phaser",     [] { return std::make_unique<fx::Phaser>(); });
  g_fx.add("flanger",    "Flanger",    [] { return std::make_unique<fx::Flanger>(); });
  g_fx.add("octave",     "Octave",     [] { return std::make_unique<fx::Octave>(); });
  g_fx.add("swell",      "Auto-Swell", [] { return std::make_unique<fx::Swell>(); });
  g_fx.add("delay",      "Delay",      [] { return std::make_unique<fx::Delay>(); });
  g_fx.add("reverb",     "Reverb",     [] { return std::make_unique<fx::Reverb>(); });

  // Pre-fill the first grid slots with a curated default chain (NOT every kind,
  // which would overflow the 8-slot grid now that there are 11). The user can
  // add/remove/reorder from there.
  const char* kDefaultChain[] = {"overdrive", "chorus", "delay", "reverb"};
  for (int slot = 0; slot < (int)(sizeof(kDefaultChain) / sizeof(char*)); slot++)
    g_chain.fxPlaceInitial(slot, g_fx.create(kDefaultChain[slot]));

  // Partition prefix/suffix and publish the initial FX order for the audio
  // thread.
  g_chain.finalize();

  // --- bring up the codec (open/configure + the RT thread live in AudioIO) ---
  pistomp::AudioIO audio;
  pistomp::AudioConfig acfg; // codec defaults: 48 kHz, 2ch, 64/256, prio 80
  // On the Mac, restore the last device/rate/buffer chosen in Settings… (no-op
  // if the file is absent; ignored fields on the Pi codec path).
  loadAudioSettings(audioCfgPath(base), acfg);
  if (!audio.open(acfg)) {
    fprintf(stderr, "audio open failed: %s\n", audio.lastError());
    return 1;
  }
  int period = audio.period();

  // Prepare every effect (sizes buffers, prewarms NAM) BEFORE going RT.
  g_chain.prepare((double)acfg.rate, period);

  // Optionally start on a default rig (ignored if absent).
  rigs::load(rigDir, "Clean Worship", g_chain, g_ctl, g_fx);

  // --- LCD + LVGL (UI thread is main; build widgets before going RT) ---
  Ili9341 lcd;
  if (!g_board.openLcd(lcd, /*rotation=*/1)) {
    fprintf(stderr, "LCD init failed\n");
    return 1;
  }
  lvgl_display::init(lcd);
  UiController ui(g_chain, g_ctl, g_fx, tuner, ampName, base.string());
  ui.begin();

  // --- NeoPixels (optional) ---
  Leds leds;
  bool haveLeds = leds.init();
  if (!haveLeds)
    fprintf(stderr, "LEDs unavailable (run with sudo for /dev/leds0)\n");

  // --- analog input-level detectors (ADC ch6/7) for the meter LEDs. Absent in
  // the sim (available()==false) -> the UI falls back to the digital audio
  // peak. ---
  pistomp::InputLevel inputLevel;
  g_board.openInputLevel(inputLevel);
  ui.setInputLevel(&inputLevel);

  if (!pistomp::realtime::lock_all_memory())
    fprintf(stderr, "warning: mlockall failed -- raise memlock limit\n");
  memset(g_L, 0, sizeof(g_L));
  memset(g_R, 0, sizeof(g_R));

  // --- web server: the primary control surface ---
  WebServer web(g_chain, g_ctl, g_fx, webDir, rigDir, presetDir, setlistDir,
                base.string());
  if (web.start("0.0.0.0", WEB_PORT))
    printf("Web UI: http://<pi-ip>:%d/  (serving %s)\n", WEB_PORT,
           webDir.c_str());

  // Block SIGINT before any worker thread so only main handles Ctrl-C.
  pistomp::realtime::block_signal(SIGINT);

  // --- spawn the RT audio thread: the DSP IS makeAudioCallback's lambda. in[0]
  // is the guitar (Aux-Left); it fans to stereo, runs the chain unless
  // bypassed, applies master. The budget (period/rate) feeds the DSP-load
  // meter. ---
  bool audioOk = audio.start(makeAudioCallback((double)period / acfg.rate));
  if (!audioOk) {
    fprintf(stderr, "audio start failed: %s\n", audio.lastError());
    fprintf(stderr,
            "  (need rtprio: run with sudo, or audio group + audio.conf)\n");
    return 1;
  }

  std::thread input(input_loop);
  pistomp::realtime::unblock_signal(SIGINT);
  signal(SIGINT, on_sigint);

  printf("Pedalboard live: period=%d, prio=%d. "
         "Nav encoder=navigate, enc1=back, enc3=master. Ctrl-C to stop.\n",
         period, acfg.rtPriority);

  // Bring audio up on a config: reopen on whichever chosen devices are present
  // (none -> idle), re-prepare the chain for the (possibly new) block size, and
  // restart the RT callback. Shared by the Settings switch and the reconnect
  // poll below. (reopen() stops the running device internally first.)
  auto bringUpAudio = [&](const pistomp::AudioConfig &c) -> bool {
    if (!audio.reopen(c))
      return false;
    int p = audio.period();
    g_chain.prepare((double)c.rate, p);
    return audio.start(makeAudioCallback((double)p / c.rate));
  };
  // Sentinels shown in the device dropdowns for "no device on this side".
  const std::string kNoInput = "No Input";
  const std::string kNoOutput = "No Output";
  auto deviceListed = [](const std::vector<pistomp::AudioDeviceInfo> &ds,
                         const std::string &name) {
    for (const auto &d : ds)
      if (d.name == name)
        return true;
    return false;
  };

  // --- macOS Settings… menu: pick input/output device, rate, buffer size,
  // live. No-op on the Pi (settings_menu_stub). The model's getters read the
  // current AudioIO state; apply() switches devices/format without dropping the
  // app: stop -> reopen -> re-prepare the chain for the new block -> restart,
  // then persist. On failure it reverts to the previous config so audio stays
  // up (the loop's audio.running() guard would otherwise exit the app). All of
  // this runs on the main thread, nested in the SDL/Cocoa event pump. ---
  {
    // Build a dropdown list: the "no device" sentinel, then the live devices,
    // then -- if the remembered choice is currently disconnected -- the chosen
    // name itself, so a DAW-style "selected but unplugged" device stays visible
    // and selected instead of silently vanishing from the menu.
    auto deviceList = [&](const std::vector<pistomp::AudioDeviceInfo> &ds,
                          const std::string &sentinel,
                          const std::string &chosen) {
      std::vector<std::string> v{sentinel};
      for (const auto &d : ds)
        v.push_back(d.name);
      if (!chosen.empty() &&
          std::find(v.begin(), v.end(), chosen) == v.end())
        v.push_back(chosen);
      return v;
    };
    sim_menu::Model m;
    m.inputs = [&] {
      return deviceList(audio.captureDevices(), kNoInput, acfg.captureName);
    };
    m.outputs = [&] {
      return deviceList(audio.playbackDevices(), kNoOutput, acfg.playbackName);
    };
    m.rates = [] { return std::vector<int>{44100, 48000, 96000}; };
    m.buffers = [] { return std::vector<int>{32, 64, 128, 256, 512}; };
    // Show the remembered choice (empty = the "no device" sentinel), not the
    // momentarily-active device -- the selection sticks across unplug/replug.
    m.currentInput = [&] {
      return acfg.captureName.empty() ? kNoInput : acfg.captureName;
    };
    m.currentOutput = [&] {
      return acfg.playbackName.empty() ? kNoOutput : acfg.playbackName;
    };
    m.currentRate = [&] { return (int)acfg.rate; };
    m.currentBuffer = [&] { return (int)acfg.wantPeriod; };
    m.apply = [&](const std::string &in, const std::string &out, int rate,
                  int buffer) -> bool {
      pistomp::AudioConfig prev = acfg;
      // Map the sentinels back to "no device" (empty name). A real name that's
      // currently disconnected just opens closed -- not an error.
      acfg.captureName = (in == kNoInput) ? std::string{} : in;
      acfg.playbackName = (out == kNoOutput) ? std::string{} : out;
      acfg.rate = (unsigned)rate;
      acfg.wantPeriod = (unsigned)buffer;
      if (bringUpAudio(acfg)) {
        saveAudioSettings(audioCfgPath(base), acfg);
        return true;
      }
      fprintf(stderr, "audio switch failed (%s); reverting\n",
              audio.lastError());
      acfg = prev;
      bringUpAudio(acfg); // best-effort restore so the app keeps running
      return false;
    };
    sim_menu::install(m);
  }

  // --- UI / LED loop (~50 Hz) ---
  auto nextDeviceCheck = std::chrono::steady_clock::now();
  while (g_ctl.running.load() && audio.running()) {
    lv_timer_handler();
    g_ctl.xruns.store(audio.xruns(),
                      std::memory_order_relaxed); // bridge to web telemetry

    // Device reconnect poll (~1 Hz): re-attach a chosen device when it
    // (re)appears and drop it when unplugged -- without ever substituting a
    // different one. Runs on the main thread, where reopen() is safe (same path
    // as the Settings switch). No-op unless the active set actually changed, so
    // steady state costs only an enumeration.
    auto nowt = std::chrono::steady_clock::now();
    if (nowt >= nextDeviceCheck) {
      nextDeviceCheck = nowt + std::chrono::seconds(1);
      bool wantCap = !acfg.captureName.empty() &&
                     deviceListed(audio.captureDevices(), acfg.captureName);
      bool wantPlay = !acfg.playbackName.empty() &&
                      deviceListed(audio.playbackDevices(), acfg.playbackName);
      if (wantCap != audio.captureActive() ||
          wantPlay != audio.playbackActive()) {
        if (!bringUpAudio(acfg)) {
          // A present device failed to init (rare). Fall back to idle so the app
          // stays up; the next tick retries the chosen device.
          fprintf(stderr, "audio reconnect failed (%s); staying idle\n",
                  audio.lastError());
          pistomp::AudioConfig idle = acfg;
          idle.captureName.clear();
          idle.playbackName.clear();
          bringUpAudio(idle);
        }
      }
    }
    tuner->analyze(); // non-RT pitch detection (no-op while disengaged)
    UiEvent ev;
    while (g_events.pop(ev))
      ui.handle(ev); // drain input -> navigate/edit
    ui.refresh();
    if (haveLeds)
      ui.updateLeds(leds); // footswitch LED colors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // --- clean shutdown ---
  web.stop();
  input.join();
  audio.stop(); // joins the RT thread; drain + close on destruct
  if (const char *f = audio.fatalError())
    fprintf(stderr, "audio thread fatal: %s\n", f);

  if (haveLeds)
    leds.close();
  inputLevel.close();
  lcd.fill(0x0000);
  lcd.close();
  printf("\nStopped. total xruns: %u\n", audio.xruns());
  return 0;
}
