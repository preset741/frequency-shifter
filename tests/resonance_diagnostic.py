#!/usr/bin/env python3
"""
Resonance Diagnostic Tool for Frequency Shifter Plugin
======================================================

Generates spectrograms to identify resonance issues in the quantization system.

Test methodology:
1. Generate test signal (noise burst + silence)
2. Process through plugin at multiple SHIFT settings
3. Compare QUANTIZE 0% vs 100% for each shift
4. Analyze spectrograms for persistent horizontal lines (resonance)

Requirements:
    pip install numpy scipy matplotlib pedalboard

Usage:
    python resonance_diagnostic.py
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy import signal
from scipy.io import wavfile
import os
from pathlib import Path
from datetime import datetime

# Try to import pedalboard for VST3 processing
try:
    from pedalboard import Pedalboard, load_plugin
    from pedalboard.io import AudioFile
    HAS_PEDALBOARD = True
except ImportError:
    HAS_PEDALBOARD = False
    print("WARNING: pedalboard not installed. Will generate test signals only.")
    print("Install with: pip install pedalboard")

# Configuration
SAMPLE_RATE = 44100
NOISE_DURATION = 1.0  # seconds of noise
SILENCE_DURATION = 2.0  # seconds of silence after noise
TOTAL_DURATION = NOISE_DURATION + SILENCE_DURATION

# Test parameters
SHIFT_VALUES = [0, 400, 700, 1000, 1500]  # Hz
QUANTIZE_VALUES = [0, 100]  # percent
SMEAR_MS = 100  # Fixed smear setting

# Plugin path (macOS)
PLUGIN_PATH = os.path.expanduser(
    "~/Library/Audio/Plug-Ins/VST3/Frequency Shifter v45 DecayFix.vst3"
)

# Output directory
OUTPUT_DIR = Path(__file__).parent / "resonance_analysis"


def generate_test_signal(sample_rate=SAMPLE_RATE, noise_duration=NOISE_DURATION,
                         silence_duration=SILENCE_DURATION):
    """Generate white noise burst followed by silence."""
    noise_samples = int(noise_duration * sample_rate)
    silence_samples = int(silence_duration * sample_rate)

    # White noise with fade in/out to avoid clicks
    noise = np.random.randn(noise_samples).astype(np.float32)

    # Apply envelope to noise (100ms fade in, 100ms fade out)
    fade_samples = int(0.1 * sample_rate)
    fade_in = np.linspace(0, 1, fade_samples)
    fade_out = np.linspace(1, 0, fade_samples)

    noise[:fade_samples] *= fade_in
    noise[-fade_samples:] *= fade_out

    # Normalize
    noise = noise / np.max(np.abs(noise)) * 0.8

    # Add silence
    silence = np.zeros(silence_samples, dtype=np.float32)

    return np.concatenate([noise, silence])


def generate_impulse_signal(sample_rate=SAMPLE_RATE, silence_duration=SILENCE_DURATION):
    """Generate impulse followed by silence."""
    silence_samples = int(silence_duration * sample_rate)
    impulse_samples = int(0.01 * sample_rate)  # 10ms impulse

    # Create impulse (brief click)
    impulse = np.zeros(impulse_samples + silence_samples, dtype=np.float32)
    impulse[0] = 1.0
    impulse[1] = -0.5

    return impulse


def compute_spectrogram(audio, sample_rate=SAMPLE_RATE, nperseg=2048, noverlap=1920):
    """Compute spectrogram of audio signal."""
    f, t, Sxx = signal.spectrogram(
        audio,
        fs=sample_rate,
        nperseg=nperseg,
        noverlap=noverlap,
        window='hann',
        scaling='density'
    )
    # Convert to dB
    Sxx_db = 10 * np.log10(Sxx + 1e-10)
    return f, t, Sxx_db


def analyze_decay_at_frequencies(audio, target_freqs, sample_rate=SAMPLE_RATE,
                                  nperseg=2048, noise_end_time=NOISE_DURATION):
    """
    Analyze magnitude decay at specific frequencies after noise ends.
    Returns decay curves for each target frequency.
    """
    f, t, Sxx_db = compute_spectrogram(audio, sample_rate, nperseg)

    decay_curves = {}
    for freq in target_freqs:
        # Find closest frequency bin
        freq_idx = np.argmin(np.abs(f - freq))
        actual_freq = f[freq_idx]

        # Get magnitude over time at this frequency
        magnitude = Sxx_db[freq_idx, :]

        # Find time index where silence begins
        silence_start_idx = np.argmin(np.abs(t - noise_end_time))

        decay_curves[actual_freq] = {
            'time': t[silence_start_idx:] - noise_end_time,
            'magnitude': magnitude[silence_start_idx:],
            'target_freq': freq
        }

    return decay_curves


def process_with_plugin(audio, shift_hz, quantize_pct, smear_ms=SMEAR_MS):
    """Process audio through the plugin with given settings."""
    if not HAS_PEDALBOARD:
        return None

    if not os.path.exists(PLUGIN_PATH):
        print(f"Plugin not found at: {PLUGIN_PATH}")
        return None

    try:
        # Load plugin
        plugin = load_plugin(PLUGIN_PATH)

        # Set parameters (parameter names may need adjustment)
        # These are the parameter IDs from PluginProcessor.h
        plugin.shiftHz = shift_hz
        plugin.quantizeStrength = quantize_pct
        plugin.smear = smear_ms
        plugin.dryWet = 100.0  # Full wet
        plugin.phaseVocoder = True  # Enhanced mode on

        # Process audio (needs to be 2D for pedalboard)
        audio_2d = audio.reshape(1, -1)
        processed = plugin.process(audio_2d, SAMPLE_RATE)

        return processed.flatten()
    except Exception as e:
        print(f"Error processing with plugin: {e}")
        return None


def create_spectrogram_grid(results, output_path):
    """
    Create a grid of spectrograms.
    Rows = shift values, Columns = quantize 0% / 100%
    """
    n_shifts = len(SHIFT_VALUES)
    n_quantize = len(QUANTIZE_VALUES)

    fig, axes = plt.subplots(n_shifts, n_quantize + 1, figsize=(18, 4 * n_shifts))
    fig.suptitle('Resonance Diagnostic: Spectrograms\n(Look for horizontal lines persisting after 1s mark)',
                 fontsize=14, fontweight='bold')

    # Add column titles
    col_titles = ['Dry Signal'] + [f'Quantize {q}%' for q in QUANTIZE_VALUES]
    for ax, title in zip(axes[0], col_titles):
        ax.set_title(title, fontsize=12, fontweight='bold')

    for i, shift in enumerate(SHIFT_VALUES):
        # Row label
        axes[i, 0].set_ylabel(f'Shift {shift}Hz', fontsize=11, fontweight='bold')

        for j, (label, data) in enumerate(results[shift].items()):
            if data is None:
                axes[i, j].text(0.5, 0.5, 'No data', ha='center', va='center',
                               transform=axes[i, j].transAxes)
                continue

            f, t, Sxx_db = compute_spectrogram(data)

            # Plot spectrogram
            im = axes[i, j].pcolormesh(t, f, Sxx_db, shading='gouraud',
                                        cmap='magma', vmin=-80, vmax=0)

            # Mark the end of noise burst
            axes[i, j].axvline(x=NOISE_DURATION, color='cyan', linestyle='--',
                              linewidth=1, alpha=0.7)

            # Limit frequency range to useful region
            axes[i, j].set_ylim(0, 8000)

            if i == n_shifts - 1:
                axes[i, j].set_xlabel('Time (s)')
            if j == 0:
                axes[i, j].set_ylabel(f'Shift {shift}Hz\nFrequency (Hz)')

    # Add colorbar
    cbar = fig.colorbar(im, ax=axes, orientation='vertical', fraction=0.02, pad=0.04)
    cbar.set_label('Magnitude (dB)')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved spectrogram grid to: {output_path}")


def create_decay_analysis(results, output_path):
    """
    Analyze and plot decay curves at scale frequencies.
    Shows if energy persists after input stops.
    """
    # C Major scale frequencies (for analysis)
    c_major_freqs = [261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25]

    fig, axes = plt.subplots(len(SHIFT_VALUES), 2, figsize=(14, 3 * len(SHIFT_VALUES)))
    fig.suptitle('Decay Analysis at Scale Frequencies\n(Should decay to -80dB within ~500ms after input stops)',
                 fontsize=14, fontweight='bold')

    for i, shift in enumerate(SHIFT_VALUES):
        for j, quantize in enumerate(QUANTIZE_VALUES):
            key = f'quantize_{quantize}'
            data = results[shift].get(key)

            if data is None:
                axes[i, j].text(0.5, 0.5, 'No data', ha='center', va='center',
                               transform=axes[i, j].transAxes)
                continue

            # Analyze decay at shifted scale frequencies
            shifted_freqs = [f + shift for f in c_major_freqs if 20 < f + shift < 10000]
            decay_curves = analyze_decay_at_frequencies(data, shifted_freqs)

            for freq, curve in decay_curves.items():
                axes[i, j].plot(curve['time'], curve['magnitude'],
                               label=f'{freq:.0f}Hz', alpha=0.7)

            axes[i, j].axhline(y=-60, color='red', linestyle='--', alpha=0.5,
                              label='-60dB threshold')
            axes[i, j].axhline(y=-80, color='red', linestyle=':', alpha=0.5,
                              label='-80dB floor')

            axes[i, j].set_xlim(0, SILENCE_DURATION)
            axes[i, j].set_ylim(-100, 0)
            axes[i, j].set_title(f'Shift {shift}Hz, Quantize {quantize}%')
            axes[i, j].set_xlabel('Time after input stops (s)')
            axes[i, j].set_ylabel('Magnitude (dB)')
            axes[i, j].grid(True, alpha=0.3)

            if i == 0 and j == 0:
                axes[i, j].legend(loc='upper right', fontsize=8)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved decay analysis to: {output_path}")


def create_resonance_summary(results, output_path):
    """
    Create a summary showing resonance severity across settings.
    Measures energy remaining 1 second after input stops.
    """
    fig, ax = plt.subplots(figsize=(10, 6))

    resonance_matrix = np.zeros((len(SHIFT_VALUES), len(QUANTIZE_VALUES)))

    for i, shift in enumerate(SHIFT_VALUES):
        for j, quantize in enumerate(QUANTIZE_VALUES):
            key = f'quantize_{quantize}'
            data = results[shift].get(key)

            if data is None:
                resonance_matrix[i, j] = np.nan
                continue

            # Measure total energy 1-2 seconds after noise ends
            f, t, Sxx_db = compute_spectrogram(data)

            # Find time indices for analysis window (1-2s after noise ends)
            analysis_start = np.argmin(np.abs(t - (NOISE_DURATION + 1.0)))
            analysis_end = np.argmin(np.abs(t - (NOISE_DURATION + 2.0)))

            # Average energy in analysis window (0-4000 Hz range)
            freq_limit = np.argmin(np.abs(f - 4000))
            avg_energy = np.mean(Sxx_db[:freq_limit, analysis_start:analysis_end])

            resonance_matrix[i, j] = avg_energy

    # Plot heatmap
    im = ax.imshow(resonance_matrix, cmap='RdYlGn_r', aspect='auto',
                   vmin=-80, vmax=-30)

    ax.set_xticks(range(len(QUANTIZE_VALUES)))
    ax.set_xticklabels([f'{q}%' for q in QUANTIZE_VALUES])
    ax.set_yticks(range(len(SHIFT_VALUES)))
    ax.set_yticklabels([f'{s}Hz' for s in SHIFT_VALUES])

    ax.set_xlabel('Quantize Strength')
    ax.set_ylabel('Shift Amount')
    ax.set_title('Resonance Severity (Average dB 1-2s after input stops)\n'
                 'Green = Good (< -70dB), Yellow = Warning, Red = Resonance')

    # Add text annotations
    for i in range(len(SHIFT_VALUES)):
        for j in range(len(QUANTIZE_VALUES)):
            val = resonance_matrix[i, j]
            if not np.isnan(val):
                color = 'white' if val > -50 else 'black'
                ax.text(j, i, f'{val:.1f}dB', ha='center', va='center',
                       color=color, fontsize=10, fontweight='bold')

    cbar = plt.colorbar(im, ax=ax)
    cbar.set_label('Average Energy (dB)')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved resonance summary to: {output_path}")


def run_offline_analysis():
    """
    Run analysis using pre-generated test audio files.
    Useful when plugin processing isn't available.
    """
    print("\n" + "="*60)
    print("OFFLINE ANALYSIS MODE")
    print("="*60)
    print("\nGenerating test signal for manual plugin processing...")

    # Generate test signal
    test_signal = generate_test_signal()

    # Save test signal
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    test_file = OUTPUT_DIR / "test_signal_noise_burst.wav"
    wavfile.write(test_file, SAMPLE_RATE, test_signal)
    print(f"Saved test signal to: {test_file}")

    # Also save impulse version
    impulse_signal = generate_impulse_signal()
    impulse_file = OUTPUT_DIR / "test_signal_impulse.wav"
    wavfile.write(impulse_file, SAMPLE_RATE, impulse_signal)
    print(f"Saved impulse signal to: {impulse_file}")

    # Analyze dry signal
    print("\nAnalyzing dry signal characteristics...")
    f, t, Sxx_db = compute_spectrogram(test_signal)

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Spectrogram of dry signal
    im = axes[0].pcolormesh(t, f, Sxx_db, shading='gouraud', cmap='magma', vmin=-80, vmax=0)
    axes[0].axvline(x=NOISE_DURATION, color='cyan', linestyle='--', linewidth=2)
    axes[0].set_ylim(0, 8000)
    axes[0].set_xlabel('Time (s)')
    axes[0].set_ylabel('Frequency (Hz)')
    axes[0].set_title('Dry Test Signal (Noise Burst + Silence)')
    plt.colorbar(im, ax=axes[0], label='Magnitude (dB)')

    # Waveform
    time_axis = np.arange(len(test_signal)) / SAMPLE_RATE
    axes[1].plot(time_axis, test_signal)
    axes[1].axvline(x=NOISE_DURATION, color='red', linestyle='--', linewidth=2, label='Silence starts')
    axes[1].set_xlabel('Time (s)')
    axes[1].set_ylabel('Amplitude')
    axes[1].set_title('Waveform')
    axes[1].legend()

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "dry_signal_analysis.png", dpi=150)
    plt.close()

    print("\n" + "="*60)
    print("MANUAL PROCESSING INSTRUCTIONS")
    print("="*60)
    print(f"""
