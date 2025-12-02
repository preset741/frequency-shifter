---
layout: default
title: Algorithm Details
---

# Algorithm Documentation

This page provides a comprehensive overview of the harmonic-preserving frequency shifter algorithm.

## Table of Contents

1. [Core Concept](#core-concept)
2. [Processing Pipeline](#processing-pipeline)
3. [Mathematical Foundation](#mathematical-foundation)
4. [Key Components](#key-components)
5. [Parameter Guide](#parameter-guide)
6. [Research References](#research-references)

---

## Core Concept

### The Problem

Traditional frequency shifting adds a fixed Hz offset to all frequencies:

$$f_{\text{output}} = f_{\text{input}} + \Delta f$$

This **destroys harmonic relationships**. For example:

| Original | After +100 Hz Shift |
|----------|---------------------|
| 440 Hz (fundamental) | 540 Hz |
| 880 Hz (2nd harmonic) | 980 Hz |
| 1320 Hz (3rd harmonic) | 1420 Hz |

The shifted frequencies are **no longer harmonically related**, resulting in a metallic, inharmonic sound.

### Our Solution

We combine three techniques:

1. **Spectral Frequency Shifting** — Linear Hz offset in frequency domain
2. **Musical Scale Quantization** — Snap shifted frequencies to nearest scale notes
3. **Enhanced Phase Vocoder** — Maintain phase coherence to reduce artifacts

---

## Processing Pipeline

<div class="mermaid">
flowchart TD
    subgraph Analysis ["📊 Analysis"]
        A1[Window Function] --> A2[Forward FFT]
        A2 --> A3[Extract Magnitude & Phase]
    end

    subgraph Processing ["⚙️ Processing"]
        P1[Frequency Shifting] --> P2[Musical Quantization]
        P2 --> P3[Phase Vocoder]
    end

    subgraph Synthesis ["🔊 Synthesis"]
        S1[Inverse FFT] --> S2[Overlap-Add]
        S2 --> S3[Window Normalization]
    end

    IN[🎵 Input Audio] --> Analysis
    Analysis --> Processing
    Processing --> Synthesis
    Synthesis --> OUT[🔊 Output Audio]

    style IN fill:#7c3aed,stroke:#9333ea,color:#fff
    style OUT fill:#cd8b32,stroke:#b8860b,color:#fff
    style Analysis fill:#1f1714,stroke:#3d3330,color:#f5f0e6
    style Processing fill:#1f1714,stroke:#3d3330,color:#f5f0e6
    style Synthesis fill:#1f1714,stroke:#3d3330,color:#f5f0e6
</div>

---

## Mathematical Foundation

### Short-Time Fourier Transform

The STFT converts audio from time domain to time-frequency representation.

**Forward Transform:**

$$X[k, m] = \sum_{n=0}^{N-1} x[n + mH] \cdot w[n] \cdot e^{-j\frac{2\pi kn}{N}}$$

Where:
- $k$ = frequency bin index $(0$ to $N-1)$
- $m$ = frame index
- $H$ = hop size (samples between frames)
- $N$ = FFT size
- $w[n]$ = window function

**Magnitude and Phase:**

$$|X[k, m]| = \sqrt{\text{Re}(X)^2 + \text{Im}(X)^2}$$

$$\phi[k, m] = \arctan2(\text{Im}(X), \text{Re}(X))$$

**Frequency Resolution:**

$$\Delta f = \frac{f_s}{N}$$

$$f[k] = k \cdot \Delta f$$

<div class="info-box">
<strong>Example:</strong> At $f_s = 44100$ Hz with $N = 4096$:

$$\Delta f = \frac{44100}{4096} \approx 10.77 \text{ Hz per bin}$$
</div>

---

### Frequency Shifting

For each frequency bin $k$:

$$f_{\text{shifted}} = f[k] + f_{\text{shift}}$$

$$k_{\text{new}} = \text{round}\left(\frac{f_{\text{shifted}}}{\Delta f}\right)$$

**Magnitude redistribution with energy conservation:**

$$|Y[k_{\text{target}}]| = \sqrt{\sum_{\text{sources}} |X[k_{\text{source}}]|^2}$$

---

### Musical Quantization

**Frequency to MIDI:**

$$\text{MIDI} = 69 + 12 \cdot \log_2\left(\frac{f}{440}\right)$$

**Scale Quantization:**

$$\text{relative} = (\text{MIDI} - \text{root}) \mod 12$$

$$\text{closest} = \arg\min_{d \in \text{scale}} |\ \text{relative} - d\ |$$

$$\text{MIDI}_{\text{quantized}} = \text{root} + \text{octave} \times 12 + \text{closest}$$

**MIDI to Frequency:**

$$f = 440 \cdot 2^{\frac{\text{MIDI} - 69}{12}}$$

**Quantization Strength:**

$$f_{\text{final}} = (1 - \alpha) \cdot f_{\text{shifted}} + \alpha \cdot f_{\text{quantized}}$$

Where $\alpha \in [0, 1]$:
- $\alpha = 0$: Pure frequency shift (inharmonic)
- $\alpha = 1$: Fully quantized to scale (harmonic)

---

### Phase Vocoder Equations

**Expected Phase Advance:**

$$\phi_{\text{expected}}[k] = \frac{2\pi k H}{N}$$

**Phase Deviation:**

$$\Delta\phi = \phi_{\text{curr}} - \phi_{\text{prev}} - \phi_{\text{expected}}$$

**Instantaneous Frequency:**

$$f_{\text{inst}}[k] = f_{\text{bin}}[k] + \frac{\Delta\phi \cdot f_s}{2\pi H}$$

**Phase Synthesis:**

$$\phi_{\text{synth}}[k] = \phi_{\text{prev}}[k] + \frac{2\pi f_{\text{new}}[k] \cdot H}{f_s}$$

---

## Key Components

### 1. STFT (Short-Time Fourier Transform)

Converts audio from time domain to time-frequency representation.

| Parameter | Values | Trade-off |
|-----------|--------|-----------|
| FFT Size | 2048, 4096, 8192 | Larger = better frequency resolution, more latency |
| Hop Size | N/4 recommended | Smaller = better quality, more computation |
| Window | Hann (default) | Good balance of frequency/time resolution |

### 2. Frequency Shifter

Moves all frequency content by a fixed Hz amount.

<div class="mermaid">
flowchart LR
    A[Bin k at f Hz] -->|+ shift_hz| B[Bin k_new at f + shift Hz]

    style A fill:#3d2963,stroke:#5b4180,color:#f5f0e6
    style B fill:#cd8b32,stroke:#b8860b,color:#fff
</div>

### 3. Musical Quantizer

Snaps frequencies to the nearest notes in a musical scale.

**Supported Scales:**

| Category | Scales |
|----------|--------|
| Western | Major, Minor, Harmonic Minor, Melodic Minor |
| Modes | Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian |
| Pentatonic | Major Pentatonic, Minor Pentatonic |
| Other | Blues, Chromatic, Whole Tone, Diminished |
| World | Arabic, Japanese, Spanish |

### 4. Phase Vocoder

Maintains phase coherence during spectral modifications using **identity phase locking** (Laroche & Dolson, 1999).

**Key Techniques:**

1. **Peak Detection**: Identify spectral peaks (harmonics, formants)
2. **Identity Phase Locking**: Lock phases around peaks
3. **Instantaneous Frequency**: Calculate true frequency in each bin
4. **Phase Synthesis**: Generate coherent phases for modified spectrum

---

## Parameter Guide

### Quality Modes

| Mode | FFT Size | Hop Size | Latency | Best For |
|------|----------|----------|---------|----------|
| Low Latency | 2048 | 512 | ~58 ms | Live use |
| Balanced | 4096 | 1024 | ~116 ms | General purpose |
| Quality | 8192 | 2048 | ~232 ms | Offline, bass-heavy |

**Latency Formula:**

$$\text{latency} = \frac{N + H}{f_s}$$

### Recommended Settings

<div class="info-box accent">
<strong>Metallic/Robotic Effects:</strong>
<ul>
<li>Shift: 50-200 Hz</li>
<li>Quantize: 0%</li>
<li>Quality: Low Latency or Balanced</li>
</ul>
</div>

<div class="info-box accent">
<strong>Re-harmonization:</strong>
<ul>
<li>Shift: Any amount</li>
<li>Quantize: 100%</li>
<li>Scale: Choose your target key</li>
<li>Quality: Balanced or Quality</li>
</ul>
</div>

<div class="info-box accent">
<strong>Subtle Chorus/Detuning:</strong>
<ul>
<li>Shift: 5-20 Hz</li>
<li>Quantize: 30-50%</li>
<li>Quality: Balanced</li>
</ul>
</div>

---

## Performance Characteristics

### Computational Complexity

Per frame: $O(N \log N)$ for FFT operations

For 1 second of audio at 44.1kHz with $N=4096$, $H=1024$:
- Frames: ~43
- Operations: ~2.1M

### Known Limitations

1. **Latency**: Not suitable for live performance (needs <10ms)
2. **Transients**: Percussive material may smear slightly
3. **Low Frequencies**: Coarse quantization below 100 Hz with small FFT
4. **Extreme Shifts**: Best quality within ±500 Hz range

---

## Research References

### Core Algorithm

1. **Laroche, J., & Dolson, M. (1999)**
   "Improved phase vocoder time-scale modification of audio"
   *IEEE Transactions on Speech and Audio Processing*

2. **Zölzer, U. (2011)**
   "DAFX: Digital Audio Effects" (2nd ed.)
   *Wiley*

3. **Smith, J. O. (2011)**
   "Spectral Audio Signal Processing"
   *W3K Publishing* — [Online](https://ccrma.stanford.edu/~jos/sasp/)

### Additional Resources

- Flanagan & Golden (1966) — Original phase vocoder concept
- Dolson (1986) — "The phase vocoder: A tutorial"
- Průša & Holighaus (2022) — "Phase Vocoder Done Right"

---

[Back to Home](index.html)
