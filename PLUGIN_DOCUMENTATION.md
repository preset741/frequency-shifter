# Frequency Shifter Plugin - Technical Documentation

## 1. Overview

The Frequency Shifter is a professional audio plugin that performs linear frequency shifting with optional musical scale quantization. Unlike pitch shifting, frequency shifting adds/subtracts a fixed Hz amount to all frequencies, creating inharmonic relationships that range from subtle phasing effects to dramatic robotic transformations.

### Core Purpose
- **Creative frequency manipulation** for sound design, electronic music, and experimental audio
- **Musical scale quantization** to constrain shifted frequencies to musical notes
- **Dual processing modes**: Spectral (FFT-based) for harmonic preservation, Classic (Hilbert) for zero-latency processing

### Target Users
- Sound designers seeking unique timbral transformations
- Electronic music producers wanting phaser/chorus-like effects or robotic vocals
- Experimental musicians exploring inharmonic textures
- Audio engineers needing precise frequency manipulation tools

---

## 2. Signal Flow Diagrams

### Spectral (FFT) Mode

```
INPUT
   │
   ├──────────────────────────────────────────────────────┐
   │                                                      │
   ▼                                                      │
[Feedback Buffer Read] ◄── (with FFT latency compensation) │
   │                                                      │
   ▼                                                      │
[+ Mix Feedback]                                          │
   │                                                      │
   ▼                                                      │
[Input Buffer] ──► [Window] ──► [FFT] ──► [Magnitude/Phase]
                                              │
                                              ▼
                                    [Frequency Shifter]
                                    (bin reassignment)
                                              │
                                              ▼
                                    [Phase Vocoder]
                                    (phase coherence)
                                              │
                                              ▼
                                    [Musical Quantizer]
                                    (optional scale snap)
                                              │
                                              ▼
                                    [Spectral Mask]
                                    (frequency-selective)
                                              │
                                              ▼
                                    [Spectral Delay]
                                    (SMEAR diffusion)
                                              │
                                              ▼
                              [IFFT] ──► [Window] ──► [Overlap-Add]
                                                          │
                                                          ▼
                                              [Delay Compensation Buffer]
                                              (maintains fixed latency)
                                                          │
                                                          ▼
                                                    [WET SIGNAL]
                                                          │
   │                                                      │
   ▼                                                      ▼
[Dry Delay Buffer] ─────────────────────────────► [DRY SIGNAL]
(MAX_FFT_SIZE samples)                                    │
                                                          │
                              ┌────────────────────────────┤
                              │                            │
                              ▼                            ▼
                        [WARM Filter]              [Preserve Envelope]
                        (4.5kHz LPF)               (amplitude matching)
                              │                            │
                              └──────────┬─────────────────┘
                                         │
                                         ▼
                                   [Dry/Wet Mix]
                                         │
                                         ▼
                                      OUTPUT
                                         │
                                         │
              ┌──────────────────────────┘
              │
              ▼
[HPF 80Hz] ──► [Damping LPF] ──► [Feedback Buffer Write]
```

### Classic (Hilbert) Mode

```
INPUT
   │
   ├─────────────────────────────────────────┐
   │                                         │
   ▼                                         │
[Feedback Buffer Read] ◄── (NO latency comp) │
   │                                         │
   ▼                                         │
[+ Mix Feedback]                             │
   │                                         │
   ▼                                         │
[Hilbert Transform]                          │
   │                                         │
   ├──► [Allpass Chain I] ──► I signal       │
   │                                         │
   └──► [Allpass Chain Q] ──► Q signal       │
              │                              │
              ▼                              │
    [SSB Modulation]                         │
    I*cos(ωt) ∓ Q*sin(ωt)                    │
              │                              │
              ▼                              │
        [WET SIGNAL]                         │
              │                              │
              │                   [DRY SIGNAL]
              │                         │
              ▼                         ▼
        [WARM Filter] ────────► [Dry/Wet Mix]
        (4.5kHz LPF)                    │
                                        ▼
                                     OUTPUT
                                        │
                                        │
              ┌─────────────────────────┘
              │
              ▼
[HPF 80Hz] ──► [Damping LPF] ──► [Feedback Buffer Write]
```

### Key Differences

| Aspect | Spectral Mode | Classic Mode |
|--------|---------------|--------------|
| Latency | ~93ms (4096 samples @ 44.1kHz) | ~0.3ms (12 samples) |
| Harmonic Preservation | Yes (with quantization) | No (always inharmonic) |
| CPU Usage | Higher | Lower |
| Sound Character | Smooth, musical | Raw, metallic |
| Best For | Melodic content, vocals | Subtle modulation, FX |