To complete the resonance analysis:

1. Open your DAW and load the test signal:
   {test_file}

2. Insert the Frequency Shifter plugin on the track

3. For each combination below, render/bounce to a new file:

   Settings to test (SMEAR fixed at {SMEAR_MS}ms):
""")

    for shift in SHIFT_VALUES:
        for quantize in QUANTIZE_VALUES:
            filename = f"processed_shift{shift}_quant{quantize}.wav"
            print(f"   - SHIFT={shift}Hz, QUANTIZE={quantize}% -> {filename}")

    print(f"""
4. Save all rendered files to:
   {OUTPUT_DIR}

5. Run this script again with --analyze flag to generate spectrograms:
   python resonance_diagnostic.py --analyze
""")


def analyze_rendered_files():
    """Analyze pre-rendered audio files."""
    print("\nAnalyzing rendered files...")

    results = {shift: {'dry': None} for shift in SHIFT_VALUES}

    # Load dry signal
    dry_file = OUTPUT_DIR / "test_signal_noise_burst.wav"
    if dry_file.exists():
        _, dry_signal = wavfile.read(dry_file)
        if dry_signal.dtype == np.int16:
            dry_signal = dry_signal.astype(np.float32) / 32768.0
        for shift in SHIFT_VALUES:
            results[shift]['dry'] = dry_signal

    # Load processed files
    for shift in SHIFT_VALUES:
        for quantize in QUANTIZE_VALUES:
            filename = f"processed_shift{shift}_quant{quantize}.wav"
            filepath = OUTPUT_DIR / filename

            if filepath.exists():
                _, audio = wavfile.read(filepath)
                if audio.dtype == np.int16:
                    audio = audio.astype(np.float32) / 32768.0
                # Handle stereo
                if len(audio.shape) > 1:
                    audio = audio[:, 0]  # Take left channel
                results[shift][f'quantize_{quantize}'] = audio
                print(f"  Loaded: {filename}")
            else:
                results[shift][f'quantize_{quantize}'] = None
                print(f"  Missing: {filename}")

    # Generate analysis outputs
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    create_spectrogram_grid(results, OUTPUT_DIR / f"spectrograms_{timestamp}.png")
    create_decay_analysis(results, OUTPUT_DIR / f"decay_analysis_{timestamp}.png")
    create_resonance_summary(results, OUTPUT_DIR / f"resonance_summary_{timestamp}.png")

    print("\nAnalysis complete!")


def run_full_analysis():
    """Run full automated analysis with plugin processing."""
    print("\n" + "="*60)
    print("AUTOMATED RESONANCE ANALYSIS")
    print("="*60)

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Generate test signal
    print("\nGenerating test signal...")
    test_signal = generate_test_signal()

    # Save dry signal
    dry_file = OUTPUT_DIR / "test_signal_noise_burst.wav"
    wavfile.write(dry_file, SAMPLE_RATE, test_signal)

    # Initialize results
    results = {shift: {'dry': test_signal} for shift in SHIFT_VALUES}

    # Process through plugin with each setting
    print("\nProcessing through plugin...")
    for shift in SHIFT_VALUES:
        for quantize in QUANTIZE_VALUES:
            print(f"  Processing: SHIFT={shift}Hz, QUANTIZE={quantize}%")
            processed = process_with_plugin(test_signal, shift, quantize)
            results[shift][f'quantize_{quantize}'] = processed

            # Save processed audio
            if processed is not None:
                filename = f"processed_shift{shift}_quant{quantize}.wav"
                wavfile.write(OUTPUT_DIR / filename, SAMPLE_RATE, processed)

    # Generate analysis outputs
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    print("\nGenerating spectrograms...")
    create_spectrogram_grid(results, OUTPUT_DIR / f"spectrograms_{timestamp}.png")

    print("Generating decay analysis...")
    create_decay_analysis(results, OUTPUT_DIR / f"decay_analysis_{timestamp}.png")

    print("Generating resonance summary...")
    create_resonance_summary(results, OUTPUT_DIR / f"resonance_summary_{timestamp}.png")

    print("\n" + "="*60)
    print("ANALYSIS COMPLETE")
    print("="*60)
    print(f"\nResults saved to: {OUTPUT_DIR}")


def main():
    import sys

    if '--analyze' in sys.argv:
        # Analyze pre-rendered files
        analyze_rendered_files()
    elif HAS_PEDALBOARD and os.path.exists(PLUGIN_PATH):
        # Run full automated analysis
        run_full_analysis()
    else:
        # Run offline mode (generate test signals only)
        run_offline_analysis()


if __name__ == "__main__":
    main()
