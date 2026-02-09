#!/usr/bin/env python3
"""
Resonance Diagnostic Tool v3 - Real-World Signal Testing
=========================================================

Tests with more realistic musical signals to identify resonance issues.
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
OUTPUT_DIR = Path(__file__).parent / "resonance_analysis_v3"

PLUGIN_PATH = os.path.expanduser(
    "~/Library/Audio/Plug-Ins/VST3/Frequency Shifter v47 PhaseBlend.vst3"
)


def generate_piano_like(freq=440, duration=1.0, silence=2.0):
    """Generate a piano-like tone with natural decay."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE

    # Harmonics with decreasing amplitude
    harmonics = [1, 0.5, 0.25, 0.125, 0.0625, 0.03]
    tone = np.zeros_like(t)
    for i, amp in enumerate(harmonics):
        tone += amp * np.sin(2 * np.pi * freq * (i + 1) * t)

    # Natural decay envelope
    decay = np.exp(-3 * t)
    tone = (tone * decay * 0.6).astype(np.float32)

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([tone, silence_samples])


def generate_vocal_like(freq=300, duration=1.5, silence=2.0):
    """Generate a vocal-like formant signal."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE

    # Fundamental + formants
    signal_out = np.sin(2 * np.pi * freq * t)  # fundamental

    # Formant frequencies typical for vowel 'ah'
    formants = [700, 1200, 2500]
    for f in formants:
        signal_out += 0.3 * np.sin(2 * np.pi * f * t) * np.exp(-0.5 * t)

    # Add vibrato
    vibrato = 1 + 0.02 * np.sin(2 * np.pi * 5 * t)
    signal_out *= vibrato

    # Envelope
    env = np.ones_like(t)
    fade_out = int(0.3 * SAMPLE_RATE)
    env[-fade_out:] = np.linspace(1, 0, fade_out)

    signal_out = (signal_out * env * 0.5).astype(np.float32)
    signal_out = signal_out / np.max(np.abs(signal_out)) * 0.7

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([signal_out, silence_samples])


def generate_drum_hit():
    """Generate a drum-like transient."""
    duration = 0.3
    silence = 2.5
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE

    # Low frequency thump
    thump = np.sin(2 * np.pi * 60 * t) * np.exp(-20 * t)

    # High frequency click
    click = np.random.randn(int(0.01 * SAMPLE_RATE)) * 0.5
    click = np.concatenate([click, np.zeros(len(t) - len(click))])
    click *= np.exp(-50 * t[:len(click)] if len(click) <= len(t) else np.exp(-50 * t))

    drum = (thump + click[:len(thump)]).astype(np.float32)
    drum = drum / np.max(np.abs(drum)) * 0.8

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([drum, silence_samples])


def generate_pad_swell(duration=2.0, silence=2.0):
    """Generate a synth pad with slow attack and release."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE

    # Multiple detuned oscillators
    freqs = [220, 220.5, 221, 329.6, 440]
    signal_out = np.zeros_like(t)
    for f in freqs:
        signal_out += np.sin(2 * np.pi * f * t + np.random.random() * 2 * np.pi)

    # Slow attack/release envelope
    attack = int(0.5 * SAMPLE_RATE)
    release = int(0.5 * SAMPLE_RATE)
    env = np.ones_like(t)
    env[:attack] = np.linspace(0, 1, attack)
    env[-release:] = np.linspace(1, 0, release)

    signal_out = (signal_out * env * 0.3).astype(np.float32)
    signal_out = signal_out / np.max(np.abs(signal_out)) * 0.7

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([signal_out, silence_samples])


