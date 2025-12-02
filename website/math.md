---
layout: default
title: Mathematical Foundation
---

# Mathematical Foundation

Complete mathematical specification for the harmonic-preserving frequency shifter.

## 1. Core Concepts

### Frequency Shifting vs Pitch Shifting

<div class="feature-grid">
  <div class="feature-card">
    <h4>Frequency Shifting (Linear)</h4>
    <p>$$f_{\text{out}} = f_{\text{in}} + \Delta f$$</p>
    <ul>
      <li>Adds/subtracts fixed Hz offset</li>
      <li>Destroys harmonic relationships</li>
      <li>Creates metallic/inharmonic sounds</li>
    </ul>
  </div>
  <div class="feature-card">
    <h4>Pitch Shifting (Multiplicative)</h4>
    <p>$$f_{\text{out}} = f_{\text{in}} \times r$$</p>
    <ul>
      <li>Scales by ratio</li>
      <li>Preserves harmonic relationships</li>
      <li>Changes perceived pitch</li>
    </ul>
  </div>
</div>

**Our Hybrid Approach:**
1. Apply frequency shift in spectral domain
2. Quantize shifted frequencies to musical scale
3. Preserve harmonic coherence through phase vocoder

---

## 2. Short-Time Fourier Transform (STFT)

### Forward Transform

For audio signal $x[n]$, apply windowed FFT:

$$X[k, m] = \sum_{n=0}^{N-1} x[n + mH] \cdot w[n] \cdot e^{-j\frac{2\pi kn}{N}}$$

Where:
- $k$ = frequency bin index $(0$ to $N-1)$
- $m$ = frame index
- $H$ = hop size (samples between frames)
- $N$ = FFT size
- $w[n]$ = window function

### Magnitude and Phase

$$|X[k, m]| = \sqrt{\text{Re}(X)^2 + \text{Im}(X)^2}$$

$$\phi[k, m] = \arctan2\left(\text{Im}(X), \text{Re}(X)\right)$$

### Frequency Resolution

$$\Delta f = \frac{f_s}{N}$$

$$f[k] = k \cdot \Delta f$$

<div class="info-box">
<strong>Example:</strong> At $f_s = 44100$ Hz with $N = 4096$:

$$\Delta f = \frac{44100}{4096} \approx 10.77 \text{ Hz/bin}$$
</div>

---

## 3. Frequency Shifting

### Linear Shift Operation

For each frequency bin $k$ at frequency $f[k]$:

$$f_{\text{shifted}}[k] = f[k] + \Delta f_{\text{shift}}$$

$$k_{\text{new}} = \text{round}\left(\frac{f_{\text{shifted}}[k]}{\Delta f}\right)$$

### Magnitude Redistribution

When multiple bins map to the same target (energy conservation):

$$|Y[k_{\text{target}}]| = \sqrt{\sum_{i} |X[k_{\text{source},i}]|^2}$$

This maintains RMS power via **Parseval's theorem**.

---

## 4. Musical Quantization

### Frequency to MIDI Conversion

$$\text{MIDI} = 69 + 12 \cdot \log_2\left(\frac{f}{440}\right)$$

Where:
- 69 = MIDI note for A4 (440 Hz)
- 440 Hz = reference frequency

### Scale Quantization Algorithm

Given scale degrees $S = \{s_0, s_1, ..., s_n\}$ relative to root:

$$\text{relative} = (\text{MIDI} - \text{root}) \mod 12$$

$$\text{closest} = \arg\min_{s \in S} \left| \text{relative} - s \right|$$

$$\text{octave} = \left\lfloor \frac{\text{MIDI} - \text{root}}{12} \right\rfloor$$

$$\text{MIDI}_{\text{quantized}} = \text{root} + \text{octave} \times 12 + \text{closest}$$

### MIDI to Frequency Conversion

$$f = 440 \cdot 2^{\frac{\text{MIDI} - 69}{12}}$$

### Quantization Strength

Interpolate between shifted and quantized:

$$f_{\text{final}} = (1 - \alpha) \cdot f_{\text{shifted}} + \alpha \cdot f_{\text{quantized}}$$

Where $\alpha \in [0, 1]$:
- $\alpha = 0$: pure frequency shift
- $\alpha = 1$: fully quantized

---

## 5. Phase Vocoder

### Phase Propagation

When processing frame $m$, compute phase deviation:

$$\Delta\phi[k] = \phi[k, m] - \phi[k, m-1] - \frac{2\pi k H}{N}$$

### Phase Wrapping

$$\Delta\phi_{\text{wrapped}} = \left((\Delta\phi[k] + \pi) \mod 2\pi\right) - \pi$$

### Instantaneous Frequency

$$f_{\text{inst}}[k] = \frac{k \cdot f_s}{N} + \frac{\Delta\phi_{\text{wrapped}} \cdot f_s}{2\pi H}$$

### Phase Synthesis

$$\phi_{\text{synth}}[k] = \phi_{\text{prev}}[k] + \frac{2\pi \cdot f_{\text{new}}[k] \cdot H}{f_s}$$

### Phase Transfer to New Bin

