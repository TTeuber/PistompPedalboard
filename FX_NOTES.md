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

- **Drive collection** (was suggestion 3): the three placeholder drives became
  a five-pedal collection with real per-circuit topology, sharing
  `effects/drive_dsp.h` (`OversamplerIIR`, `DcBlock`, `EnvFollower`,
  `HighPass1`, `ScoopTone`, clip primitives); `drive_base.h` deleted:
    - **Overdrive** — TS-style: 720 Hz HP *into* a feedback-clipper identity
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

## Suggestions (to tackle individually)

### 1. Octave — fix the tracking (known broken)

- **Bug:** the Schmitt trigger's hysteresis is scaled by the envelope of the
  *unfiltered* input, but compared against the heavily lowpassed tracking
  signal (`octave.h`). Above a few hundred Hz the 400 Hz tracking filter
  attenuates the fundamental so much it never crosses `0.15 * env` — the
  divider stops flipping and the sub voice drops out on high notes. Derive
  the hysteresis from the envelope of the *filtered* signal.
- **Bug:** the fixed 400 Hz tracking cutoff fails both directions — low-E
  harmonics (164/246 Hz) pass through and cause octave jumping; high notes
  get killed. Make the cutoff adaptive (track the detected period, or run a
  cheap monophonic tracker à la YIN — the tuner already has one).
- Octave-up rectifies the *raw* signal → intermodulation hash on real guitar.
  Rectify the tracked/filtered signal instead.
- Alternative direction: a granular dual-tap pitch shifter (Whammy/POG style)
  gives chord-friendly octave up/down at the cost of a little warble; could
  live alongside the vintage mono voice.

### 2. Reverb — replace Freeverb (biggest sonic payoff)

- `juce::Reverb` is Freeverb: metallic at the large sizes ambient worship
  uses, and there is no pre-delay at all.
- Hand-roll an FDN reverb: 8 modulated delay lines, Householder feedback,
  input diffusion allpasses, per-band damping, pre-delay param.
- Then add **shimmer** (pitch-shifted feedback path) — the signature worship
  pad sound.

### 3. Delay — analog character

- Subtle modulation on the repeats (slow LFO wow on the read tap).
- Gentle saturation in the feedback loop (tape/BBD vibe).
- Highpass in the feedback loop so repeats shed lows instead of piling up.

### 4. Compressor — hand-roll it

`juce::dsp::Compressor` is hard-knee feedforward with *unlinked* channels.
A custom one with soft knee, stereo-linked detector, and program-dependent
(optical-style) release turns it from utility into always-on.

### 5. Smaller polish

- **Tremolo:** harmonic tremolo mode (split ~800 Hz, modulate lows/highs in
  antiphase — brownface sound); stereo-phase (pan trem) knob; the square-edge
  smoothing coefficient (0.02) is sample-rate dependent.
- **Swell:** linear attack ramp instead of one-pole (reads more like a real
  volume-pedal swell); onset re-trigger so legato notes can re-swell.
- **Phaser:** expose stage count / centre frequency instead of fixed 600 Hz.
- **Chorus:** multi-voice (2–3 per side) if the JUCE one ever feels thin.

### 6. New effect candidates

- Shimmer reverb (or a mode of the new reverb) — #1 for the genre
- Cab IR loader (fixed-partition convolution) — matters if any NAM captures
  are amp-only with no cab baked in
- Envelope filter / auto-wah
- Rotary speaker
- Clean boost
- Detune / poly pitch shift (doubles as the poly octave above)
