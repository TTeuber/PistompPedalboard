# FX DSP Notes

Working notes for replacing the placeholder DSP with more professional,
custom, better-sounding effects. Tackled incrementally.

## Done

- **Parameter smoothing** (all effects): audible-path gains/mixes/depths are
  one-pole smoothed per sample (`Smoother` in `dsp_util.h`, ~20 ms) so swept
  knobs glide instead of stepping per block. EQ band gains smooth at block
  rate (~50 ms) since they act through biquad coefficients. Chorus/Phaser/
  Reverb rely on JUCE's internal smoothing.
- **Click-free bypass**: toggling an effect crossfades dry<->wet over ~10 ms
  (`Effect::run()` in `effect.h`, driven by `chain.h`) instead of hard-
  switching. Footswitches never click.
- **Tails**: Delay and Reverb report `hasTails()`; when bypassed they keep
  processing with their input faded out, so echoes/washes ring out naturally
  instead of being amputated. (Side effect: Reverb Freeze sustains through
  bypass — treat as a feature.) Cost: both keep running while bypassed.
- **Delay time glide**: Time/Sync/Div changes glide the read tap (rate-capped
  one-pole, ~100 ms, max 0.5 samples/sample) — the tape-machine pitch bend
  instead of a click.

- **Reverb collection** (replaces Freeverb): three hand-rolled algorithmic
  reverbs sharing `effects/reverb_dsp.h` (Schroeder/Dattorro `Allpass`, `Delay1`
  multitap, `HighCut`, 8-line Householder `FDN8`, granular `PitchShift`):
  - **Hall** (`hall_reverb.h`) — modulated FDN, Valhalla-style lush tail
    (size/decay/pre-delay/damping/mod/rate/low-cut/width).
  - **Plate** (`plate_reverb.h`) — Dattorro 1997 figure-8 tank, bright & dense.
  - **Shimmer** (`shimmer_reverb.h`) — the FDN with an octave (+12/-12/dual)
    pitch-shifter in the feedback path for the ever-rising ambient wash.
    Old `juce::Reverb` (Freeverb) removed; `hall` is the new default-chain reverb.

- **Delay collection** (absorbs old suggestion "Delay — analog character"): the
  single delay became a six-pedal collection sharing `effects/delay_dsp.h`
  (`GlideValue` rate-capped time glide, `Lfo`, `Jitter`, `WowFlutter`,
  `softLimit` loop ceiling, `Freeze` ramp + recipe, `HighPass1`, `targetMs`):
  - **Digital** (`delay.h`, keeps kind `"delay"` + old param ids so presets
    restore) — the old echo plus a 100 Hz loop highpass (repeats shed lows),
    Spread (R up to +10% time), Freeze, 2 s max time.
  - **Tape Echo** (new, `tape_echo.h`) — wow/flutter transport modulation
    (independently-phased L/R = free stereo drift), write-side head-bump/
    rolloff/HP/tanh color scaled by one Age knob, feedback to 110% for
    bounded self-oscillation.
  - **Analog** (new, `analog_delay.h`) — BBD: loop bandwidth follows the
    glided time (6 kHz at 150 ms → 1.2 kHz at 800 ms) through a 4th-order
    Butterworth, Memory-Man chorus mod on the read tap (stays on during
    freeze — the loop "breathes").
  - **Multi-Tap** (new, `multitap_delay.h`) — mono loop on `rv::Delay1`'s
    non-advancing `tap()`, six rhythm patterns (dotted-8th default, Golden
    = phi-spaced ambient wash), constant-power pans scaled by Width.
  - **Reverse** (new, `reverse_delay.h`) — dual Hann-windowed heads whose
    delay grows 2 samples/sample (the PitchShift trick at "ratio −1") =
    backwards playback with a silent wrap point; feedback re-reverses each
    pass; Spread offsets the R window phase.
  - **Ambient** (new, `ambient_delay.h`) — allpass diffusers + damping +
    orthogonal cross-rotation in the loop: repeats smear into a cloud.
    Freeze showcase: diffusion/rotation are lossless so the frozen slice
    circulates at exactly unity while the wobbled allpass taps keep
    rearranging it — an evolving pad, not a static loop.
  - **All freezable pedals** (not Reverse): input muted, loop gain snaps to
    exactly unity, lossy coloring crossfaded out, all through a ~50 ms ramp
    (`dly::Freeze`) — click-free, holds through bypass via hasTails().
    `dly::softLimit` (unity slope below ~−6 dBFS, ceiling +8 dB) guards
    every loop, so >100% feedback plateaus instead of blowing up.

