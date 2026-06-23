// pedalboard.cpp -- the worship pedalboard: a multi-effect chain + web UI.
//
// Built on the proven nam_amp.cpp skeleton (ALSA + SCHED_FIFO audio, lock-free
// atomics, the demo hardware drivers). The single NAM model call becomes a full
// effect Chain, and a cpp-httplib WebServer thread becomes the primary control
// surface (edit params from a phone/laptop on the network).
//
// Domains, sharing state only through atomics:
//   AUDIO (RT)  -- capture mono guitar, run the chain, master + bypass, play out.
//   INPUT (~1k) -- one encoder = master level, footswitch = global bypass.
//   UI (~50Hz)  -- LVGL status + NeoPixel.
//   WEB         -- REST over the chain's Param/PedalControls atomics.
//
// Build:  scripts/build.sh pedalboard
// Deploy: rsync the binary + web/ + presets/ + a .nam to the Pi, run with sudo:
//   ssh -t pistomp@pistomp.local 'sudo ~/app/pedalboard "/home/pistomp/app/SuperReverbNAM/...sm57.nam"'
//   then open http://pistomp.local:8080 from any device on the LAN.

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include "chain.h"
#include "fx_factory.h"
#include "pedal_controls.h"
#include "ui_events.h"
#include "ui/ui_controller.h"
#include "web_server.h"
#include "presets.h"
#include "effects/tuner.h"
#include "effects/input_gain.h"
#include "effects/gate.h"
#include "effects/comp.h"
#include "effects/drive.h"
#include "effects/amp_nam.h"
#include "effects/eq.h"
#include "effects/chorus.h"
#include "effects/delay.h"
#include "effects/reverb.h"

#include "ili9341.h"
#include "lvgl_display.h"
#include "encoder.h"
#include "footswitch.h"
#include "gpio_button.h"
#include "leds.h"

#include "lvgl.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

// ---- audio config (matches nam_amp / nam_passthrough) ----------------------
static const char* DEVICE = "plughw:CARD=IQaudIOCODEC";
static const unsigned RATE = 48000;
static const unsigned CHANNELS = 2;
static const snd_pcm_uframes_t WANT_PERIOD = 64;
static const snd_pcm_uframes_t WANT_BUFFER = 256;
static const int RT_PRIORITY = 80;
static const int MAX_FRAMES = 8192;

// ---- pi-Stomp v3 control mapping (BCM/GPIO; see pistomp/pistomptre.py) ------
// Four rotary controls: a dedicated NAV encoder + 3 tweak knobs. NAV drives the
// on-screen cursor; enc1 push = Back; enc1/2/3 turn edit a pedal's params, and
// enc3 doubles as master/output level everywhere else. All of this is now routed
// through the UiController via a UiEvent queue -- the input thread never touches
// LVGL or the chain directly.
static const int NAV_D = 17,  NAV_CLK = 4;     // navigation encoder rotation
static const int ENC1_D = 12, ENC1_CLK = 25, ENC1_SW = 16;
static const int ENC2_D = 24, ENC2_CLK = 23;   // (push GPIO26, unused for now)
static const int ENC3_D = 22, ENC3_CLK = 27;   // (no push switch on enc3)

// Footswitches FS0..3 ride MCP3008 ch0..3 and map 1:1 to NeoPixels 0..3. A tap
// toggles the effects bound to it (assignment lives in the UI); holding FS3
// opens the tuner. FS3 acts on RELEASE so a hold doesn't also fire a tap.
static const int FS_CHANNEL[4] = {0, 1, 2, 3};
static const std::chrono::milliseconds FS_TUNER_HOLD{2000};

// Navigation encoder push (MCP3008 ch4, rests high ~1022, pressed ~0): a short
// press = select, a QUIT_HOLD hold = exit cleanly back to the boot launcher.
// (Verified on-device; legacy pistomp.py mislabels this as ch7, which on the v3
// board is actually a knob resting near the threshold.)
static const int NAV_SW_CHANNEL = 4;
static const int NAV_SW_THRESHOLD = 512;
static const std::chrono::milliseconds QUIT_HOLD{2000};

static const int WEB_PORT = 8080;