---

## 3. Processing Modes

### Spectral Mode (FFT-based)

**How it works:**
1. Audio is windowed and transformed to frequency domain via FFT
2. Magnitude/phase spectra are manipulated (shift, quantize, mask)
3. Phase vocoder maintains coherence to reduce artifacts
4. IFFT reconstructs time-domain signal with overlap-add

**Characteristics:**
- Can preserve harmonic relationships when quantizing to musical scales
- Smooth, musical-sounding shifts
- High latency due to FFT windowing (reported to DAW for compensation)
- SMEAR control varies FFT size (5-123ms window)

**When to use:**
- Vocal processing where musicality matters
- Harmonic content that should stay "in tune"
- When latency is acceptable (mixing, not live performance)

### Classic Mode (Hilbert Transform)

**How it works:**
1. Input passes through two 6th-order allpass filter chains
2. Chains are designed to create 90° phase difference (quadrature signals I and Q)
3. Single-sideband modulation: `output = I*cos(ωt) ± Q*sin(ωt)`
4. Sign determines shift direction (+ for down, - for up)

**Characteristics:**
- Near-zero latency (~0.3ms from filter group delay)
- Always creates inharmonic content (fundamental frequency relationship lost)
- Negative shifts cause low frequencies to "fold back" creating metallic artifacts
- Allpass coefficients optimized for 44.1kHz (may have issues at other sample rates)

**When to use:**
- Live performance requiring minimal latency
- Subtle ±1-20Hz shifts for phaser/chorus effects
- Intentionally metallic/robotic sound design
- When CPU is limited

---

## 4. Parameters

### Main Controls

| Parameter | Range | Description |
|-----------|-------|-------------|
| **SHIFT** | -5000 to +5000 Hz | Frequency shift amount. Positive = up, negative = down |
| **MODE** | Classic / Spectral | Processing algorithm selection |
| **DRY/WET** | 0-100% | Mix between original and processed signal |
| **WARM** | On/Off | 4.5kHz lowpass on wet signal for vintage character |

### Scale Quantization (Spectral Mode Only)

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Quantize** | 0-100% | Strength of scale quantization (0 = free shift) |
| **Root** | C through B | Root note of the musical scale |
| **Scale** | 22 scales | Scale type (Major, Minor, modes, exotic scales) |

**Available Scales:**
Major, Minor, Natural Minor, Harmonic Minor, Melodic Minor, Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, Pentatonic Major, Pentatonic Minor, Blues, Chromatic, Whole Tone, Diminished, Half-Whole Diminished, Arabic, Japanese, Spanish

### Spectral Controls (Spectral Mode Only)

| Parameter | Range | Description |
|-----------|-------|-------------|
| **SMEAR** | 5-123 ms | FFT window size. Larger = smoother but more latent |
| **PRESERVE** | 0-100% | Spectral envelope preservation (maintains timbre) |
| **TRANSIENTS** | 0-100% | Bypass quantization during transients (preserves punch) |
| **SENSITIVITY** | 0-100% | Transient detection threshold |

### Frequency LFO

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Depth** | 0-5000 Hz or 0-360° | Modulation amount |
| **Depth Mode** | Hz / Degrees | Whether depth is absolute Hz or phase-based |
| **Rate** | 0.01-20 Hz | LFO speed (when not synced) |
| **Sync** | On/Off | Lock to host tempo |
| **Division** | 4/1 to 1/32 | Tempo division when synced |
| **Shape** | Sine/Tri/Saw/InvSaw/Random | LFO waveform |

### Delay Section

| Parameter | Range | Description |
|-----------|-------|-------------|
| **DELAY** | On/Off | Enable feedback delay |
| **TIME** | 1-2000 ms | Delay time |
| **Sync** | On/Off | Lock delay to host tempo |
| **Division** | 1/32 to 4/1 | Tempo division when synced |
| **FEEDBACK** | 0-95% | Amount fed back (each repeat gets re-shifted!) |
| **DAMP** | 0-100% | High-frequency rolloff per repeat |
| **SMEAR** | 0-100% | Spectral delay diffusion (Spectral mode only) |
| **MIX** | 0-100% | Echo level in output |

### Delay Time LFO

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Depth** | 0-1000 ms | Delay time modulation amount |
| **Rate** | 0.01-20 Hz | LFO speed |
| **Sync** | On/Off | Lock to host tempo |
| **Division** | 4/1 to 1/32 | Tempo division |
| **Shape** | Sine/Tri/Saw/InvSaw/Random | LFO waveform |

