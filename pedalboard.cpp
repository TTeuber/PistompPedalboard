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
#include "pedal_controls.h"
#include "pedal_ui.h"
#include "web_server.h"
#include "presets.h"
#include "effects/drive.h"
#include "effects/amp_nam.h"
#include "effects/reverb.h"

#include "ili9341.h"
#include "lvgl_display.h"
#include "encoder.h"
#include "footswitch.h"
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

// ---- pi-Stomp v3 control mapping -------------------------------------------
static const int MASTER_D = 24, MASTER_CLK = 23;  // encoder 2 = master level
static const int BYPASS_FS_CHANNEL = 0;            // footswitch 0 = global bypass
static const int BYPASS_LED_INDEX = 0;
static const float MASTER_MAX = 2.0f, MASTER_STEP = 0.05f;

static const int WEB_PORT = 8080;

// --- shared state -----------------------------------------------------------
static PedalControls g_ctl;
static Chain g_chain;
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
static void input_loop() {
  Encoder masterEnc;
  Footswitch bypassFs;
  if (!masterEnc.init(MASTER_D, MASTER_CLK, "pb_master") ||
      !bypassFs.init(BYPASS_FS_CHANNEL)) {
    fprintf(stderr, "input init failed (GPIO/SPI in use? run with sudo?)\n");
    g_ctl.running.store(false);
    return;
  }
  bypassFs.set_spi_lock(&g_spi_lock);

  while (g_ctl.running.load()) {
    if (int d = masterEnc.poll()) {
      float m = g_ctl.masterLevel.load() + d * MASTER_STEP;
      g_ctl.masterLevel.store(std::clamp(m, 0.0f, MASTER_MAX));
    }
    if (bypassFs.poll_pressed_edge())
      g_ctl.bypassed.store(!g_ctl.bypassed.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  masterEnc.close();
  bypassFs.close();
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

  // --- build the chain in classic order (vertical slice: Drive > Amp > Reverb).
  //     Order lives here in data; adding/reordering effects is local to this. ---
  g_chain.add(std::make_unique<fx::Drive>());
  g_chain.add(std::make_unique<fx::AmpNam>(model.get(), ampName));
  g_chain.add(std::make_unique<fx::Reverb>());

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
  presets::load(presetDir, "Clean Worship", g_chain, g_ctl);

  // --- LCD + LVGL (UI thread is main; build widgets before going RT) ---
  Ili9341 lcd;
  if (!lcd.init(/*rotation=*/1)) { fprintf(stderr, "LCD init failed\n"); return 1; }
  lcd.set_spi_lock(&g_spi_lock);
  lvgl_display::init(lcd);
  pedal_ui::build(ampName.c_str(), g_chain);

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
  WebServer web(g_chain, g_ctl, webDir, presetDir);
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
         "Encoder=master, footswitch=bypass. Ctrl-C to stop.\n",
         (unsigned long)period, 1000.0 * play_b / RATE, RT_PRIORITY);

  // --- UI / LED loop (~50 Hz) ---
  bool lastBypass = !g_ctl.bypassed.load();
  while (g_ctl.running.load()) {
    lv_timer_handler();
    pedal_ui::update(g_chain, g_ctl);

    bool b = g_ctl.bypassed.load();
    if (haveLeds && b != lastBypass) {
      leds.clear();
      if (b) leds.set(BYPASS_LED_INDEX, 255, 0, 0);   // red   = bypassed
      else   leds.set(BYPASS_LED_INDEX, 0, 255, 0);   // green = live
      leds.show();
      lastBypass = b;
    }
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