def generate_pluck(freq=330, duration=0.8, silence=2.0):
    """Generate a plucked string sound."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE

    # Karplus-Strong-like synthesis
    harmonics = np.zeros_like(t)
    for n in range(1, 15):
        # Higher harmonics decay faster
        decay_rate = 5 + n * 2
        harmonics += (1/n) * np.sin(2 * np.pi * freq * n * t) * np.exp(-decay_rate * t)

    # Initial brightness
    noise_burst = np.random.randn(int(0.005 * SAMPLE_RATE)) * 0.3
    noise_burst = np.concatenate([noise_burst, np.zeros(len(t) - len(noise_burst))])

    pluck = (harmonics + noise_burst[:len(harmonics)]).astype(np.float32)
    pluck = pluck / np.max(np.abs(pluck)) * 0.7

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([pluck, silence_samples])


def generate_continuous_then_stop(freq=440, duration=3.0, silence=2.0):
    """Generate a continuous tone that abruptly stops - tests decay behavior."""
    t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE

    # Rich harmonic content
    tone = np.zeros_like(t)
    for n in range(1, 8):
        tone += (1/n) * np.sin(2 * np.pi * freq * n * t)

    # Abrupt stop (small fade to avoid click)
    fade = int(0.01 * SAMPLE_RATE)
    tone[-fade:] *= np.linspace(1, 0, fade)

    tone = (tone * 0.5).astype(np.float32)
    tone = tone / np.max(np.abs(tone)) * 0.7

    silence_samples = np.zeros(int(silence * SAMPLE_RATE), dtype=np.float32)
    return np.concatenate([tone, silence_samples])


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

    region = Sxx_db[f_start_idx:f_end_idx, t_start_idx:t_end_idx]
    return np.mean(region), np.max(region)


def find_resonant_frequencies(audio, silence_start, analysis_duration=1.0):
    """Find frequencies that persist after input stops."""
    f, t, Sxx_db = compute_spectrogram(audio)

    # Find time indices for silence period
    t_start = np.argmin(np.abs(t - silence_start))
    t_end = np.argmin(np.abs(t - (silence_start + analysis_duration)))

    # Average energy per frequency during silence
    avg_energy = np.mean(Sxx_db[:, t_start:t_end], axis=1)

    # Find peaks above threshold
    threshold = -60  # dB
    resonant_freqs = []
    for i, (freq, energy) in enumerate(zip(f, avg_energy)):
        if energy > threshold and freq > 20:
            resonant_freqs.append((freq, energy))

    return resonant_freqs, f, avg_energy


def run_tests():
    """Run comprehensive tests with musical signals."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print("\n" + "="*60)
    print("REAL-WORLD SIGNAL RESONANCE TESTS")
    print("="*60)

    # Test signals with their silence start times
    test_signals = {
        'piano_A4': (generate_piano_like(440, 1.0, 2.0), 1.0),
        'piano_C4': (generate_piano_like(261.63, 1.0, 2.0), 1.0),
        'vocal_like': (generate_vocal_like(300, 1.5, 2.0), 1.5),
        'drum_hit': (generate_drum_hit(), 0.3),
        'pad_swell': (generate_pad_swell(2.0, 2.0), 2.0),
        'pluck_E4': (generate_pluck(330, 0.8, 2.0), 0.8),
        'continuous_stop': (generate_continuous_then_stop(440, 3.0, 2.0), 3.0),
    }

    # Test configurations - focus on problematic settings
    configs = [
        {'shift': 0, 'quantize': 0, 'label': 'Bypass'},
        {'shift': 0, 'quantize': 100, 'label': 'Quant only'},
        {'shift': 500, 'quantize': 0, 'label': 'Shift only'},
        {'shift': 500, 'quantize': 100, 'label': 'Shift+Quant'},
        {'shift': 1000, 'quantize': 100, 'label': 'High shift+Quant'},
    ]

    results = {}
    all_resonances = []

    for sig_name, (sig_audio, silence_start) in test_signals.items():
        print(f"\n{'='*40}")
        print(f"Testing: {sig_name}")
        print(f"{'='*40}")
        results[sig_name] = {}

        # Save dry signal
        wavfile.write(OUTPUT_DIR / f"dry_{sig_name}.wav", SAMPLE_RATE, sig_audio)

        for cfg in configs:
            print(f"\n  Config: {cfg['label']} (shift={cfg['shift']}, quant={cfg['quantize']})")

            processed = process_audio(
                sig_audio,
                cfg['shift'],
                cfg['quantize'],
                smear_ms=100,
                enhanced=True
            )

            if processed is not None:
                # Save processed
                filename = f"proc_{sig_name}_s{cfg['shift']}_q{cfg['quantize']}.wav"
                wavfile.write(OUTPUT_DIR / filename, SAMPLE_RATE, processed)

                # Measure residual energy during silence
                avg_res, max_res = measure_residual_energy(
                    processed,
                    silence_start + 0.5,  # Start measuring 0.5s after silence begins
                    silence_start + 1.5   # Measure for 1 second
                )

                # Find resonant frequencies
                resonant_freqs, freqs, avg_spectrum = find_resonant_frequencies(
                    processed, silence_start
                )

                results[sig_name][cfg['label']] = {
                    'audio': processed,
                    'avg_residual_db': avg_res,
                    'max_residual_db': max_res,
                    'resonant_freqs': resonant_freqs,
                    'silence_start': silence_start,
                    'config': cfg
                }

                print(f"    Avg residual: {avg_res:.1f} dB, Max: {max_res:.1f} dB")

                if resonant_freqs:
                    print(f"    Resonant frequencies found:")
                    for freq, energy in resonant_freqs[:5]:  # Show top 5
                        print(f"      {freq:.1f} Hz: {energy:.1f} dB")
                        all_resonances.append({
                            'signal': sig_name,
                            'config': cfg['label'],
                            'freq': freq,
                            'energy': energy
                        })
                else:
                    print(f"    No resonant frequencies above -60dB")

    # Create visualizations
    create_summary_heatmap(results, configs)
    create_spectrogram_comparison(results, test_signals)
    create_resonance_frequency_plot(all_resonances)

    return results