### Spectral Mask (Spectral Mode Only)

| Parameter | Range | Description |
|-----------|-------|-------------|
| **MASK** | On/Off | Enable frequency-selective processing |
| **Mode** | LowPass/HighPass/BandPass | Which frequencies to affect |
| **Low Freq** | 20-20000 Hz | Low cutoff frequency |
| **High Freq** | 20-20000 Hz | High cutoff frequency |
| **Transition** | 0.05-4 octaves | Crossfade width at cutoffs |

---

## 5. DSP Architecture

### FFT Implementation

- **Algorithm:** Cooley-Tukey radix-2 FFT
- **Window:** Hann window (default), also supports Hamming and Blackman
- **Sizes:** 256, 512, 1024, 2048, 4096 samples
- **Overlap:** 4x (75% overlap, hop = fftSize/4)
- **Crossfade:** Smooth morphing between two FFT sizes when SMEAR is between boundaries

**SMEAR Behavior:**
- Maps continuous 5-123ms range to discrete FFT sizes
- When between sizes, runs two FFT processors in parallel
- Equal-power crossfade blends outputs
- Provides smooth timbral transition without clicks

### Hilbert Transform Implementation

- **Architecture:** Two parallel 6th-order allpass filter chains
- **Coefficients:** Olli Niemitalo's design for ~90° phase difference across 20Hz-20kHz
- **Sample Rate:** Optimized for 44.1kHz (may have phase errors at other rates)

**Allpass Coefficients:**
```
Chain I: 0.402, 0.856, 0.972, 0.995, 0.999, 0.9998
Chain Q: 0.168, 0.702, 0.935, 0.986, 0.998, 0.9997
```

**SSB Modulation:**
- Positive shift: `output = I * cos(ωt) - Q * sin(ωt)` (upper sideband)
- Negative shift: `output = I * cos(ωt) + Q * sin(ωt)` (lower sideband)

### Scale Quantization Algorithm

1. **Frequency to MIDI:** Convert each bin's frequency to fractional MIDI note
2. **Find nearest scale degree:** Compare to all notes in current scale
3. **Energy redistribution:** Move magnitude to target bin with accumulation normalization
4. **Phase synthesis:** Maintain phase accumulators per MIDI note for continuity

**Improvements:**
- Accumulation normalization: `sqrt(N)` when N bins map to same target
- Total energy normalization: Preserves input energy
- Per-note phase accumulators: Smooth synthesis across frames
- Silent frame tracking: Reset phase after 8 silent frames (~185ms)

### Spectral Envelope Preservation (PRESERVE)

**Algorithm:**
1. Capture input envelope at ~1/5 octave resolution (48 bands)
2. At PRESERVE > 75%, use ~1/10 octave resolution (96 bands)
3. After quantization, reimpose original envelope shape
4. Blend based on PRESERVE amount

**Benefits:**
- Maintains formant structure of vocals
- Preserves timbral character during large shifts
- Reduces "chipmunk" effect on voices

### Transient Detection and Bypass

**Algorithm:**
1. Calculate total spectral energy per frame
2. Compare to previous frame energy
3. If ratio exceeds threshold (based on SENSITIVITY), mark as transient
4. Ramp down quantization strength over 4 frames during transients

**Benefits:**
- Preserves attack punch on drums and percussive content
- Reduces quantization artifacts on sharp transients

### Feedback Delay Implementation

**Signal Chain:**
```
Output → HPF (80Hz) → Damping LPF → Feedback Buffer → Input Mix
```

**Key Features:**
- **Time-domain buffer:** Up to 2 seconds (96000 samples at 48kHz)
- **HPF:** 2-pole Butterworth at 80Hz prevents low-frequency buildup
- **Damping:** One-pole lowpass, cutoff varies from 1kHz to 12kHz based on parameter
- **Feedback routing:** Goes to INPUT of shifter, so each repeat gets shifted again
- **Latency compensation:** In Spectral mode, feedback delay accounts for FFT latency

### LFO Modulation System

**Two Independent LFOs:**
1. **Frequency LFO:** Modulates shift amount
2. **Delay Time LFO:** Modulates delay time for dub/tape effects

**Waveforms:**
- Sine: `sin(2π * phase)`
- Triangle: `4 * |phase - 0.5| - 1`
- Saw: `2 * phase - 1`
- Inverse Saw: `1 - 2 * phase`
- Random: Sample & hold noise (new value each cycle)

