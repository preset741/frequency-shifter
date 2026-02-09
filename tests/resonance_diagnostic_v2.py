#!/usr/bin/env python3
"""
Resonance Diagnostic Tool v2 - Enhanced Edge Case Testing
==========================================================

Tests additional scenarios that might reveal resonance issues:
1. Sustained tones (not just noise)
2. Impulses/transients
3. Sweep signals
4. Various SMEAR settings
5. Different scales

Requirements:
    pip install numpy scipy matplotlib pedalboard
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy import signal
from scipy.io import wavfile
import os
from pathlib import Path
from datetime import datetime

try:
    from pedalboard import load_plugin
    HAS_PEDALBOARD = True
except ImportError:
    HAS_PEDALBOARD = False
    print("WARNING: pedalboard not installed")

SAMPLE_RATE = 44100
OUTPUT_DIR = Path(__file__).parent / "resonance_analysis_v2"

PLUGIN_PATH = os.path.expanduser(
    "~/Library/Audio/Plug-Ins/VST3/Frequency Shifter v47 PhaseBlend.vst3"
)


def generate_sine_burst(freq=440, duration=1.0, silence=2.0, sample_rate=SAMPLE_RATE):
    """Generate a sine wave burst followed by silence."""
    t_sound = np.arange(int(duration * sample_rate)) / sample_rate
    t_silence = np.zeros(int(silence * sample_rate))

    # Sine with envelope
    sine = np.sin(2 * np.pi * freq * t_sound).astype(np.float32)

    # Apply envelope
    fade_samples = int(0.05 * sample_rate)
    sine[:fade_samples] *= np.linspace(0, 1, fade_samples)
    sine[-fade_samples:] *= np.linspace(1, 0, fade_samples)

    return np.concatenate([sine * 0.8, t_silence.astype(np.float32)])


def generate_chord_burst(freqs=[261.63, 329.63, 392.00], duration=1.0, silence=2.0):
    """Generate a chord (multiple sines) followed by silence."""
    result = np.zeros(int((duration + silence) * SAMPLE_RATE), dtype=np.float32)
    for freq in freqs:
        result += generate_sine_burst(freq, duration, silence) / len(freqs)
    return result


def generate_impulse_train(num_impulses=5, interval=0.2, silence=2.0):
    """Generate a series of impulses followed by silence."""
    total_duration = num_impulses * interval + silence
    samples = int(total_duration * SAMPLE_RATE)
    audio = np.zeros(samples, dtype=np.float32)

    for i in range(num_impulses):
        idx = int(i * interval * SAMPLE_RATE)
        # Short click
        audio[idx:idx+100] = np.sin(np.linspace(0, 10*np.pi, 100)) * 0.8

    return audio


def generate_sweep(start_freq=100, end_freq=2000, duration=1.0, silence=2.0):
    """Generate a frequency sweep followed by silence."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE
    # Logarithmic sweep
    sweep = signal.chirp(t, start_freq, duration, end_freq, method='logarithmic')
    sweep = (sweep * 0.8).astype(np.float32)

    # Fade out
    fade = int(0.1 * SAMPLE_RATE)
    sweep[-fade:] *= np.linspace(1, 0, fade)

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([sweep, silence_samples])


def process_audio(audio, shift_hz, quantize_pct, smear_ms=100, enhanced=True):
    """Process audio through the plugin."""
    if not HAS_PEDALBOARD or not os.path.exists(PLUGIN_PATH):
        return None

    try:
        plugin = load_plugin(PLUGIN_PATH)
        plugin.shiftHz = float(shift_hz)
        plugin.quantizeStrength = float(quantize_pct)
        plugin.smear = float(smear_ms)
        plugin.dryWet = 100.0
        plugin.phaseVocoder = enhanced

        audio_2d = audio.reshape(1, -1)
        processed = plugin.process(audio_2d, SAMPLE_RATE)
        return processed.flatten()
    except Exception as e:
        print(f"Error: {e}")
        return None


def compute_spectrogram(audio, nperseg=2048, noverlap=1920):
    """Compute spectrogram."""
    f, t, Sxx = signal.spectrogram(
        audio, fs=SAMPLE_RATE, nperseg=nperseg, noverlap=noverlap,
        window='hann', scaling='density'
    )
    return f, t, 10 * np.log10(Sxx + 1e-10)


def measure_residual_energy(audio, start_time, end_time, freq_range=(20, 8000)):
    """Measure average energy in a time window."""
    f, t, Sxx_db = compute_spectrogram(audio)

    t_start_idx = np.argmin(np.abs(t - start_time))
    t_end_idx = np.argmin(np.abs(t - end_time))
    f_start_idx = np.argmin(np.abs(f - freq_range[0]))
    f_end_idx = np.argmin(np.abs(f - freq_range[1]))

    return np.mean(Sxx_db[f_start_idx:f_end_idx, t_start_idx:t_end_idx])