// --- shared state -----------------------------------------------------------
static PedalControls g_ctl;
static Chain g_chain;
static FxFactory g_fx;          // mints FX instances for the grid picker
static EventQueue<64> g_events;  // input thread -> UI thread (lock-free SPSC)
static std::atomic<const char*> g_fatal{nullptr};
static std::mutex g_spi_lock;  // SPI0 shared by LCD (0.0) and footswitch ADC (0.1)

// Audio buffers at file scope (BSS), prefaulted before going RT.
static int16_t g_buf[MAX_FRAMES * CHANNELS];
static float g_L[MAX_FRAMES];
static float g_R[MAX_FRAMES];

struct AudioCtx { snd_pcm_t* cap; snd_pcm_t* play; snd_pcm_uframes_t period; };

static void on_sigint(int) { g_ctl.running.store(false); }

static int configure(snd_pcm_t* pcm, snd_pcm_uframes_t* period_out,
                     snd_pcm_uframes_t* buffer_out) {
  snd_pcm_hw_params_t* hw = nullptr;
  snd_pcm_hw_params_alloca(&hw);
  snd_pcm_hw_params_any(pcm, hw);
  snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
  snd_pcm_hw_params_set_channels(pcm, hw, CHANNELS);
  unsigned rate = RATE;
  snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
  snd_pcm_uframes_t period = WANT_PERIOD;
  snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
  snd_pcm_uframes_t buffer = WANT_BUFFER;
  snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
  int err = snd_pcm_hw_params(pcm, hw);
  if (err < 0) return err;
  snd_pcm_hw_params_get_period_size(hw, &period, 0);
  snd_pcm_hw_params_get_buffer_size(hw, &buffer);
  *period_out = period;
  *buffer_out = buffer;
  return 0;
}

// ---------------- THE REAL-TIME AUDIO THREAD ----------------
// No malloc/printf/locks; chain.process() and every effect obey the same rule.
static void* audio_thread(void* arg) {
  AudioCtx* ctx = static_cast<AudioCtx*>(arg);
  const snd_pcm_uframes_t period = ctx->period;
  const double budget_s = (double)period / RATE;

  while (g_ctl.running.load(std::memory_order_relaxed)) {
    snd_pcm_sframes_t got = snd_pcm_readi(ctx->cap, g_buf, period);
    if (got == -EPIPE) { g_ctl.xruns.fetch_add(1, std::memory_order_relaxed);
                         snd_pcm_prepare(ctx->cap); continue; }
    if (got == -EINTR) continue;
    if (got < 0) { g_fatal.store(snd_strerror((int)got)); g_ctl.running.store(false); break; }

    const float master = g_ctl.masterLevel.load(std::memory_order_relaxed);
    const bool bypass = g_ctl.bypassed.load(std::memory_order_relaxed);

    // Guitar on Aux-Left -> mono float, fanned to both channels to start stereo.
    for (int f = 0; f < (int)got; f++) {
      float x = g_buf[2 * f + 0] / 32768.0f;
      g_L[f] = x;
      g_R[f] = x;
    }

    if (!bypass) {
      auto t0 = std::chrono::steady_clock::now();
      g_chain.process(g_L, g_R, (int)got);
      auto t1 = std::chrono::steady_clock::now();
      double used = std::chrono::duration<double>(t1 - t0).count();
      g_ctl.dspPermille.store((unsigned)(1000.0 * used / budget_s),
                              std::memory_order_relaxed);
    } else {
      g_ctl.dspPermille.store(0, std::memory_order_relaxed);
    }

    // -> S16 stereo, applying master level, with a hard clip guard.
    for (int f = 0; f < (int)got; f++) {
      float l = g_L[f] * master, r = g_R[f] * master;
      int sl = (int)(l * 32768.0f), sr = (int)(r * 32768.0f);
      if (sl > 32767) sl = 32767; else if (sl < -32768) sl = -32768;
      if (sr > 32767) sr = 32767; else if (sr < -32768) sr = -32768;
      g_buf[2 * f + 0] = (int16_t)sl;
      g_buf[2 * f + 1] = (int16_t)sr;
    }

    snd_pcm_sframes_t wrote = snd_pcm_writei(ctx->play, g_buf, got);
    if (wrote == -EPIPE) { g_ctl.xruns.fetch_add(1, std::memory_order_relaxed);
                           snd_pcm_prepare(ctx->play); continue; }
    if (wrote == -EINTR) continue;
    if (wrote < 0) { g_fatal.store(snd_strerror((int)wrote)); g_ctl.running.store(false); break; }
  }
  return nullptr;
}