**Tempo Sync:**
- Divisions from 4/1 (4 bars) to 1/32
- Supports triplets (T) and dotted (D) divisions
- Phase advances based on host BPM and division

### Stereo Decorrelation

- **Method:** 0.06ms delay on left channel
- **Purpose:** Reduces phase-locked resonance between channels
- **Implementation:** Simple delay line with fixed offset

---

## 6. Latency Compensation

### Reported Latency

| Mode | Latency (samples) | Latency (ms @ 44.1kHz) |
|------|-------------------|------------------------|
| Spectral | 4096 (MAX_FFT_SIZE) | ~93ms |
| Classic | 12 | ~0.3ms |

### How It Works

**Spectral Mode:**
- Always reports MAX_FFT_SIZE (4096) to host regardless of SMEAR setting
- Internal delay compensation buffer pads smaller FFT outputs to match
- Dry signal delayed by same amount to maintain phase alignment
- Allows smooth SMEAR changes without latency jumps

**Classic Mode:**
- Reports 12 samples (approximate allpass group delay)
- No internal delay compensation needed
- Dry signal not delayed

### Mode Switching

- 15ms equal-power crossfade between modes
- Both processing paths run simultaneously during transition
- Dry signal crossfades between delayed (Spectral) and immediate (Classic)

---

## 7. Known Issues / Quirks

### Classic Mode Limitations

1. **Sample Rate Dependency:** Hilbert transform coefficients are optimized for 44.1kHz. At other sample rates, the 90° phase relationship may be imperfect, causing incomplete sideband suppression.

2. **Negative Shift Artifacts:** Frequencies that shift below 0Hz "fold back" as positive frequencies, creating metallic/resonant artifacts. This is inherent to frequency shifting, not a bug.

3. **Allpass Phase Response:** Even at 0Hz shift, the signal passes through allpass filters which alter phase response. This is audible as a subtle coloration.

### Spectral Mode Limitations

1. **Latency:** 93ms latency is unavoidable for the FFT-based approach. Not suitable for real-time performance monitoring.

2. **Transient Smearing:** Large SMEAR values blur transients. Use TRANSIENTS control or smaller SMEAR for percussive material.

3. **Pre-echo:** At high SMEAR settings, the windowing can cause subtle pre-echo artifacts.

### General Quirks

1. **WARM Filter:** Fixed at 4.5kHz cutoff. May be too aggressive for some material.

2. **Feedback with Delay Off:** WARM filter only processes direct wet signal. Feedback path uses separate damping.

3. **Scale Quantization Energy:** Very strong quantization can cause amplitude fluctuations as energy concentrates on fewer bins.

---

## 8. Class Structure

### Main Classes

| Class | File | Responsibility |
|-------|------|----------------|
| `FrequencyShifterProcessor` | PluginProcessor.h/cpp | Main JUCE processor, parameter handling, signal routing |
| `FrequencyShifterEditor` | PluginEditor.h/cpp | GUI implementation |

### DSP Classes

| Class | File | Responsibility |
|-------|------|----------------|
| `STFT` | dsp/STFT.h/cpp | Short-Time Fourier Transform (analysis/synthesis) |
| `PhaseVocoder` | dsp/PhaseVocoder.h/cpp | Phase coherence for artifact reduction |
| `FrequencyShifter` | dsp/FrequencyShifter.h/cpp | Spectral bin reassignment |
| `HilbertShifter` | dsp/HilbertShifter.h | Classic mode SSB frequency shifting |
| `MusicalQuantizer` | dsp/MusicalQuantizer.h/cpp | Scale-based frequency quantization |
| `SpectralMask` | dsp/SpectralMask.h | Frequency-selective processing |
| `SpectralDelay` | dsp/SpectralDelay.h | Per-bin delay for diffusion effects |

### Utility Headers

| File | Contents |
|------|----------|
| `dsp/Scales.h` | Scale definitions, MIDI/frequency conversion utilities |

### Key Data Structures

**Per-Channel Processing:**
- 2 channels maximum (stereo)
- 2 FFT processors per channel (for crossfade)
- Independent state arrays for all filters and buffers

**Atomic Parameters:**
- All user-facing parameters are `std::atomic` for thread safety
- Parameter changes handled via `AudioProcessorValueTreeState::Listener`

---

## Document Version

- **Plugin Version:** v78
- **Last Updated:** February 2025
- **Authors:** Original implementation with Claude Code assistance
