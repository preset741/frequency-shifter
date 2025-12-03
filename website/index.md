---
layout: default
title: Harmonic Frequency Shifter
---

# Harmonic Frequency Shifter

A VST3/AU audio plugin that combines frequency shifting with musical scale quantization for creative sound design.

![Plugin Screenshot](plugin.png)

## What It Does

This plugin performs **frequency shifting** — moving all frequencies in your audio by a fixed Hz amount — while keeping the output **musical** through intelligent scale quantization.

Unlike pitch shifting (which preserves harmonic relationships), frequency shifting creates unique, often metallic or otherworldly tones. By adding scale quantization, you get the best of both worlds: the character of frequency shifting with musical coherence.

## Features

<div class="feature-grid">
  <div class="feature-card">
    <h4>Frequency Shift</h4>
    <p>±20,000 Hz range with linear or logarithmic control</p>
  </div>
  <div class="feature-card">
    <h4>Musical Quantization</h4>
    <p>Snap frequencies to any of 22 musical scales</p>
  </div>
  <div class="feature-card">
    <h4>Phase Vocoder</h4>
    <p>High-quality processing with minimal artifacts</p>
  </div>
  <div class="feature-card">
    <h4>Quality Modes</h4>
    <p>Low Latency, Balanced, or Quality presets</p>
  </div>
  <div class="feature-card">
    <h4>Spectrum Analyzer</h4>
    <p>Real-time visualization of frequency content</p>
  </div>
  <div class="feature-card">
    <h4>Dry/Wet Mix</h4>
    <p>Blend processed and original signals</p>
  </div>
</div>

## Quick Start

1. **Download** the plugin from the [Releases page](https://github.com/furmanlukasz/frequency-shifter/releases)
2. **Install** the VST3 or AU to your plugin folder
3. **Load** in your DAW and start experimenting!

### Basic Usage

| Parameter | Description |
|-----------|-------------|
| **Shift (Hz)** | Amount to shift frequencies. Positive = up, negative = down |
| **Quantize** | How strongly to snap to scale notes (0% = pure shift, 100% = fully quantized) |
| **Root Note** | The root of your scale (C, C#, D, etc.) |
| **Scale** | Choose from Major, Minor, Dorian, Pentatonic, Blues, and more |
| **Dry/Wet** | Mix between original and processed audio |

### Creative Tips

- **Metallic vocals**: Shift by 50-200 Hz with 0% quantization
- **Re-harmonize**: Use 100% quantization to force audio into a new scale
- **Subtle detuning**: Small shifts (5-20 Hz) with 50% quantization for chorus-like effects
- **Robotic sounds**: Large shifts with the Chromatic scale

## Downloads

Get the latest release for your platform:

| Platform | Format | Download |
|----------|--------|----------|
| macOS | VST3 | [Download](https://github.com/furmanlukasz/frequency-shifter/releases/latest) |
| macOS | AU | [Download](https://github.com/furmanlukasz/frequency-shifter/releases/latest) |
| Windows | VST3 | [Download](https://github.com/furmanlukasz/frequency-shifter/releases/latest) |

## How It Works

The plugin uses a sophisticated DSP pipeline:

<div class="mermaid">
flowchart TD
    A[🎵 Audio Input] --> B[STFT Analysis]
    B --> C[Frequency Shift]
    C --> D[Scale Quantization]
    D --> E[Phase Vocoder]
    E --> F[ISTFT Synthesis]
    F --> G[🔊 Audio Output]

    B -->|Magnitude & Phase| C
    C -->|Shifted Spectrum| D
    D -->|Quantized to Scale| E
    E -->|Phase Coherent| F

    style A fill:#7c3aed,stroke:#9333ea,color:#fff
    style G fill:#cd8b32,stroke:#b8860b,color:#fff
    style B fill:#3d2963,stroke:#5b4180,color:#f5f0e6
    style C fill:#3d2963,stroke:#5b4180,color:#f5f0e6
    style D fill:#3d2963,stroke:#5b4180,color:#f5f0e6
    style E fill:#3d2963,stroke:#5b4180,color:#f5f0e6
    style F fill:#3d2963,stroke:#5b4180,color:#f5f0e6
</div>

### The Algorithm

**1. STFT (Short-Time Fourier Transform)**: Break audio into overlapping frames, apply window function, transform to frequency domain.

**2. Frequency Shifting**: Reassign each frequency bin by adding the shift amount:

$$f_{\text{new}} = f_{\text{original}} + \Delta f$$

**3. Musical Quantization**: For each frequency, find the nearest note in the selected scale:

$$\text{MIDI} = 69 + 12 \cdot \log_2\left(\frac{f}{440}\right)$$

$$f_{\text{quantized}} = 440 \cdot 2^{\frac{\text{MIDI}_{\text{quantized}} - 69}{12}}$$

**4. Phase Vocoder**: Use identity phase locking (Laroche & Dolson) to maintain phase coherence between frames.

**5. ISTFT**: Overlap-add synthesis to reconstruct the time-domain signal.

For complete mathematical details, see [Algorithm Documentation](algorithm.html).

## Documentation

- [**User Guide**](user-guide.html) — Complete parameter reference and preset recipes
- [Algorithm Details](algorithm.html) — Full technical documentation
- [Phase Vocoder](phase-vocoder.html) — How phase coherence is maintained
- [Mathematical Foundation](math.html) — The DSP math behind the plugin

## System Requirements

- **macOS**: 10.13+ (Intel or Apple Silicon)
- **Windows**: Windows 10+
- **DAW**: Any VST3 or AU compatible host

## Source Code

This project is open source! Check out the [GitHub repository](https://github.com/furmanlukasz/frequency-shifter) to:

- Browse the source code
- Report issues
- Contribute improvements
- Build from source

<a href="https://github.com/furmanlukasz/frequency-shifter/releases" class="btn">Download Plugin</a>
<a href="https://github.com/furmanlukasz/frequency-shifter" class="btn btn-outline">View Source</a>

## Acknowledgments

Based on established DSP techniques:
- Phase vocoder (Laroche & Dolson, 1999)
- STFT overlap-add (Allen & Rabiner, 1977)
- Identity phase locking for improved quality

---

**Questions?** Open an issue on [GitHub](https://github.com/furmanlukasz/frequency-shifter/issues).