// ---------------- THE INPUT DOMAIN (~1 kHz) ----------------
// Reads the encoders/switches and POSTS UiEvents -- it never touches LVGL or the
// chain. The UI thread drains the queue and decides what each event means.
static void input_loop() {
  Encoder navEnc, enc1, enc2, enc3;
  GpioButton enc1Btn;
  Footswitch navSw, fs[4];
  bool fsOk = true;
  for (int i = 0; i < 4; i++) fsOk &= fs[i].init(FS_CHANNEL[i]);
  if (!navEnc.init(NAV_D, NAV_CLK, "pb_nav") ||
      !enc1.init(ENC1_D, ENC1_CLK, "pb_enc1") ||
      !enc2.init(ENC2_D, ENC2_CLK, "pb_enc2") ||
      !enc3.init(ENC3_D, ENC3_CLK, "pb_enc3") ||
      !enc1Btn.init(ENC1_SW, "pb_enc1_sw") ||
      !navSw.init(NAV_SW_CHANNEL, NAV_SW_THRESHOLD) || !fsOk) {
    fprintf(stderr, "input init failed (GPIO/SPI in use? run with sudo?)\n");
    g_ctl.running.store(false);
    return;
  }
  navSw.set_spi_lock(&g_spi_lock);
  for (int i = 0; i < 4; i++) fs[i].set_spi_lock(&g_spi_lock);

  // Nav push: short press = NavSelect, hold = quit. Arm only after a release so
  // the launch press (still held from the menu) can't fire select/quit instantly.
  bool navArmed = false, navHolding = false, navQuitFired = false, navDownPrev = false;
  std::chrono::steady_clock::time_point navStart;

  // FS3 is dual-function (tap = footswitch, hold = tuner), so it acts on RELEASE:
  // a release before FS_TUNER_HOLD is a tap; passing the threshold fires the hold
  // and suppresses the tap. FS0..2 are plain press-edge taps.
  bool fs3DownPrev = false, fs3HoldFired = false;
  std::chrono::steady_clock::time_point fs3Start;

  while (g_ctl.running.load()) {
    if (int d = navEnc.poll()) g_events.push({UiEvent::NavRotate, (int8_t)d});
    if (int d = enc1.poll())   g_events.push({UiEvent::Enc1Turn, (int8_t)d});
    if (int d = enc2.poll())   g_events.push({UiEvent::Enc2Turn, (int8_t)d});
    if (int d = enc3.poll())   g_events.push({UiEvent::Enc3Turn, (int8_t)d});
    if (enc1Btn.poll_pressed_edge()) g_events.push({UiEvent::Back, 0});

    for (int i = 0; i < 3; i++)
      if (fs[i].poll_pressed_edge()) g_events.push({UiEvent::Footswitch, (int8_t)i});

    bool fs3Down = fs[3].is_pressed();
    if (fs3Down && !fs3DownPrev) { fs3HoldFired = false; fs3Start = std::chrono::steady_clock::now(); }
    if (!fs3Down && fs3DownPrev && !fs3HoldFired)
      g_events.push({UiEvent::Footswitch, 3});   // tap on release
    if (fs3Down && !fs3HoldFired &&
        std::chrono::steady_clock::now() - fs3Start >= FS_TUNER_HOLD) {
      g_events.push({UiEvent::FsHold, 3});        // hold -> tuner
      fs3HoldFired = true;
    }
    fs3DownPrev = fs3Down;

    bool navDown = navSw.is_pressed();
    if (!navDown) {
      if (navDownPrev && navArmed && !navQuitFired)
        g_events.push({UiEvent::NavSelect, 0});
      navArmed = true;
      navHolding = false;
      navQuitFired = false;
    } else {
      if (!navDownPrev) { navHolding = true; navStart = std::chrono::steady_clock::now(); }
      if (navArmed && navHolding && !navQuitFired &&
          std::chrono::steady_clock::now() - navStart >= QUIT_HOLD) {
        g_ctl.running.store(false);   // clean exit -> launcher shows the menu
        navQuitFired = true;
        break;
      }
    }
    navDownPrev = navDown;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  navEnc.close(); enc1.close(); enc2.close(); enc3.close();
  enc1Btn.close(); navSw.close();
  for (int i = 0; i < 4; i++) fs[i].close();
}

// Resolve a path next to the executable (so web/ + presets/ are found regardless
// of the cwd ssh drops us in).
static std::filesystem::path exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return std::filesystem::current_path();
  buf[n] = '\0';
  return std::filesystem::path(buf).parent_path();
}