def create_summary_heatmap(results, configs):
    """Create summary heatmap of residual energy."""
    sig_names = list(results.keys())
    cfg_labels = [c['label'] for c in configs]

    # Build matrix
    matrix = np.zeros((len(sig_names), len(configs)))
    for i, sig_name in enumerate(sig_names):
        for j, cfg in enumerate(configs):
            if cfg['label'] in results.get(sig_name, {}):
                matrix[i, j] = results[sig_name][cfg['label']]['avg_residual_db']
            else:
                matrix[i, j] = np.nan

    fig, ax = plt.subplots(figsize=(12, 8))
    im = ax.imshow(matrix, cmap='RdYlGn_r', aspect='auto', vmin=-100, vmax=-30)

    ax.set_xticks(range(len(configs)))
    ax.set_xticklabels(cfg_labels, fontsize=10)
    ax.set_yticks(range(len(sig_names)))
    ax.set_yticklabels(sig_names, fontsize=10)

    ax.set_xlabel('Configuration', fontsize=12)
    ax.set_ylabel('Test Signal', fontsize=12)
    ax.set_title('Residual Energy During Silence Period (dB)\n'
                 'Green = Good (<-70dB), Yellow = Warning, Red = Resonance Problem',
                 fontsize=12)

    # Add text annotations
    for i in range(len(sig_names)):
        for j in range(len(configs)):
            val = matrix[i, j]
            if not np.isnan(val):
                color = 'white' if val > -50 else 'black'
                ax.text(j, i, f'{val:.0f}', ha='center', va='center',
                       color=color, fontsize=10, fontweight='bold')

    plt.colorbar(im, ax=ax, label='Residual Energy (dB)')
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "summary_heatmap.png", dpi=150)
    plt.close()
    print(f"\nSaved summary heatmap")


