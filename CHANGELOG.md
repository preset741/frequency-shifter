# Changelog

All notable changes to the Frequency Shifter plugin will be documented in this file.

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
