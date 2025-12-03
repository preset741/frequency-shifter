---
layout: default
title: User Guide
---

# Frequency Shifter User Guide

A comprehensive reference for all parameters, modes, and creative techniques.

---

## Table of Contents

- [Overview](#overview)
- [Main Controls](#main-controls)
- [Scale Quantization](#scale-quantization)
- [Quality & Processing](#quality--processing)
- [Drift Modulation](#drift-modulation)
- [Stochastic Modes](#stochastic-modes)
- [Spectral Mask](#spectral-mask)
- [Spectral Delay](#spectral-delay)
- [Preset Ideas & Recipes](#preset-ideas--recipes)

---

## Overview

The Frequency Shifter is a spectral processing plugin that moves all frequencies in your audio by a fixed Hz amount. Unlike pitch shifting (which preserves harmonic ratios), frequency shifting creates inharmonic, often metallic textures.

**Signal Flow:**
```
Input → STFT → Freq Shift → Quantize → Mask → Delay → ISTFT → Output
```

---

## Main Controls

### Shift (Hz)
**Range:** -20,000 Hz to +20,000 Hz
**Default:** 0 Hz

The main frequency shift amount. This shifts ALL frequencies by the same Hz value.

| Value | Effect |
|-------|--------|
| **+5 to +50 Hz** | Subtle detuning, phasing, chorus-like warmth |
| **+50 to +200 Hz** | Metallic, robotic character |
| **+200 to +1000 Hz** | Extreme transformation, bell-like tones |
| **-50 to -200 Hz** | Darker, underwater quality |
| **Large negative** | Inverted, alien textures |

**Tip:** The knob uses a logarithmic scale for precise control near zero.

### Dry/Wet
**Range:** 0% to 100%
**Default:** 100%

Blends between the original (dry) and processed (wet) signal.

- **0%**: Original audio only
- **50%**: Equal blend (creates phasing effects)
- **100%**: Fully processed

### Spectrum Toggle
Click to show/hide the real-time frequency spectrum analyzer. Displays the output frequencies with auto-scaling.

---

## Scale Quantization

Quantization snaps shifted frequencies to musical scale degrees, making the output more tonal.

### Quantize Strength
**Range:** 0% to 100%
**Default:** 0%

| Value | Effect |
|-------|--------|
| **0%** | Pure frequency shifting (no quantization) |
| **25-50%** | Subtle pitch correction, retains character |
| **75-100%** | Strong quantization, very tonal output |

### Root Note
**Options:** C, C#, D, D#, E, F, F#, G, G#, A, A#, B
**Default:** C

Sets the tonal center. All scale degrees are calculated relative to this note.

### Scale Type
**22 scales available:**

| Scale | Character |
|-------|-----------|
| **Major** | Bright, happy, classic |
| **Minor (Natural)** | Dark, melancholic |
| **Harmonic Minor** | Exotic, Middle Eastern flavor |
| **Melodic Minor** | Jazz, sophisticated |
| **Dorian** | Minor with bright 6th, jazz/funk |
| **Phrygian** | Spanish, flamenco feel |
| **Lydian** | Dreamy, floating, film scores |
| **Mixolydian** | Bluesy major, rock |
| **Locrian** | Unstable, diminished, dark |
| **Pentatonic Major** | Simple, folk, Asian |
| **Pentatonic Minor** | Blues rock, safe improvisation |
| **Blues** | Gritty, soulful |
| **Whole Tone** | Dreamlike, Debussy |
| **Diminished (HW)** | Tension, jazz, symmetric |
| **Diminished (WH)** | Tension, jazz, symmetric |
| **Augmented** | Unsettling, symmetric |
| **Chromatic** | All 12 notes (minimal quantization effect) |
| **Hirajoshi** | Japanese, pentatonic |
| **Hungarian Minor** | Eastern European, dramatic |
| **Neapolitan Minor** | Classical, ornate |
| **Neapolitan Major** | Classical, ornate |
| **Enigmatic** | Rare, mysterious |

---

## Quality & Processing

### Phase Vocoder
**Toggle:** On/Off
**Default:** On

When enabled, uses phase vocoder processing for higher quality output with better phase coherence. Reduces artifacts at the cost of slightly more CPU.

- **On**: Recommended for most uses, cleaner sound
- **Off**: Raw frequency shifting, more artifacts but unique character

### Quality Mode
**Options:** Low Latency, Balanced, Quality
**Default:** Quality

Controls FFT size and overlap, affecting latency and sound quality.

| Mode | FFT Size | Latency | Best For |
|------|----------|---------|----------|
| **Low Latency** | 1024 | ~23ms | Live performance, monitoring |
| **Balanced** | 2048 | ~46ms | General use |
| **Quality** | 4096 | ~93ms | Mixing, offline processing |

---

## Drift Modulation

Adds organic movement to the quantization, making it less static and robotic.

### Drift Amount
**Range:** 0% to 100%
**Default:** 0%

How much the frequencies deviate from their quantized targets.

- **0%**: No drift, static quantization
- **10-30%**: Subtle vibrato-like movement
- **50-100%**: Wild, unpredictable pitch wandering

### Drift Rate
**Range:** 0.1 Hz to 10 Hz
**Default:** 1 Hz

Speed of the drift modulation.

- **0.1-0.5 Hz**: Slow, evolving movement
- **1-2 Hz**: Natural vibrato speed
- **5-10 Hz**: Fast wobble, tremolo-like

### Drift Mode
**Options:** LFO, Perlin, Stochastic
**Default:** LFO

| Mode | Character |
|------|-----------|
| **LFO** | Regular, predictable sine wave modulation |
| **Perlin** | Smooth organic noise, natural-sounding drift |
| **Stochastic** | Random, unpredictable, with sub-modes (see below) |

---

## Stochastic Modes

When Drift Mode is set to **Stochastic**, these additional controls become active.

### Stochastic Type
**Options:** Poisson, Random Walk, Jump Diffusion

#### Poisson
Random events occur at a rate determined by density. Creates sporadic, unpredictable jumps.
- Good for: Glitchy textures, random pitch events

#### Random Walk
Each value is based on the previous, creating wandering motion that accumulates over time.
- Good for: Gradually evolving, drifting textures

#### Jump Diffusion
Combines smooth diffusion with occasional sudden jumps. The most complex and unpredictable.
- Good for: Hybrid textures, controlled chaos

### Stochastic Density
**Range:** 0% to 100%
**Default:** 50%

Controls how frequently random events occur.

- **Low (0-30%)**: Sparse, occasional events
- **Medium (30-70%)**: Moderate activity
- **High (70-100%)**: Dense, frequent events

### Stochastic Smoothness
**Range:** 0% to 100%
**Default:** 50%

Controls interpolation between random values.

- **Low (0-30%)**: Abrupt, stepped changes
- **Medium (30-70%)**: Moderate smoothing
- **High (70-100%)**: Very smooth, filtered randomness

---

## Spectral Mask

Selectively applies the frequency shift effect to only certain frequency ranges. Toggle **Mask** to enable.

### Mask Mode
**Options:** Low Pass, High Pass, Band Pass

| Mode | Effect |
|------|--------|
| **Low Pass** | Only frequencies BELOW the high cutoff are shifted |
| **High Pass** | Only frequencies ABOVE the low cutoff are shifted |
| **Band Pass** | Only frequencies BETWEEN low and high are shifted |

### Low Frequency
**Range:** 20 Hz to 20,000 Hz
**Default:** 200 Hz

The lower boundary of the mask. Frequencies below this are:
- **Band Pass**: Unaffected (dry)
- **High Pass**: Unaffected (dry)
- **Low Pass**: Processed (wet)

### High Frequency
**Range:** 20 Hz to 20,000 Hz
**Default:** 5,000 Hz

The upper boundary of the mask. Frequencies above this are:
- **Band Pass**: Unaffected (dry)
- **High Pass**: Processed (wet)
- **Low Pass**: Unaffected (dry)

### Transition (Trans)
**Range:** 0.1 to 4.0 octaves
**Default:** 1.0 octave

Width of the smooth crossfade between affected and unaffected regions. Uses Hermite smoothstep for artifact-free transitions.

- **0.1-0.5 oct**: Sharp transition, more obvious splits
- **1-2 oct**: Natural, gradual blend
- **3-4 oct**: Very smooth, subtle masking

---

## Spectral Delay

A frequency-dependent delay that processes each FFT bin separately. Toggle **Delay** to enable.

### Time
**Range:** 10 ms to 2,000 ms
**Default:** 200 ms

Base delay time. This is the delay applied at the center frequency. Uses logarithmic scaling.

### Slope
**Range:** -100% to +100%
**Default:** 0%

How delay time varies across the frequency spectrum.

| Value | Effect |
|-------|--------|
| **-100%** | Low frequencies delayed MORE than high (natural reverb-like) |
| **0%** | All frequencies delayed equally (standard delay) |
| **+100%** | High frequencies delayed MORE than low (unusual, experimental) |

### Feedback (FDBK)
**Range:** 0% to 95%
**Default:** 30%

How much of the delayed signal is fed back into the delay line.

- **0%**: Single echo
- **30-50%**: Multiple echoes, gradual decay
- **70-95%**: Long, resonant tails (watch for buildup!)

### Damping (DAMP)
**Range:** 0% to 100%
**Default:** 30%

High-frequency absorption per delay repeat. Simulates natural acoustic absorption.

- **0%**: No damping, bright repeats
- **30-50%**: Natural decay, like a room
- **70-100%**: Dark, muffled repeats

### Mix
**Range:** 0% to 100%
**Default:** 50%

Amount of delayed signal added to the dry signal. This is ADDITIVE, not a crossfade.

- **0%**: No delay audible
- **50%**: Moderate delay presence
- **100%**: Full delay signal added

### Gain
**Range:** -12 dB to +24 dB
**Default:** 0 dB

Boost or cut the delayed signal level.

- **-12 to 0 dB**: Subtle delay presence
- **+6 to +12 dB**: Prominent delay effect
- **+12 to +24 dB**: Delay dominates (use with low mix)

---

## Preset Ideas & Recipes

### Classic Metallic Voice
| Parameter | Value |
|-----------|-------|
| Shift | +80 Hz |
| Quantize | 0% |
| Dry/Wet | 100% |
| Phase Vocoder | On |

*Robot voice, Dalek-style effect.*

---

### Harmonic Re-tuning
| Parameter | Value |
|-----------|-------|
| Shift | +50 to +200 Hz |
| Quantize | 100% |
| Root | Match your song key |
| Scale | Major or Minor |
| Dry/Wet | 70-100% |

*Force any audio into a specific key.*

---

### Subtle Chorus/Detune
| Parameter | Value |
|-----------|-------|
| Shift | +7 Hz |
| Quantize | 0% |
| Dry/Wet | 50% |

*Creates a natural doubling/chorus effect.*

---

### Evolving Pad Texture
| Parameter | Value |
|-----------|-------|
| Shift | +100 Hz |
| Quantize | 60% |
| Scale | Lydian or Whole Tone |
| Drift Amount | 40% |
| Drift Mode | Perlin |
| Drift Rate | 0.3 Hz |

*Slowly morphing, ethereal texture.*

---

### Glitchy Experimental
| Parameter | Value |
|-----------|-------|
| Shift | +300 Hz |
| Quantize | 50% |
| Drift Mode | Stochastic |
| Stochastic Type | Jump Diffusion |
| Density | 70% |
| Smoothness | 20% |

*Unpredictable, IDM-style pitch glitches.*

---

### Bass Enhancement with Mask
| Parameter | Value |
|-----------|-------|
| Shift | +30 Hz |
| Mask | On |
| Mode | Low Pass |
| High Freq | 300 Hz |
| Transition | 1 oct |
| Dry/Wet | 100% |

*Shift only the sub frequencies, leave mids/highs clean.*

---

### Vocal Isolation Effect
| Parameter | Value |
|-----------|-------|
| Shift | +150 Hz |
| Mask | On |
| Mode | Band Pass |
| Low Freq | 300 Hz |
| High Freq | 4000 Hz |
| Transition | 0.5 oct |

*Shift the vocal range, keep bass and air intact.*

---

### Spectral Reverb/Smear
| Parameter | Value |
|-----------|-------|
| Shift | +20 Hz |
| Delay | On |
| Time | 400 ms |
| Slope | -50% |
| Feedback | 60% |
| Damping | 50% |
| Mix | 70% |
| Gain | +6 dB |

*Low frequencies ring out longer, natural decay.*

---

### Shimmer Effect
| Parameter | Value |
|-----------|-------|
| Shift | +1200 Hz (one octave-ish) |
| Quantize | 100% |
| Scale | Major |
| Delay | On |
| Time | 150 ms |
| Feedback | 70% |
| Damping | 30% |
| Mix | 40% |
| Gain | +12 dB |

*Octave-up shimmer reverb effect.*

---

### Dark Underwater
| Parameter | Value |
|-----------|-------|
| Shift | -100 Hz |
| Quantize | 30% |
| Scale | Phrygian |
| Delay | On |
| Time | 800 ms |
| Slope | +30% |
| Damping | 80% |
| Mix | 50% |

*Submerged, murky texture with high frequencies echoing longer.*

---

### Randomized Ambient Drone
| Parameter | Value |
|-----------|-------|
| Shift | +50 Hz |
| Quantize | 80% |
| Scale | Pentatonic Minor |
| Drift Mode | Stochastic |
| Type | Random Walk |
| Density | 40% |
| Smoothness | 90% |
| Delay | On |
| Time | 1500 ms |
| Feedback | 80% |
| Mix | 60% |

*Ever-evolving generative texture.*

---

## Tips & Tricks

1. **Start subtle**: Small shifts (5-50 Hz) often sound better than extreme values.

2. **Use the mask** to protect bass frequencies from shifting — keeps the low end solid.

3. **Quantize + Drift** together creates organic, musical results that don't sound robotic.

4. **Spectral delay slope** at -50% to -100% creates natural reverb-like diffusion.

5. **For live use**, choose Low Latency quality mode to minimize delay.

6. **Blend with Dry/Wet** at 30-50% for more natural integration.

7. **Automate the Shift** parameter for sweeping, filter-like effects.

8. **Stack multiple instances** with different settings for complex layered textures.

---

## Troubleshooting

**No sound / silence**
- Check Dry/Wet is not at 0%
- Ensure the plugin is not bypassed
- Verify input signal is reaching the plugin

**Metallic artifacts**
- Enable Phase Vocoder
- Use Quality mode instead of Low Latency
- Reduce the shift amount

**Delay not audible**
- Increase Delay Gain (+6 to +12 dB)
- Increase Delay Mix (50-100%)
- Ensure Delay toggle is enabled

**CPU usage too high**
- Use Balanced or Low Latency quality mode
- Disable Spectrum analyzer when not needed

---

<a href="index.html" class="btn">← Back to Home</a>
<a href="algorithm.html" class="btn btn-outline">Technical Details →</a>