def create_spectrogram_comparison(results, test_signals):
    """Create before/after spectrogram comparisons for key cases."""
    # Focus on continuous_stop signal which best shows decay behavior
    sig_name = 'continuous_stop'
    sig_audio, silence_start = test_signals[sig_name]

    fig, axes = plt.subplots(2, 3, figsize=(18, 10))
    fig.suptitle(f'Spectrogram Comparison: {sig_name}\n'
                 f'(Vertical line = silence start at {silence_start}s)',
                 fontsize=14)

    # Dry signal
    f, t, Sxx_db = compute_spectrogram(sig_audio)
    im = axes[0, 0].pcolormesh(t, f, Sxx_db, shading='gouraud', cmap='magma', vmin=-80, vmax=0)
    axes[0, 0].axvline(x=silence_start, color='cyan', linestyle='--', linewidth=2)
    axes[0, 0].set_ylim(0, 4000)
    axes[0, 0].set_title('Dry Signal')
    axes[0, 0].set_ylabel('Frequency (Hz)')

    # Processed versions
    plot_configs = ['Bypass', 'Quant only', 'Shift only', 'Shift+Quant', 'High shift+Quant']
    plot_positions = [(0, 1), (0, 2), (1, 0), (1, 1), (1, 2)]

    for cfg_label, (row, col) in zip(plot_configs, plot_positions):
        if cfg_label in results.get(sig_name, {}):
            audio = results[sig_name][cfg_label]['audio']
            f, t, Sxx_db = compute_spectrogram(audio)

            im = axes[row, col].pcolormesh(t, f, Sxx_db, shading='gouraud',
                                            cmap='magma', vmin=-80, vmax=0)
            axes[row, col].axvline(x=silence_start, color='cyan', linestyle='--', linewidth=2)
            axes[row, col].set_ylim(0, 4000)

            res = results[sig_name][cfg_label]['avg_residual_db']
            axes[row, col].set_title(f'{cfg_label}\nResidual: {res:.0f}dB')

            if col == 0:
                axes[row, col].set_ylabel('Frequency (Hz)')
            if row == 1:
                axes[row, col].set_xlabel('Time (s)')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "spectrogram_comparison.png", dpi=150)
    plt.close()
    print("Saved spectrogram comparison")


def create_resonance_frequency_plot(all_resonances):
    """Plot histogram of resonant frequencies found."""
    if not all_resonances:
        print("No resonant frequencies to plot")
        return

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Histogram of resonant frequencies
    freqs = [r['freq'] for r in all_resonances]
    axes[0].hist(freqs, bins=50, range=(0, 4000), edgecolor='black')
    axes[0].set_xlabel('Frequency (Hz)')
    axes[0].set_ylabel('Count')
    axes[0].set_title('Distribution of Resonant Frequencies\n(frequencies with >-60dB during silence)')

    # Scatter: frequency vs energy, colored by config
    configs = list(set(r['config'] for r in all_resonances))
    colors = plt.cm.tab10(np.linspace(0, 1, len(configs)))
    config_colors = {c: colors[i] for i, c in enumerate(configs)}

    for r in all_resonances:
        axes[1].scatter(r['freq'], r['energy'],
                       c=[config_colors[r['config']]],
                       label=r['config'], alpha=0.6)

    # Remove duplicate legend entries
    handles, labels = axes[1].get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    axes[1].legend(by_label.values(), by_label.keys())

    axes[1].axhline(y=-60, color='red', linestyle='--', label='Threshold')
    axes[1].set_xlabel('Frequency (Hz)')
    axes[1].set_ylabel('Energy (dB)')
    axes[1].set_title('Resonant Frequency Energy by Configuration')
    axes[1].set_xlim(0, 4000)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "resonance_frequencies.png", dpi=150)
    plt.close()
    print("Saved resonance frequency plot")


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    if not HAS_PEDALBOARD:
        print("pedalboard required for this analysis")
        return

    if not os.path.exists(PLUGIN_PATH):
        print(f"Plugin not found: {PLUGIN_PATH}")
        return

    results = run_tests()

    print("\n" + "="*60)
    print("ANALYSIS COMPLETE")
    print("="*60)
    print(f"\nResults saved to: {OUTPUT_DIR}")

    # Summary
    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)

    worst_cases = []
    for sig_name, configs in results.items():
        for cfg_label, data in configs.items():
            if data['avg_residual_db'] > -70:
                worst_cases.append({
                    'signal': sig_name,
                    'config': cfg_label,
                    'residual': data['avg_residual_db']
                })

    if worst_cases:
        print("\nPotential resonance issues found (>-70dB residual):")
        for case in sorted(worst_cases, key=lambda x: x['residual'], reverse=True):
            print(f"  {case['signal']} + {case['config']}: {case['residual']:.1f}dB")
    else:
        print("\nNo significant resonance issues detected (all <-70dB)")


if __name__ == "__main__":
    main()
