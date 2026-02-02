# Changelog

All notable changes to the Frequency Shifter plugin will be documented in this file.

## [v55-TempoSync] - 2026-01-30

### Added
- **Tempo Sync**: Delay time can now sync to host tempo
  - SYNC toggle: ON = tempo divisions, OFF = milliseconds
  - 16 divisions: 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2T, 1/2, 1/2D, 1/1, 2/1, 4/1
  - Reads host BPM via `getPlayHead()->getPosition()->getBpm()`

- **80Hz Highpass Filter**: Prevents low frequency buildup in feedback loop
  - Two-pole Butterworth biquad (Q=0.707) at fixed 80Hz cutoff
  - Applied BEFORE the lowpass damping filter

### Signal Chain (Feedback Path)
```
Delayed signal → HPF (80Hz) → LPF (DAMP control) → Soft clip → Feedback to input
```

### Technical Details
- Tempo sync multipliers relative to quarter note:
  - 1/32=0.125, 1/16T=0.167, 1/16=0.25, 1/16D=0.375
  - 1/8T=0.333, 1/8=0.5, 1/8D=0.75
  - 1/4T=0.667, 1/4=1.0, 1/4D=1.5
  - 1/2T=1.333, 1/2=2.0, 1/2D=3.0
  - 1/1=4.0, 2/1=8.0, 4/1=16.0
- HPF coefficients: Butterworth biquad with Q=0.707, fc=80Hz
- Fallback BPM: 120 when host tempo unavailable

### Expected Behaviour
- With SYNC ON and host at 120 BPM: 1/4 = 500ms, 1/8 = 250ms, etc.
- Bass frequencies no longer accumulate and cause overload
- Feedback remains controlled even at high FDBK settings with low SHIFT

## [v54-CascadeFeedback] - 2026-01-30

### Added
- **Cascading Pitch Shift Feedback**: Delay feedback now routes through the frequency shifter
- Each echo gets shifted again: +100Hz → +200Hz → +300Hz → etc.
- Time-domain feedback buffer added (up to 2 seconds)
- One-pole lowpass damping filter on feedback path

### Signal Flow (Changed)
```
Old: Input → FFT → Shift → Spectral Delay (internal feedback) → IFFT → Output
New: Input + Feedback → FFT → Shift → Spectral Delay → IFFT → Output
            ↑________________________________________________↓
```

### Technical Details
- Feedback buffer: `MAX_FEEDBACK_DELAY_SAMPLES = 96000` (~2s at 48kHz)
- Feedback read position based on TIME parameter (ms → samples)
- Damping filter: One-pole lowpass, cutoff from 12kHz (0%) to 1kHz (100%)
- Soft clipping: `tanh()` when feedback exceeds 0.95 threshold
- Spectral delay internal feedback disabled (set to 0)
- Feedback only processed once per sample (on proc 0) to avoid double-processing

### Expected Behaviour
- SHIFT=+100Hz, FDBK=50%: Echoes rise in pitch (+100, +200, +300Hz...)
- SHIFT=-50Hz, FDBK=50%: Echoes fall in pitch (-50, -100, -150Hz...)
- DAMP control affects feedback brightness (same as before)
- SLOPE still affects frequency-dependent delay within spectral domain

## [v53] - 2026-01-30

### Reset
- Clean slate from v50-PreserveExtreme
- Reverted Phase 3 feedback delay experiments (v51, v52)
- All Phase 2B features intact (PRESERVE, TRANSIENTS, SENSITIVITY)

### Current Features
- Frequency shifting with phase vocoder (Enhanced Mode)
- Musical scale quantization (22 scales)
- SMEAR control (5-123ms latency/quality tradeoff)
- Drift modulation (LFO, Perlin, Stochastic)
- Spectral mask (Low/High/Band pass)
- Spectral delay (frequency-dependent timing)
- PRESERVE: Spectral envelope preservation
- TRANSIENTS: Transient detection bypass
- SENSITIVITY: Detection threshold control

