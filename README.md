# PistompPedalboard

A live multi-effect guitar pedalboard in C++17, built for worship-service use on the [pi-Stomp](https://www.treefallsound.com/) v3 hardware (Raspberry Pi 5). One binary drives the whole instrument: a realtime DSP chain with a neural amp model, the on-device screen/encoders/footswitches, and a phone-friendly web control surface.

The entire app also runs as a **desktop simulator on macOS** — same code, the LVGL UI in an SDL window, audio through CoreAudio, controls on the keyboard — so everything short of the realtime CPU budget is developed and tested without touching the Pi.

## Highlights

- **30+ effects** — delays (analog, tape, multitap, reverse, ambient), reverbs (hall, plate, shimmer), modulation (chorus, flanger, phaser, uni-vibe, rotary, dimension, tremolo, vibrato, detune, octave), drives (overdrive, distortion, fuzz, gold drive), dynamics (compressor, gate, sustainer, swell), EQ, and a tuner. Heavy lifting uses `juce::dsp`; each effect is a small class behind a common `Effect` interface.
- **Neural amp block** — runs [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) `.nam` captures in pure C++ (no Python) on the Pi in realtime.
- **Lock-free chain editing** — the audio thread never takes a lock; chain edits publish via an epoch-based RCU scheme with `shared_ptr` ownership, so effects can be added, reordered, and removed mid-performance without a click. A TSan/ASan-swept stress test exercises the reclamation protocol.
- **Rigs / presets / setlists** — a rig is a board layout, presets are parameter snapshots, setlists order them for a service; all JSON on disk with atomic writes.
- **Tempo sync** — per-rig BPM with tap tempo; delays and tremolo follow note divisions.
- **Two control surfaces, one state** — the on-device LVGL UI (8-slot FX grid, footswitch assignment, IO meter pages) and a Svelte web UI served by the pedal itself, kept live-consistent over Server-Sent Events.

## Architecture

```
                    ┌───────────────────────────────┐
 guitar ──► codec ──►  RT audio thread (SCHED_FIFO) ──► codec ──► amp
                    │   chain.h: RCU snapshot of    │
                    │   Effect* chain, NAM amp      │
                    └──────────────▲────────────────┘
                                   │ lock-free publish
        ┌──────────────────────────┴──────────────────────────┐
        │                     app state                       │
        ├── LVGL UI (screen + encoders + footswitches)        │
        ├── web_server.cpp (cpp-httplib: REST + SSE) ◄── phone│
        └── rigs / presets / setlists (JSON, atomic writes)   │
```

Hardware access goes through [pistomp-hal](https://github.com/TTeuber/pistomp-hal), a JUCE-free HAL split out of this project — it owns the board wiring, SPI-bus locking, display, and the realtime ALSA/CoreAudio duplex loop, and is fetched by tag via CMake FetchContent.

## Building

### macOS simulator

```
brew install cmake ninja sdl2 node
scripts/build-mac.sh                  # everything, into build-mac/
./build-mac/pedalboard_artefacts/pedalboard [model.nam]
```

Keyboard: arrows = nav encoder, Return = select, Q/W A/S Z/X = encoders 1–3, F1–F4 = footswitches.

For the web-UI dev loop (simulator backend + Vite hot reload):

```
scripts/dev-mac.sh        # then open http://localhost:5173
```

### Raspberry Pi

Binaries are built in a `debian:bookworm` arm64 container (native on Apple Silicon — no cross-compiler, and the container's glibc matches the Pi's), then rsynced over:

```
docker compose build      # once
scripts/build.sh pedalboard
scripts/deploy.sh pedalboard --run
```

### Amp captures

`.nam` files aren't distributed with the repo. Drop a capture at `NAM_Models/Amp.nam` (or pass a path as the first argument).

## Tests

A hardware-free Catch2 suite sweeps every registered effect for DSP invariants (NaN/denormal/DC safety, bypass transparency, block-size independence), exercises FX-grid edge cases and rig/preset round-trips, and stress-tests concurrent chain edits against the audio thread:

```
scripts/test-mac.sh          # build + ctest
scripts/test-mac.sh tsan     # ThreadSanitizer: RCU happens-before edges
scripts/test-mac.sh asan     # AddressSanitizer: use-after-free past the grace period
```

The same suite runs in CI on every push, plain and under both sanitizers.

## Hacking on the HAL and the app together

The HAL is pinned by tag, but CMake can point the fetch at a local checkout:

```
cmake -B build-mac -DFETCHCONTENT_SOURCE_DIR_PISTOMP_HAL=/path/to/pistomp-hal
```

## License

MIT — see [LICENSE](LICENSE). NAM model captures and JUCE/LVGL/NAM sources fetched at build time carry their own licenses.
