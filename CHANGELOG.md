# Changelog

All notable changes to the Frequency Shifter plugin will be documented in this file.

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