int main(int argc, char** argv) {
  std::string modelPath =
      "SuperReverbNAM/Fender Super Reverb_ EQ Flat, Volume 3, sm57.nam";
  if (argc > 1) modelPath = argv[1];

  std::filesystem::path base = exe_dir();
  std::string webDir = (base / "web").string();
  std::string presetDir = (base / "presets").string();

  // --- load + prewarm the NAM model (normal thread; malloc fine here) ---
  printf("Loading NAM model: %s\n", modelPath.c_str());
  std::unique_ptr<nam::DSP> model;
  try {
    model = nam::get_dsp(std::filesystem::path(modelPath));
  } catch (const std::exception& e) {
    fprintf(stderr, "failed to load model (%s) -- continuing with a clean amp\n", e.what());
  }
  std::string ampName =
      model ? std::filesystem::path(modelPath).stem().string() : "Clean (no model)";

  // --- build the chain in the device UI's literal-linear order:
  //       INPUT (level -> gate -> comp) -> FX (drive,chorus,delay,reverb) -> OUTPUT (amp -> eq)
  //     plus the master/output level applied at the very end in the audio thread.
  //     Each effect carries its UI Section so the Input/FX/Output pages can list
  //     "their" pedals. Order lives here in data; the FX middle becomes runtime-
  //     reorderable in phase 2. ---
  // Tuner taps the dry guitar at the very front; off by default, zero cost until
  // engaged. Hidden section: it's a full-screen takeover, not a listed pedal. Kept
  // as a typed pointer so the UI loop can run its (non-RT) pitch detection.
  fx::Tuner* tuner =
      static_cast<fx::Tuner*>(g_chain.add(std::make_unique<fx::Tuner>()));
  tuner->section = Section::Hidden;
  g_chain.add(std::make_unique<fx::InputGain>())->section = Section::Input;
  g_chain.add(std::make_unique<fx::Gate>())->section = Section::Input;
  g_chain.add(std::make_unique<fx::Comp>())->section = Section::Input;
  g_chain.add(std::make_unique<fx::AmpNam>(model.get(), ampName))->section = Section::Output;
  g_chain.add(std::make_unique<fx::EQ>())->section = Section::Output;

  // The FX (middle) region is a fixed grid of slots the user fills at runtime,
  // including duplicates of a kind -- so FX can't be singletons. Register each
  // kind with the factory (the picker mints fresh instances on demand) and
  // pre-fill the first slots so the default chain matches what shipped before.
  g_fx.add("drive",  "Drive",  [] { return std::make_unique<fx::Drive>(); });
  g_fx.add("chorus", "Chorus", [] { return std::make_unique<fx::Chorus>(); });
  g_fx.add("delay",  "Delay",  [] { return std::make_unique<fx::Delay>(); });
  g_fx.add("reverb", "Reverb", [] { return std::make_unique<fx::Reverb>(); });
  for (size_t i = 0; i < g_fx.kinds().size(); i++)
    g_chain.fxPlaceInitial((int)i, g_fx.create(i));   // canonical ids seed the factory

  // Partition prefix/suffix and publish the initial FX order for the audio thread.
  g_chain.finalize();

  // --- ALSA open + configure ---
  snd_pcm_t* cap = nullptr; snd_pcm_t* play = nullptr;
  int err = snd_pcm_open(&cap, DEVICE, SND_PCM_STREAM_CAPTURE, 0);
  if (err < 0) { fprintf(stderr, "open capture: %s\n", snd_strerror(err)); return 1; }
  err = snd_pcm_open(&play, DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) { fprintf(stderr, "open playback: %s\n", snd_strerror(err)); return 1; }

  snd_pcm_uframes_t cap_p, cap_b, play_p, play_b;
  if (configure(cap, &cap_p, &cap_b) < 0 || configure(play, &play_p, &play_b) < 0) {
    fprintf(stderr, "configure failed\n"); return 1;
  }
  snd_pcm_uframes_t period = cap_p < play_p ? cap_p : play_p;
  if ((int)period > MAX_FRAMES) period = MAX_FRAMES;

  // Prepare every effect (sizes buffers, prewarms NAM) BEFORE going RT.
  g_chain.prepare((double)RATE, (int)period);

  {
    snd_pcm_sw_params_t* sw = nullptr;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(play, sw);
    snd_pcm_sw_params_set_start_threshold(play, sw, play_b);
    snd_pcm_sw_params_set_avail_min(play, sw, period);
    if ((err = snd_pcm_sw_params(play, sw)) < 0) {
      fprintf(stderr, "sw_params: %s\n", snd_strerror(err)); return 1;
    }
  }
  snd_pcm_prepare(cap);
  snd_pcm_prepare(play);

  // Optionally start on a default preset (ignored if absent).
  presets::load(presetDir, "Clean Worship", g_chain, g_ctl, g_fx);

  // --- LCD + LVGL (UI thread is main; build widgets before going RT) ---
  Ili9341 lcd;
  if (!lcd.init(/*rotation=*/1)) { fprintf(stderr, "LCD init failed\n"); return 1; }
  lcd.set_spi_lock(&g_spi_lock);
  lvgl_display::init(lcd);
  UiController ui(g_chain, g_ctl, g_fx, tuner, ampName, presetDir);
  ui.begin();

  // --- NeoPixels (optional) ---
  Leds leds;
  bool haveLeds = leds.init();
  if (!haveLeds) fprintf(stderr, "LEDs unavailable (run with sudo for /dev/leds0)\n");

  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
    fprintf(stderr, "warning: mlockall failed (%s) -- raise memlock limit\n", strerror(errno));
  memset(g_buf, 0, sizeof(g_buf));
  memset(g_L, 0, sizeof(g_L));
  memset(g_R, 0, sizeof(g_R));

  // --- web server: the primary control surface ---
  WebServer web(g_chain, g_ctl, g_fx, webDir, presetDir);
  if (web.start("0.0.0.0", WEB_PORT))
    printf("Web UI: http://<pi-ip>:%d/  (serving %s)\n", WEB_PORT, webDir.c_str());

  // Block SIGINT before any worker thread so only main handles Ctrl-C.
  sigset_t sigint_set;
  sigemptyset(&sigint_set);
  sigaddset(&sigint_set, SIGINT);
  pthread_sigmask(SIG_BLOCK, &sigint_set, nullptr);

  // --- spawn the RT audio thread ---
  AudioCtx ctx{cap, play, period};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  struct sched_param sp; sp.sched_priority = RT_PRIORITY;
  pthread_attr_setschedparam(&attr, &sp);
  pthread_t tid;
  int rc = pthread_create(&tid, &attr, audio_thread, &ctx);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    fprintf(stderr, "pthread_create RT failed: %s\n", strerror(rc));
    fprintf(stderr, "  (need rtprio: audio.conf + `audio` group + re-login, or run with sudo)\n");
    return 1;
  }

  std::thread input(input_loop);
  pthread_sigmask(SIG_UNBLOCK, &sigint_set, nullptr);
  signal(SIGINT, on_sigint);

  printf("Pedalboard live: period=%lu, ~%.1f ms each-way, prio=%d. "
         "Nav encoder=navigate, enc1=back, enc3=master. Ctrl-C to stop.\n",
         (unsigned long)period, 1000.0 * play_b / RATE, RT_PRIORITY);

  // --- UI / LED loop (~50 Hz) ---
  while (g_ctl.running.load()) {
    lv_timer_handler();
    tuner->analyze();  // non-RT pitch detection (no-op while disengaged)
    UiEvent ev;
    while (g_events.pop(ev)) ui.handle(ev);   // drain input -> navigate/edit
    ui.refresh();
    if (haveLeds) ui.updateLeds(leds);        // footswitch LED colors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // --- clean shutdown ---
  web.stop();
  input.join();
  pthread_join(tid, nullptr);
  if (const char* f = g_fatal.load()) fprintf(stderr, "audio thread fatal: %s\n", f);

  snd_pcm_drain(play);
  snd_pcm_close(play);
  snd_pcm_close(cap);
  if (haveLeds) leds.close();
  lcd.fill(0x0000);
  lcd.close();
  printf("\nStopped. total xruns: %u\n", g_ctl.xruns.load());
  return 0;
}