$$\phi[k_{\text{new}}, m] = \phi[k, m-1] + \phi_{\text{inst}}[k] \cdot \frac{f[k_{\text{new}}]}{f[k]}$$

---

## 6. Overlap-Add Reconstruction

### Inverse STFT

$$y[n] = \sum_{m} \text{IFFT}(Y[k, m]) \cdot w[n - mH]$$

### Window Normalization

For perfect reconstruction with overlap factor $R = N/H$:

$$w_{\text{normalized}}[n] = \frac{w[n]}{\sum_{m} w^2[n - mH]}$$

<div class="info-box accent">
<strong>Common overlap factors:</strong>
<ul>
<li>2× ($H = N/2$): Hann window</li>
<li>4× ($H = N/4$): Better for modification (default)</li>
<li>8× ($H = N/8$): Highest quality</li>
</ul>
</div>

---

## 7. Energy Conservation

### Parseval's Theorem

Total energy in time domain equals total energy in frequency domain:

$$E_{\text{time}} = \sum_{n} |x[n]|^2$$

$$E_{\text{freq}} = \frac{1}{N} \sum_{k} |X[k]|^2$$

### Normalization After Binning

$$|Y[k_{\text{target}}]| = \sqrt{\sum_{\text{sources}} |X[k_{\text{source}}]|^2}$$

---

## 8. Scale Definitions

### Common Scales (semitones from root)

| Scale | Degrees | Notes (from C) |
|-------|---------|----------------|
| **Major** | $\{0, 2, 4, 5, 7, 9, 11\}$ | C D E F G A B |
| **Minor** | $\{0, 2, 3, 5, 7, 8, 10\}$ | C D E♭ F G A♭ B♭ |
| **Harmonic Minor** | $\{0, 2, 3, 5, 7, 8, 11\}$ | C D E♭ F G A♭ B |
| **Melodic Minor** | $\{0, 2, 3, 5, 7, 9, 11\}$ | C D E♭ F G A B |
| **Dorian** | $\{0, 2, 3, 5, 7, 9, 10\}$ | C D E♭ F G A B♭ |
| **Phrygian** | $\{0, 1, 3, 5, 7, 8, 10\}$ | C D♭ E♭ F G A♭ B♭ |
| **Lydian** | $\{0, 2, 4, 6, 7, 9, 11\}$ | C D E F♯ G A B |
| **Mixolydian** | $\{0, 2, 4, 5, 7, 9, 10\}$ | C D E F G A B♭ |
| **Pentatonic Major** | $\{0, 2, 4, 7, 9\}$ | C D E G A |
| **Pentatonic Minor** | $\{0, 3, 5, 7, 10\}$ | C E♭ F G B♭ |
| **Blues** | $\{0, 3, 5, 6, 7, 10\}$ | C E♭ F F♯ G B♭ |
| **Chromatic** | $\{0, 1, 2, ..., 11\}$ | All 12 notes |
| **Whole Tone** | $\{0, 2, 4, 6, 8, 10\}$ | C D E F♯ G♯ A♯ |

---

## 9. Performance Metrics

### Latency

$$\text{latency}_{\text{samples}} = N + H$$

$$\text{latency}_{\text{ms}} = \frac{N + H}{f_s} \times 1000$$

| FFT Size ($N$) | Hop Size ($H$) | Latency |
|----------------|----------------|---------|
| 2048 | 512 | ~58 ms |
| 4096 | 1024 | ~116 ms |
| 8192 | 2048 | ~232 ms |

### Computational Complexity

$$O(N \log N) \text{ per frame}$$

$$\text{frames/second} = \frac{f_s}{H}$$

---

## 10. Edge Cases

### DC and Nyquist

- **DC bin** ($k=0$): Leave unshifted (represents constant offset)
- **Nyquist bin** ($k=N/2$): Handle carefully to avoid aliasing

### Aliasing Prevention

$$\text{if } f_{\text{shifted}} > \frac{f_s}{2} \text{ then } f_{\text{shifted}} = \frac{f_s}{2} - \Delta f$$

---

## 11. Quality Metrics

### Target Specifications

| Metric | Target |
|--------|--------|
| **Frequency accuracy** | Within 1 cent of target |
| **Energy conservation** | Within 0.1 dB |
| **Phase continuity** | No discontinuities $> \pi$ |
| **THD** | < 1% |
| **SNR** | > 60 dB |

### Cents (Pitch Difference)

$$\text{cents} = 1200 \cdot \log_2\left(\frac{f_2}{f_1}\right)$$

---

## References

1. **Laroche, J., & Dolson, M. (1999)**. "Improved phase vocoder time-scale modification of audio." *IEEE Transactions on Speech and Audio Processing.*

2. **Zölzer, U. (2011)**. "DAFX: Digital Audio Effects" (2nd ed.). *Wiley.*

3. **Smith, J. O. (2011)**. "Spectral Audio Signal Processing." *W3K Publishing.* [Online](https://ccrma.stanford.edu/~jos/sasp/)

---

[Back to Home](index.html) | [Algorithm Details](algorithm.html)