- **Drive collection** (was suggestion 3): the three placeholder drives became
  a five-pedal collection with real per-circuit topology, sharing
  `effects/drive_dsp.h` (`OversamplerIIR`, `DcBlock`, `EnvFollower`,
  `HighPass1`, `ScoopTone`, clip primitives); `drive_base.h` deleted:
  - **Overdrive** — TS-style: 720 Hz HP _into_ a feedback-clipper identity
    (`u + tanh(g·hp(u))` → mid-hump, tight lows), 6 kHz post rolloff,
    parallel clean Blend knob.
  - **Gold Drive** (new, `gold_drive.h`) — Klon-style dual path: clean boost
    crossfades into a 350 Hz-HP'd hard clipper (mix = drive², so the low
    third of the knob stays a transparent boost), ±8 dB treble shelf.
  - **Distortion** — RAT-style: +12..+60 dB into asymmetric hard clip
    (+1.0/−0.85) with a post DC blocker (the old unblocked-DC bug is dead),
    swept "Filter" lowpass.
  - **Fuzz** — Fuzz Face-style: envelope follower (3/120 ms) drives a bias
    shift into `biasClip` — gated sputter when hit hard, cleans up as the
    input drops; Bias knob scales it; no input HP on purpose.
  - **Sustainer** (new, `sustainer.h`) — Big Muff-style: two cascaded tanh
    stages with 80 Hz/8 kHz inter-stage filters, ScoopTone tilt tone
    (classic mid scoop at noon), Sustain knob.
  - **All**: per-pedal output makeup `C·g^-α` flattens the Drive sweep (was
    ~30 dB of swing); every clipper is followed by a DC blocker; the old
    `Oversampler2x` (single 12 dB/oct biquad — decorative) is replaced by
    `drv::OversamplerIIR`, 8th-order Butterworth up/down cascades — 2x on
    the overdrives, 4x on distortion/fuzz/sustainer.

- **Octave rework** (`octave.h`) — the known-broken tracking is fixed, plus a
  new **Mode** knob (Mono/Poly):
  - Hysteresis is now derived from the envelope of the _filtered_ signal —
    the same signal the Schmitt trigger compares against — so high notes
    attenuated by the tracking filter keep the divider flipping.
  - The tracking lowpass is adaptive: each Schmitt flip is one input cycle,
    so samples-between-flips measures the period directly; the cutoff
    follows 1.4× the detected f0 (70 Hz–1 kHz, ±25% two-in-a-row interval
    validation, ~30 ms log-domain glide). A stall-relax rule reopens the
    filter after upward note jumps; silence resets it to a neutral 250 Hz.
  - Octave-up rectifies the tracked fundamental instead of the raw input —
    the intermod hash is gone.
  - **Poly mode**: dual `rv::PitchShift` granular taps (the shimmer engine;
    40 ms up / 70 ms down windows) — chord-friendly octaves at the cost of a
    little grain warble and ~20–35 ms wet-only latency. Mode switches fade
    the wet out/in (~8 ms) around an engine reset, so no clicks. (If a third
    consumer of `rv::PitchShift` appears, extract it to a `pitch_dsp.h`.)

- **Compressor rework** (`comp.h`, was suggestion 1) — `juce::dsp::Compressor`
  (hard-knee, unlinked channels) replaced by a hand-rolled optical-style comp:
  stereo-linked log-domain detector, 6 dB soft knee, fixed 10 ms attack,
  program-dependent release (a ~400 ms GR average steers release between
  80 ms and 700 ms — brief peaks recover fast, sustained squash releases slow,
  no pumping). Makeup is now automatic (derived from Threshold/Ratio around a
  −10 dBFS nominal peak), which freed the third knob for **Blend** — parallel
  compression, dry keeps the pick attack while the wet path carries sustain.
  Threshold/Ratio ids unchanged (presets + meter markers still work); old
  preset `makeup` values are dropped. GR meter now reports the detector's
  real gain reduction instead of a peak-in/out estimate.

## Suggestions (to tackle individually)

### 2. Smaller polish

- **Tremolo:** harmonic tremolo mode (split ~800 Hz, modulate lows/highs in
  antiphase — brownface sound); stereo-phase (pan trem) knob; the square-edge
  smoothing coefficient (0.02) is sample-rate dependent.
- **Swell:** linear attack ramp instead of one-pole (reads more like a real
  volume-pedal swell); onset re-trigger so legato notes can re-swell.
- **Phaser:** expose stage count / centre frequency instead of fixed 600 Hz.
- **Chorus:** multi-voice (2–3 per side) if the JUCE one ever feels thin.

### 3. New effect candidates

- Cab IR loader (fixed-partition convolution) — matters if any NAM captures
  are amp-only with no cab baked in
- Envelope filter / auto-wah
- Rotary speaker
- Clean boost
- Detune — could reuse `rv::PitchShift` at a near-unity ratio (the octave's
  Poly mode already covers poly octave shifting)