def run_edge_case_tests():
    """Run comprehensive edge case tests."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print("\n" + "="*60)
    print("EDGE CASE RESONANCE TESTS")
    print("="*60)

    # Test signals
    test_signals = {
        'sine_440': generate_sine_burst(440, 1.0, 2.0),
        'sine_1000': generate_sine_burst(1000, 1.0, 2.0),
        'chord_cmaj': generate_chord_burst([261.63, 329.63, 392.00], 1.0, 2.0),
        'impulses': generate_impulse_train(5, 0.2, 2.0),
        'sweep': generate_sweep(100, 2000, 1.0, 2.0),
    }

    # Test configurations
    configs = [
        {'shift': 0, 'quantize': 100, 'smear': 100, 'enhanced': True},
        {'shift': 500, 'quantize': 100, 'smear': 100, 'enhanced': True},
        {'shift': 500, 'quantize': 100, 'smear': 50, 'enhanced': True},
        {'shift': 500, 'quantize': 100, 'smear': 100, 'enhanced': False},
        {'shift': 1000, 'quantize': 100, 'smear': 100, 'enhanced': True},
        {'shift': 500, 'quantize': 50, 'smear': 100, 'enhanced': True},
    ]

    results = {}

    for sig_name, sig_audio in test_signals.items():
        print(f"\nTesting: {sig_name}")
        results[sig_name] = {}

        # Save dry signal
        wavfile.write(OUTPUT_DIR / f"dry_{sig_name}.wav", SAMPLE_RATE, sig_audio)

        for cfg in configs:
            cfg_name = f"s{cfg['shift']}_q{cfg['quantize']}_sm{cfg['smear']}_e{int(cfg['enhanced'])}"
            print(f"  Config: {cfg_name}")

            processed = process_audio(
                sig_audio,
                cfg['shift'],
                cfg['quantize'],
                cfg['smear'],
                cfg['enhanced']
            )

            if processed is not None:
                # Save processed
                wavfile.write(
                    OUTPUT_DIR / f"proc_{sig_name}_{cfg_name}.wav",
                    SAMPLE_RATE, processed
                )

                # Measure residual energy 1-2s after sound ends
                # Sound ends at ~1s for most signals
                residual = measure_residual_energy(processed, 2.0, 3.0)
                results[sig_name][cfg_name] = {
                    'audio': processed,
                    'residual_db': residual,
                    'config': cfg
                }
                print(f"    Residual energy (1-2s after): {residual:.1f} dB")

    # Generate comparison plot
    create_comparison_plot(test_signals, results, configs)

    # Generate detailed spectrograms for worst cases
    find_and_plot_worst_cases(results)

    return results


def create_comparison_plot(test_signals, results, configs):
    """Create a comparison heatmap of residual energy."""
    sig_names = list(test_signals.keys())
    cfg_names = [f"s{c['shift']}_q{c['quantize']}" for c in configs]

    # Build matrix
    matrix = np.zeros((len(sig_names), len(configs)))
    for i, sig_name in enumerate(sig_names):
        for j, cfg in enumerate(configs):
            cfg_name = f"s{cfg['shift']}_q{cfg['quantize']}_sm{cfg['smear']}_e{int(cfg['enhanced'])}"
            if cfg_name in results.get(sig_name, {}):
                matrix[i, j] = results[sig_name][cfg_name]['residual_db']
            else:
                matrix[i, j] = np.nan

    fig, ax = plt.subplots(figsize=(14, 8))
    im = ax.imshow(matrix, cmap='RdYlGn_r', aspect='auto', vmin=-100, vmax=-30)

    ax.set_xticks(range(len(configs)))
    cfg_labels = [f"S:{c['shift']}\nQ:{c['quantize']}\nSm:{c['smear']}\nE:{c['enhanced']}"
                  for c in configs]
    ax.set_xticklabels(cfg_labels, fontsize=8)
    ax.set_yticks(range(len(sig_names)))
    ax.set_yticklabels(sig_names)

    ax.set_xlabel('Configuration (S=Shift, Q=Quantize, Sm=Smear, E=Enhanced)')
    ax.set_ylabel('Test Signal')
    ax.set_title('Residual Energy 1-2s After Input Stops (dB)\n'
                 'Green = Good (<-70dB), Yellow = Warning, Red = Resonance')

    # Add text annotations
    for i in range(len(sig_names)):
        for j in range(len(configs)):
            val = matrix[i, j]
            if not np.isnan(val):
                color = 'white' if val > -50 else 'black'
                ax.text(j, i, f'{val:.0f}', ha='center', va='center',
                       color=color, fontsize=9)

    plt.colorbar(im, ax=ax, label='Residual Energy (dB)')
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "edge_case_comparison.png", dpi=150)
    plt.close()
    print(f"\nSaved comparison to: {OUTPUT_DIR / 'edge_case_comparison.png'}")


def find_and_plot_worst_cases(results):
    """Find and plot detailed spectrograms for worst cases."""
    # Find cases with highest residual energy
    worst_cases = []
    for sig_name, configs in results.items():
        for cfg_name, data in configs.items():
            worst_cases.append((data['residual_db'], sig_name, cfg_name, data))

    worst_cases.sort(reverse=True)

    # Plot top 6 worst cases
    fig, axes = plt.subplots(3, 2, figsize=(14, 12))
    fig.suptitle('Detailed Spectrograms - Highest Residual Energy Cases\n'
                 '(Cyan line = expected silence start)', fontsize=12)

    for idx, (residual, sig_name, cfg_name, data) in enumerate(worst_cases[:6]):
        row, col = idx // 2, idx % 2
        ax = axes[row, col]

        f, t, Sxx_db = compute_spectrogram(data['audio'])
        im = ax.pcolormesh(t, f, Sxx_db, shading='gouraud', cmap='magma', vmin=-80, vmax=0)

        # Mark silence start (varies by signal type)
        ax.axvline(x=1.0, color='cyan', linestyle='--', linewidth=1, alpha=0.7)

        ax.set_ylim(0, 4000)
        ax.set_title(f'{sig_name} | {cfg_name}\nResidual: {residual:.1f}dB', fontsize=9)
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Freq (Hz)')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "worst_case_spectrograms.png", dpi=150)
    plt.close()
    print(f"Saved worst cases to: {OUTPUT_DIR / 'worst_case_spectrograms.png'}")


def analyze_frequency_bands():
    """Analyze which frequency bands have resonance issues."""
    print("\n" + "="*60)
    print("FREQUENCY BAND ANALYSIS")
    print("="*60)

    # Generate broadband noise
    noise = np.random.randn(int(1.0 * SAMPLE_RATE)).astype(np.float32)
    noise = noise / np.max(np.abs(noise)) * 0.8
    silence = np.zeros(int(2.0 * SAMPLE_RATE), dtype=np.float32)
    test_signal = np.concatenate([noise, silence])

    # Process with problematic settings
    processed = process_audio(test_signal, 500, 100, 100, True)

    if processed is None:
        print("Could not process audio")
        return

    # Analyze energy in different bands during silence period
    f, t, Sxx_db = compute_spectrogram(processed)

    # Time window: 1.5-2.5s (during silence)
    t_start = np.argmin(np.abs(t - 1.5))
    t_end = np.argmin(np.abs(t - 2.5))

    bands = [
        (20, 100, 'Sub-bass'),
        (100, 250, 'Bass'),
        (250, 500, 'Low-mid'),
        (500, 1000, 'Mid'),
        (1000, 2000, 'Upper-mid'),
        (2000, 4000, 'Presence'),
        (4000, 8000, 'Brilliance'),
    ]

    print("\nResidual energy by frequency band (during silence):")
    print("-" * 50)

    fig, ax = plt.subplots(figsize=(10, 6))
    band_names = []
    band_energies = []

    for f_low, f_high, name in bands:
        f_low_idx = np.argmin(np.abs(f - f_low))
        f_high_idx = np.argmin(np.abs(f - f_high))

        avg_energy = np.mean(Sxx_db[f_low_idx:f_high_idx, t_start:t_end])
        max_energy = np.max(Sxx_db[f_low_idx:f_high_idx, t_start:t_end])

        print(f"{name:15s} ({f_low:4d}-{f_high:4d}Hz): avg={avg_energy:.1f}dB, max={max_energy:.1f}dB")

        band_names.append(f"{name}\n{f_low}-{f_high}Hz")
        band_energies.append(avg_energy)

    # Plot
    colors = ['green' if e < -70 else 'yellow' if e < -50 else 'red' for e in band_energies]
    ax.bar(band_names, band_energies, color=colors)
    ax.axhline(y=-70, color='green', linestyle='--', label='Good threshold (-70dB)')
    ax.axhline(y=-50, color='orange', linestyle='--', label='Warning threshold (-50dB)')
    ax.set_ylabel('Average Energy (dB)')
    ax.set_title('Residual Energy by Frequency Band\n(SHIFT=500Hz, QUANTIZE=100%, during silence period)')
    ax.legend()
    ax.set_ylim(-100, 0)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "frequency_band_analysis.png", dpi=150)
    plt.close()
    print(f"\nSaved band analysis to: {OUTPUT_DIR / 'frequency_band_analysis.png'}")


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    if not HAS_PEDALBOARD:
        print("pedalboard required for this analysis")
        return

    if not os.path.exists(PLUGIN_PATH):
        print(f"Plugin not found: {PLUGIN_PATH}")
        return

    # Run tests
    results = run_edge_case_tests()
    analyze_frequency_bands()

    print("\n" + "="*60)
    print("ANALYSIS COMPLETE")
    print("="*60)
    print(f"\nResults saved to: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
