# Pedalboard Roadmap

Items identified in the 2026-07-05 codebase review, to tackle later. (The five
correctness/hardening fixes from that review -- FX-order reclamation epoch,
shared_ptr effect lifetime, path sanitization, atomic saves, output limiter --
are done and not listed here.)

## Missing features (roughly in order of value)

### 1. Runtime NAM model switching + per-rig amp captures
The biggest product gap: the model is a CLI arg, one per session, and `AmpNam`
holds a borrowed pointer for life. Wanted:
- A model browser: list `.nam` files in a models dir via the web API.
- Load on a worker thread (NAM parse + prewarm is slow), then atomically swap
  into the amp block using the same epoch-retire pattern the FX grid uses.
- Store the capture name in the rig, so switching rigs switches amps -- right
  now rigs can't change amps, which is half of what a rig *is*.

### 2. Cab IR loader (fixed-partition convolution)
Pairs with #1 -- amp-only captures need a cabinet. Already on the FX_NOTES
suggestion list. Classic DSP showcase piece.

### 3. MIDI
- Program change -> rig select; CC -> any `Param` (the self-describing param
  list makes generic MIDI-learn almost free); MIDI clock -> `tempo::bpm`.
- ALSA rawmidi/seq on the Pi; CoreMIDI or just skip on the Mac sim.
- Table stakes on commercial multi-FX units.

### 4. Looper
The flagship "big" feature: RT-safe record/overdub/undo into preallocated
buffers, footswitch-driven, very demo-able.

### 5. Session recording to WAV
Capture post-chain (and optionally dry) audio through a lock-free SPSC ring
buffer to a writer thread. Practically useful for worship sets; textbook
lock-free design to write about.

### 6. Spillover on rig change
Delay/reverb tails currently die when a rig load replaces grid instances.
- Cheap version: short output crossfade around `rigs::apply`.
- Premium version: old instances keep ringing out while the new rig fades in.

### 7. Small ones
- Device-side tap tempo (tap is web-only today; make it assignable to a
  footswitch). LVGL tempo page is already planned separately.
- Metronome/click tied to the existing BPM.
- Show round-trip latency in the UI (period and rate are already known).

## Portfolio scaffolding (highest ROI for job applications)

### Tests
Catch2 v3 + ctest are wired in (tests/, BUILD_TESTING default ON; run `ctest`
in the build dir). FX registration was extracted to fx_registry.cpp so the
suite sweeps every registered kind automatically. DONE: offline DSP invariants
(bypass null test, tail decay, no-NaN/no-inf sweeps across every param at
min/mid/max, silence-in/silence-out), param clamping/metadata, factory
id-uniqueness + createRestored counter tests. Still to do:
- Rig/preset round-trip serialization tests.
- fxReorder/fxMoveTo edge cases.
- A threaded chain-edit stress test (audio-thread simulator + hammering grid
  edits) run under TSan/ASan -- this is the test that would have caught the
  reclamation bug. Needs -DSANITIZE=thread|address build configs.

### CI -- no .github/ exists
GitHub Actions matrix:
- macOS build (build-mac path) + Docker arm64 build (docker/ already exists).
- Run the test suite; ASan/TSan job.
- clang-format check. Badges in the README.

### A real README for the pedalboard app
- Architecture diagram: the four domains (AUDIO/INPUT/UI/WEB) + atomics, the
  RCU-style chain, the param system.
- Screenshot/GIF of the web UI; short demo video.
- "Design decisions" section -- chain.h and FX_NOTES.md already read like
  ADRs; lift that material up. This matters more than any feature: it is the
  only part most reviewers will read.

## Noted non-issue

No FTZ/denormal handling exists anywhere, but on ARM (Pi + Apple Silicon)
subnormals are cheap -- unlike x86 there is no reverb-tail CPU spike. A
one-line FPCR flush-to-zero on the audio thread is optional insurance at most.
