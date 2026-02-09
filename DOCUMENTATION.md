# Frequency Shifter Plugin - Complete Documentation

## Overview

The Frequency Shifter is a spectral audio processing plugin that shifts frequencies using Short-Time Fourier Transform (STFT) analysis/synthesis. Unlike pitch shifting, frequency shifting moves all frequencies by a fixed Hz amount, creating inharmonic and metallic timbres.

**Current Version:** v14 NoDrift
**Formats:** AU, VST3, AUv3, Standalone
**Platform:** macOS (Universal Binary - Intel & Apple Silicon)

---

## Table of Contents

1. [Installation](#installation)
2. [User Interface](#user-interface)
3. [Parameters Reference](#parameters-reference)
4. [Technical Architecture](#technical-architecture)
5. [DSP Pipeline](#dsp-pipeline)
6. [Latency & Timing](#latency--timing)
7. [File Structure](#file-structure)
8. [Building from Source](#building-from-source)
9. [Known Limitations](#known-limitations)

---

## Installation

### Automatic (Build)
The plugin automatically installs to system folders when built:
- **AU:** `~/Library/Audio/Plug-Ins/Components/`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/`

### Manual
Copy the built plugin bundles from:
```
plugin/build/FrequencyShifter_artefacts/Release/AU/
plugin/build/FrequencyShifter_artefacts/Release/VST3/
```

### DAW Setup
1. Restart your DAW after installation
2. Rescan plugins if necessary
3. The plugin appears as "Frequency Shifter v14 NoDrift" under "HarmonicTools"

---

## User Interface

### Layout (640 x 430 pixels)

```
+------------------------------------------+
|  FREQUENCY SHIFTER                       |
+------------------------------------------+
|  +--------+   +------------------------+ |
|  |        |   | QUANTIZE    PRESERVE   | |
|  | SHIFT  |   | ROOT NOTE   SCALE      | |
|  | KNOB   |   | SMEAR       VOCODER    | |
|  +--------+   +------------------------+ |
+------------------------------------------+
|  DRY/WET [===================] [Spectrum]|
+------------------------------------------+
|  [Mask] MODE [v] TRANS [===]             |
|  LOW [==========]  HIGH [==========]     |
+------------------------------------------+
```

### Control Sections

1. **Shift Knob (Left Panel)** - Main frequency shift control
2. **Controls Panel (Right)** - Quantization, scale, and quality settings
3. **Mix Panel** - Dry/wet blend and spectrum toggle
4. **Mask Panel** - Spectral masking controls

---

## Parameters Reference

### Main Controls

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Shift (Hz)** | -20,000 to +20,000 Hz | 0 Hz | Amount to shift all frequencies. Positive = up, negative = down. |
| **Quantize** | 0-100% | 0% | Strength of musical scale quantization. 0% = pure frequency shift. |
| **Preserve** | 0-100% | 0% | Spectral envelope preservation. Maintains original timbral character. |
| **Root Note** | C through B | C | Root note for scale quantization. |
| **Scale** | Various | Major | Musical scale for quantization (Major, Minor, Dorian, etc.). |
| **Smear (ms)** | 5-123 ms | 46 ms | FFT window size as latency. Higher = smoother but more smeared. |
| **Vocoder** | On/Off | On | Enhanced phase vocoder for reduced artifacts. |
| **Dry/Wet** | 0-100% | 100% | Mix between original and processed signal. |

### Spectral Mask Controls

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Mask** | On/Off | Off | Enable/disable spectral masking. |
| **Mode** | Low Pass, High Pass, Band Pass | Band Pass | Type of frequency masking. |
| **Low** | 20-20,000 Hz | 200 Hz | Low frequency cutoff (logarithmic). |
| **High** | 20-20,000 Hz | 5,000 Hz | High frequency cutoff (logarithmic). |
| **Transition** | 0.1-4.0 octaves | 1.0 oct | Smoothness of mask edges. |

### Available Scales

- Major
- Natural Minor
- Harmonic Minor
- Melodic Minor
- Dorian
- Phrygian
- Lydian
- Mixolydian
- Locrian
- Pentatonic Major
- Pentatonic Minor
- Blues
- Whole Tone
- Chromatic

---

## Technical Architecture

### Core Technologies

- **Framework:** JUCE 8.0.4
- **Language:** C++20
- **FFT:** Custom Cooley-Tukey implementation
- **Processing:** STFT with 75% overlap (hop = FFT/4)

### Valid FFT Sizes

| FFT Size | Latency @ 44.1kHz | Frequency Resolution |
|----------|-------------------|---------------------|
| 256 | ~6 ms | ~172 Hz |
| 512 | ~12 ms | ~86 Hz |
| 1024 | ~23 ms | ~43 Hz |
| 2048 | ~46 ms | ~22 Hz |
| 4096 | ~93 ms | ~11 Hz |

### Dual-FFT Crossfading

The SMEAR control uses two parallel FFT processors that crossfade between adjacent FFT sizes for smooth, artifact-free transitions:

```
SMEAR 5ms  → FFT 256 only
SMEAR 15ms → FFT 256 + 512 crossfaded
SMEAR 30ms → FFT 512 + 1024 crossfaded
SMEAR 60ms → FFT 1024 + 2048 crossfaded
SMEAR 100ms → FFT 2048 + 4096 crossfaded
SMEAR 123ms → FFT 4096 only
```

---

## DSP Pipeline

### Signal Flow

```
Input
  │
  ├──► Dry Signal Buffer
  │
  ▼
Circular Input Buffer
  │
  ▼
FFT Analysis (windowed)
  │
  ▼
Magnitude/Phase Extraction
  │
  ├──► Phase Vocoder (if enabled)
  │
  ▼
Frequency Shifting
  │
  ├──► Musical Quantization (if enabled)
  │
  ├──► Spectral Envelope Preservation (if enabled)
  │
  ├──► Spectral Mask (if enabled)
  │
  ▼
IFFT Synthesis
  │
  ▼
Overlap-Add Output Buffer
  │
  ▼
Latency Compensation Delay
  │
  ▼
Dry/Wet Mix
  │
  ▼
Output
```

### Key DSP Classes

| Class | File | Purpose |
|-------|------|---------|
| `STFT` | dsp/STFT.h | FFT analysis/synthesis with windowing |
| `PhaseVocoder` | dsp/PhaseVocoder.h | Phase coherence for reduced artifacts |
| `FrequencyShifter` | dsp/FrequencyShifter.h | Spectral bin shifting |
| `MusicalQuantizer` | dsp/MusicalQuantizer.h | Scale-aware frequency snapping |
| `SpectralMask` | dsp/SpectralMask.h | Frequency-selective processing |
| `DriftModulator` | dsp/DriftModulator.h | Organic pitch variation (currently disabled in UI) |

---

## Latency & Timing

### Fixed Latency Design

The plugin reports a **fixed latency of 4096 samples** (~93ms @ 44.1kHz) to the host, regardless of the SMEAR setting. This ensures:

1. **Consistent DAW compensation** - The host always compensates by the same amount
2. **Smooth SMEAR automation** - Changing FFT size doesn't cause timing jumps
3. **Reliable sync** - Plugin stays in time with the project at all settings

### Internal Compensation

When using smaller FFT sizes, internal delay buffers add compensation:

| FFT Size | Internal Delay Added |
|----------|---------------------|
| 256 | 3,840 samples |
| 512 | 3,584 samples |
| 1024 | 3,072 samples |
| 2048 | 2,048 samples |
| 4096 | 0 samples |

### Bypass Behavior

The plugin bypasses processing (true passthrough) when:
- Shift Hz ≈ 0 AND Quantize ≈ 0

Note: The fixed latency still applies even in bypass to maintain timing.

---

## File Structure

```
frequency-shifter/
├── plugin/
│   ├── CMakeLists.txt           # Build configuration
│   ├── src/
│   │   ├── PluginProcessor.h    # Main audio processor header
│   │   ├── PluginProcessor.cpp  # Main audio processor implementation
│   │   ├── PluginEditor.h       # GUI header
│   │   ├── PluginEditor.cpp     # GUI implementation
│   │   └── dsp/
│   │       ├── STFT.h/cpp           # Short-Time Fourier Transform
│   │       ├── PhaseVocoder.h/cpp   # Phase vocoder processing
│   │       ├── FrequencyShifter.h/cpp # Spectral shifting
│   │       ├── MusicalQuantizer.h/cpp # Scale quantization
│   │       ├── SpectralMask.h/cpp   # Frequency masking
│   │       ├── DriftModulator.h     # Pitch drift (header only)
│   │       ├── Scales.h             # Musical scale definitions
│   │       └── FeedbackDelay.h      # Feedback delay (currently unused)
│   └── build/                   # Build output directory
├── DOCUMENTATION.md             # This file
└── README.md                    # Project readme
```

---

## Building from Source

### Requirements

- CMake 3.22+
- Xcode Command Line Tools (macOS)
- C++20 compatible compiler

### Build Commands

```bash
cd plugin
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Build Output

Plugins are automatically installed to:
- `~/Library/Audio/Plug-Ins/Components/` (AU)
- `~/Library/Audio/Plug-Ins/VST3/` (VST3)

### Changing Version Name

Edit `plugin/CMakeLists.txt`:
```cmake
PRODUCT_NAME "Frequency Shifter vXX YourName"
```

---

## Known Limitations

### Current Limitations

1. **Fixed 93ms latency** - Required for timing stability, may be too high for live performance
2. **No MIDI input** - Scale root/type must be set manually
3. **Drift controls disabled** - UI removed pending redesign
4. **Feedback delay disabled** - Removed pending timing fixes
5. **macOS only** - Windows/Linux builds not tested

### Audio Artifacts

- **Metallic sound** - Inherent to frequency shifting (not pitch shifting)
- **Transient smearing** - Higher SMEAR values blur transients
- **Phase issues** - Some material may have comb filtering; try adjusting SMEAR

### Performance Notes

- CPU usage scales with FFT size
- Dual-FFT crossfading doubles CPU during transitions
- Spectrum analyzer adds minor CPU overhead when visible

---

## Parameter IDs (for Automation)

| Parameter ID | Description |
|--------------|-------------|
| `shiftHz` | Frequency shift amount |
| `quantizeStrength` | Quantization strength |
| `rootNote` | Root note (0-11) |
| `scaleType` | Scale type index |
| `dryWet` | Dry/wet mix |
| `phaseVocoder` | Phase vocoder toggle |
| `smear` | FFT size/latency |
| `envelopePreserve` | Envelope preservation |
| `maskEnabled` | Mask toggle |
| `maskMode` | Mask mode (0-2) |
| `maskLowFreq` | Mask low frequency |
| `maskHighFreq` | Mask high frequency |
| `maskTransition` | Mask transition width |

---

## Version History

| Version | Changes |
|---------|---------|
| v14 NoDrift | Removed drift UI for redesign |
| v13 FixedLatency | Fixed timing with internal delay compensation |
| v12 TimingFix | Debugging latency issues |
| v10-v11 | Removed feedback delay module |
| Earlier | Initial development with feedback, drift, etc. |

---

## Credits

- **Framework:** JUCE by ROLI/Pace
- **FFT Algorithm:** Cooley-Tukey
- **Window Function:** Hann window

---

## Support

For issues and feature requests, refer to the project repository or contact the developer.
