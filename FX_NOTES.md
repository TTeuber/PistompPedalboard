# FX DSP Notes

Working notes for replacing the placeholder DSP with more professional,
custom, better-sounding effects. Tackled incrementally.

## Done

- **Parameter smoothing** (all effects): audible-path gains/mixes/depths are
  one-pole smoothed per sample (`Smoother` in `dsp_util.h`, ~20 ms) so swept
  knobs glide instead of stepping per block. EQ band gains smooth at block
  rate (~50 ms) since they act through biquad coefficients. (Nothing relies
  on JUCE's internal smoothing anymore -- the last JUCE-wrapped effects,
  Chorus and Phaser, are hand-rolled as of the modulation collection.)
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
    _Update: it appeared (Detune) -- the shifter now lives in
    `effects/pitch_dsp.h` as `pit::PitchShift`; consumers are shimmer/octave/
    detune._

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

- **Modulation collection** (absorbs suggestions "Phaser: expose stage count/
  centre" and "Chorus: multi-voice"): the JUCE Chorus/Phaser wrappers are
  gone; chorus/phaser/flanger rebuilt hand-rolled plus four new pedals, all
  sharing `effects/mod_dsp.h` (`md::Allpass1` first-order allpass, `ModDelay`
  modulated tap, `rateHz` sync-or-knob resolver, `tri`/`skewedCos` shapes;
  the phase-accumulator `Lfo` itself promoted from `delay_dsp.h` to
  `dsp_util.h` on its second collection of consumers):
  - **Chorus** (rework, keeps ids; Feedback dropped -- no natural feedback
    point in a voice bank) — up to 3 voices/side at staggered 5.5/8.5/12 ms
    bases with phase- and rate-spread LFOs, equal-power Voices switching
    through smoothed gains, ~8 kHz BBD-dark wet, mid/side Width.
  - **Phaser** (rework, keeps ids) — 8 always-running `Allpass1` stages/side,
    wet tap crossfades between stage 4/6/8 (click-free Stages switch),
    exponential ±1.5-octave sweep around a Centre knob (default 600 Hz = the
    old fixed voice), feedback ≤0.9 into stage 1 (allpass chain is unity-
    magnitude, unconditionally stable), Sync/Div beat sync.
  - **Flanger** (rework, keeps ids; feedback widened bipolar −95..95) —
    exponential sweep `manual · ratio^lfo` (equal time per comb octave),
    Manual base-delay knob, ~7 kHz damping in the fed-back branch, soft-
    limited loop, Sync/Div; **Thru-Zero** puts the dry on its own tap at the
    sweep's log-midpoint with a smoothed wet polarity flip -- one tape-flange
    null per cycle, toggle glides instead of clicking.
  - **Uni-Vibe** (new, `uni_vibe.h`, kind `vibe`) — 4 staggered allpass
    corners (110/280/700/1800 Hz, the mismatched-caps signature) swept by a
    shared lamp/LDR LFO (`skewedCos`: fast bloom, slow tail); Voice picks
    Chorus (50/50) or Vibrato (wet-only) through smoothed gains.
  - **Dimension** (new, `dimension.h`) — SDD-320-style widener: antiphase-
    modulated sub-ms taps cross-mixed L/R (`wet_L − 0.7·wet_R`), four preset
    Mode buttons glided through smoothers, deliberately subtler than Chorus.
  - **Detune** (new, `detune.h`) — `pit::PitchShift` at ±cents with a 25 ms
    grain window (warble-free near unity, ~12 ms avg wet latency); Spread
    on = L down/R up, off = both up (mono-safe).
  - **Rotary** (new, `rotary.h`) — Leslie: mono-sum → gentle tanh drive →
    800 Hz complementary crossover → horn (0.8/6.8 Hz) + drum (0.7/5.7 Hz),
    each one sin/cos pair driving Doppler tap + AM + mic panning; Speed
    targets chase through rotor-inertia smoothers (horn ~0.8 s, drum ~3.5 s)
    so the horn audibly arrives first. No cab IR (scope-bound).
  - Enum-id note: the web `ENUMS` map and device `kEnums` are global by param
    id, so the new enums use fresh ids (`stages`/`voices`/`voice`/`dmode`/
    `speed`); device table widened to 4 labels for Dimension.

## Suggestions (to tackle individually)

### 2. Smaller polish

- **Tremolo:** harmonic tremolo mode (split ~800 Hz, modulate lows/highs in
  antiphase — brownface sound); stereo-phase (pan trem) knob; the square-edge
  smoothing coefficient (0.02) is sample-rate dependent.
- **Swell:** linear attack ramp instead of one-pole (reads more like a real
  volume-pedal swell); onset re-trigger so legato notes can re-swell.

### 3. New effect candidates

- Cab IR loader (fixed-partition convolution) — matters if any NAM captures
  are amp-only with no cab baked in
- Envelope filter / auto-wah
- Clean boost