## [v50-PreserveExtreme] - 2026-01-30

### Enhanced
- **PRESERVE now much more extreme at high settings**:
  - Non-linear scaling: `effectiveStrength = pow(preserveAmount, 0.7)` gives more effect at top
  - Dynamic clamp limits based on PRESERVE setting:
    - At 50%: ±18dB correction range
    - At 100%: ±48dB correction range (nearly unclamped)
  - High-resolution mode (96 bands, ~1/10 octave) activates at PRESERVE > 75%
    - Double the bands for tighter spectral matching
    - Extra aggressive clamp: ±36dB to ±60dB at 75%-100%

- **NEW: Amplitude envelope tracking**:
  - Tracks input amplitude envelope with fast attack (~1ms) and moderate release (~50ms)
  - Tracks output amplitude envelope the same way
  - Applies gain correction to match output dynamics to input dynamics
  - Tied to PRESERVE control: at 100%, both spectral shape AND amplitude dynamics match input
  - Correction clamped to ±12dB to avoid pumping

### Technical Details
- Non-linear response: `pow(x, 0.7)` curve makes 50% feel like ~62%, 75% feel like ~81%
- Standard resolution (0-75%): 48 bands at ~1/5 octave spacing
- High resolution (75-100%): 96 bands at ~1/10 octave spacing
- Amplitude follower: `env = abs(sample) + coeff * (env - abs(sample))`
  - Attack coeff: `exp(-1/(sr * 0.001))`
  - Release coeff: `exp(-1/(sr * 0.05))`
- Gain correction: `wetSample *= inputEnv / (outputEnv + epsilon)`

### Expected Behaviour
- At PRESERVE=100%: Both tonal character AND dynamics should closely match input
- Bright sounds stay bright, dark sounds stay dark (spectral preservation)
- Punchy attacks stay punchy, soft passages stay soft (amplitude preservation)
- Two-stage behaviour: subtle at low settings, aggressive at high settings

## [v49-PreserveFix] - 2026-01-30

### Fixed
- **PRESERVE now much more effective** - completely reworked envelope preservation:
  - Increased resolution from 1/3 octave (24 bands) to 1/6 octave (48 bands)
  - Changed from peak to RMS energy per band for more stable envelope estimation
  - Raised correction clamp from ±12dB to ±36dB to handle aggressive quantization
  - **Critical fix**: Envelope now captured from INPUT signal BEFORE shift/quantization
  - Previously was capturing post-shift envelope, which defeated the purpose

### Technical Details
- 48 bands at ~1/6 octave spacing (2^(1/6) ≈ 1.122 intervals)
- RMS energy per band: `sqrt(sum(mag²) / count)` instead of peak
- Correction ratio range: 0.016 to 64 (±36dB)
- New API: `getSpectralEnvelope()` to capture envelope externally
- Processor now captures envelope before `shift()` and passes to `quantizeSpectrum()`

### Expected Behaviour
- Bright cymbal vs dark pad at PRESERVE=100%, QUANTIZE=100%: should retain distinct brightness
- PRESERVE=100%: Quantized output matches original spectral balance closely
- High quantization no longer "smears" all sounds to similar timbre

## [v48-PreserveTransients] - 2026-01-30

### Added
- **Phase 2B.1 - Spectral Envelope Preservation (PRESERVE control)**:
  - New PRESERVE slider (0-100%) maintains timbral character after quantization
  - Captures spectral envelope at ~1/3 octave resolution using peak magnitudes
  - Reimpose original envelope shape on quantized output
  - Preserves formants and overall brightness after pitch quantization

- **Phase 2B.2 - Transient Detection Bypass (TRANSIENT & SENS controls)**:
  - New TRANSIENT slider (0-100%) controls how much transients bypass quantization
  - New SENS slider (0-100%) adjusts detection sensitivity (0%=3x ratio, 100%=1.2x ratio)
  - Compares spectral energy between frames to detect attacks
  - Ramps quantization back up over 4 frames for smooth transitions
  - At TRANSIENTS=100%, attacks pass through completely unquantized for punch

