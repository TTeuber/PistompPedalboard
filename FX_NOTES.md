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

### 3. Drive family — real voicing, not just a curve

All three pedals share `drive_base.h` and differ only in the waveshape; real
pedals differ mostly in filter topology:

- **Overdrive (TS-style):** highpass ~700 Hz *into* the clipper (mid-hump,
  tight low end), post tone stack, maybe a parallel clean blend.
- **Distortion (RAT/DS-1):** harder clip, tighter input HP, steeper post LP.
  **Bug:** `tanh(v + 0.15v²)` is asymmetric and generates DC — nothing after
  the shaper blocks it. Any asymmetric shaper needs a post DC blocker.
- **Fuzz:** envelope-dependent bias shift into an asymmetric clipper for the
  gated sputter / cleans-up-with-guitar-volume behavior; `tanh(3v)` is just
  louder overdrive.
- **All:** output loudness compensation across the Drive sweep (currently max
  drive is ~30 dB louder than min).
- **Oversampler:** `Oversampler2x`'s anti-alias is a single 12 dB/oct biquad —
  nearly decorative. Use a half-band FIR or 2–3 cascaded biquads; consider 4x
  for the fuzz.

### 4. Delay — analog character

- Subtle modulation on the repeats (slow LFO wow on the read tap).
- Gentle saturation in the feedback loop (tape/BBD vibe).
- Highpass in the feedback loop so repeats shed lows instead of piling up.

### 5. Compressor — hand-roll it

`juce::dsp::Compressor` is hard-knee feedforward with *unlinked* channels.
A custom one with soft knee, stereo-linked detector, and program-dependent
(optical-style) release turns it from utility into always-on.

### 6. Smaller polish

- **Tremolo:** harmonic tremolo mode (split ~800 Hz, modulate lows/highs in
  antiphase — brownface sound); stereo-phase (pan trem) knob; the square-edge
  smoothing coefficient (0.02) is sample-rate dependent.
- **Swell:** linear attack ramp instead of one-pole (reads more like a real
  volume-pedal swell); onset re-trigger so legato notes can re-swell.
- **Phaser:** expose stage count / centre frequency instead of fixed 600 Hz.
- **Chorus:** multi-voice (2–3 per side) if the JUCE one ever feels thin.

### 7. New effect candidates

- Shimmer reverb (or a mode of the new reverb) — #1 for the genre
- Cab IR loader (fixed-partition convolution) — matters if any NAM captures
  are amp-only with no cab baked in
- Envelope filter / auto-wah
- Rotary speaker
- Clean boost
- Detune / poly pitch shift (doubles as the poly octave above)