### Technical Details
- Envelope uses 24 bands covering 20Hz-20kHz at ~1/3 octave (bark-like) resolution
- Peak magnitude per band (not average) for better transient response
- Envelope correction clamped to ±12dB to avoid extreme boosts
- Transient detection uses energy ratio threshold: `3.0 - sensitivity * 1.8`
- Transient ramp-down over TRANSIENT_RAMP_FRAMES=4 frames (~93ms at default smear)

### Expected Behaviour
- PRESERVE=100%: Quantized output retains original brightness/darkness balance
- TRANSIENTS=100% + SENS=50%: Drum attacks pass through unaffected, sustains quantized
- Works well for preserving punch on percussive material while still quantizing held notes

## [v47-PhaseBlend] - 2026-01-30

### Fixed
- **Enhanced Mode now works with partial quantization**: Previously, Enhanced Mode (phase vocoder) had no effect beyond ~2% quantization because phase accumulators were completely overriding the phase vocoder output for remapped bins

### Technical Details
- Phase blending now respects quantization strength parameter:
  - At strength=0%: 100% input phase (phase vocoder output if Enhanced Mode on)
  - At strength=100%: 100% quantized phase (MIDI note phase accumulator)
  - At strength=30%: 70% phase vocoder + 30% phase accumulator blend
- Uses circular interpolation for phase blending to handle wraparound at ±π boundary
- Non-remapped bins always preserve input phase (phase vocoder coherence maintained)

### Expected Behaviour
- At 30% quantization with Enhanced Mode ON, sustained sounds should have audibly different character than Enhanced Mode OFF
- Phase vocoder's formant preservation and pitch stability should be more apparent at lower quantization amounts

## [v46-WeightedSmooth] - 2026-01-30

### Fixed
- **Strategy A - Weighted Energy Distribution**: Instead of mapping 100% energy to nearest scale bin, now distributes energy between the two closest scale-legal bins based on inverse distance weighting
- **Strategy C - Magnitude Smoothing**: Applied 3-tap moving average [0.25, 0.5, 0.25] after quantization to reduce sharp spectral peaks/pits

### Technical Details
- `findTwoNearestScaleFrequencies()`: Finds lower and upper scale frequencies, calculates inverse-distance weights using log-frequency (cents) for perceptually uniform distribution
- Energy distribution: If input is closer to lower note, lower bin gets more energy; closer to upper note, upper bin gets more energy
- Smoothing applied after accumulation normalization but before energy normalization to preserve total energy
- Boundary conditions: First and last bins unchanged during smoothing

### Expected Improvements
- Smoother spectral distribution reduces "picket fence" effect from hard quantization
- Reduced resonance buildup at isolated frequency peaks
- More natural timbral transitions between scale degrees
- Less harsh artifacts during transients

## [v45-DecayFix] - 2026-01-30

### Fixed
- **Tinnitus/Ringing Elimination**: Phase accumulators now only run when MIDI notes have significant energy
- **Natural Decay**: Notes decay naturally when input stops instead of sustaining indefinitely
- **DC Offset Prevention**: DC bin (bin 0) is now explicitly zeroed to prevent low-frequency rumble

### Technical Details
- Added `silentFrameCount` per MIDI note - tracks consecutive frames below threshold
- `MAGNITUDE_THRESHOLD = 0.001f` (~-60dB) gates phase accumulator updates
- `SILENCE_FRAMES_TO_RESET = 8` frames (~185ms) before phase accumulator resets
- Phase accumulators only increment when note magnitude > threshold
- Below-threshold notes use strongest contributor's phase (natural decay behavior)
- DC bin zeroed after quantization to prevent buildup

### Expected Behavior
- Sounds decay naturally when input stops
- No perpetual tones at quantized frequencies
- No tinnitus-like ringing after transients
- Test: SHIFT=500Hz, SMEAR=100ms, QUANTIZE=100% - should decay cleanly

## [v44-PhasePreserve] - 2026-01-30

### Fixed
- **Phase Vocoder Compatibility**: Enhanced Mode (phase vocoder) now works correctly when combined with quantization
- Phase continuity now ONLY applies to bins that are actually remapped by quantization
- Bins that stay at their original frequency preserve input phase (including phase vocoder's coherent phase)

### Technical Details
- Added `binWasRemapped` tracking: distinguishes between remapped bins (quantization moved them) vs unchanged bins
- Signal flow now correctly handles all combinations:
  - Enhanced OFF + Quantize OFF: raw phases pass through ✓
  - Enhanced ON + Quantize OFF: phase vocoder handles shift coherence ✓
  - Enhanced OFF + Quantize ON: phase continuity handles quantized bins ✓
  - Enhanced ON + Quantize ON: phase vocoder for shift, then phase continuity ONLY for remapped bins ✓

### Investigation Findings
- The v43 implementation unconditionally replaced ALL phases with MIDI-note accumulators
- This discarded phase vocoder's carefully synthesized coherent phases
- Now: only bins that get remapped to a different target use phase accumulators
- Bins mapping to themselves (source == target) preserve the input phase

## [v43-QuantizeFix] - 2026-01-30

### Fixed
- **Phase 2A.1 - Accumulation Normalization**: When multiple source bins quantize to the same target bin, energy is now normalized by sqrt(N) instead of simple summation, preventing energy buildup
- **Phase 2A.2 - Total Energy Normalization**: Total spectral energy is now preserved through quantization, ensuring consistent volume regardless of scale/quantization settings
- **Phase 2A.3 - Phase Continuity**: Replaced "loudest bin wins" phase selection with persistent MIDI note-based phase accumulators, providing smooth synthesis and natural decay

### Technical Details
- Phase accumulators indexed by MIDI note (0-127) for consistent phase across different FFT sizes
- Energy normalization: scaleFactor = sqrt(energyBefore / energyAfter) applied after quantization
- Phase increment per note: phaseIncrement = 2π × noteFreq × hopSize / sampleRate
- All 128 MIDI note phases updated every frame for continuous phase evolution

### Expected Improvements
- Natural decay without artificial sustain/release artifacts
- Stable volume without runaway energy buildup
- Different sources maintain distinct character (no convergence to same timbre)

## [v42-DryTiming] - 2026-01-30

### Fixed
- Dry signal timing: dry signal now properly delayed by full reported latency (4096 samples)
- Dry and wet signals are now time-aligned when mixed

### Technical Details
- Added dedicated dry delay buffer per channel
- Dry signal path: Input → Dry delay buffer (4096 samples) → Mix
- Wet signal path: Input → FFT processing → Internal compensation → Mix
- Both paths now have equal latency, ensuring proper phase alignment

## [v41-SmearControl] - 2026-01-30

### Added
- New SMEAR parameter (5-123ms) replaces the Quality Mode dropdown
- Continuous control over latency/quality tradeoff
- Dual FFT processor architecture with equal-power crossfading between adjacent FFT sizes
- Valid FFT sizes: 256 (~6ms), 512 (~12ms), 1024 (~23ms), 2048 (~46ms), 4096 (~93ms)

### Changed
- Fixed latency reporting: always reports 4096 samples (~93ms at 44.1kHz) to host
- Internal delay compensation buffers maintain DAW timing stability at all SMEAR settings
- Single processor mode activates when exactly on an FFT size boundary (no crossfade overhead)

### Technical Details
- Crossfade uses equal-power (cos/sin) gains to prevent level dips during transitions
- Delay compensation automatically adjusts based on effective FFT size blend

## [v40-Original] - 2026-01-30

### Initial Release
- Harmonic-preserving frequency shifting with phase vocoder
- Musical scale quantization (22 scales)
- Quality mode dropdown (Low Latency/Balanced/Quality)
- Drift modulation (LFO, Perlin, Stochastic)
- Spectral mask (Low/High/Band pass)
- Spectral delay with frequency-dependent timing
- Real-time spectrum analyzer
