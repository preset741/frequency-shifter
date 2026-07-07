#pragma once

#include <vector>
#include <utility>
#include <algorithm>
#include "Scales.h"

namespace fshift
{

/**
 * MusicalQuantizer - Musical scale quantization for frequency spectra.
 *
 * Takes a frequency spectrum and quantizes frequencies to the nearest
 * notes in a specified musical scale. Supports optional pitch drift
 * for organic, less-perfect quantization.
 *
 * Phase 2A improvements:
 * - Accumulation normalization: sqrt(N) normalization when multiple bins map to same target
 * - Total energy normalization: maintains constant energy before/after quantization
 * - Phase continuity: persistent phase accumulators for smooth synthesis
 *
 * Phase 2B improvements:
 * - Spectral envelope preservation: maintains timbral character after quantization
 * - Transient detection bypass: reduces quantization during attacks for punch
 *
 * Based on the Python implementation in harmonic_shifter/core/quantizer.py
 */
class MusicalQuantizer
{
public:
    /**
     * Construct musical quantizer.
     *
     * @param rootMidi MIDI note number for scale root (0-127)
     * @param scaleType Scale type from ScaleType enum
     */
    MusicalQuantizer(int rootMidi, ScaleType scaleType);

    ~MusicalQuantizer() = default;

    /**
     * Prepare the quantizer for processing.
     * Must be called before quantizeSpectrum.
     *
     * @param sampleRate Sample rate in Hz
     * @param fftSize FFT size
     * @param hopSize Hop size in samples
     */
    void prepare(double sampleRate, int fftSize, int hopSize);

    /**
     * Reset internal state (phase accumulators).
     */
    void reset();

    /**
     * Set the root note.
     *
     * @param rootMidi MIDI note number for scale root (0-127)
     */
    void setRootNote(int rootMidi);

    /**
     * Set the scale type.
     *
     * @param scaleType Scale type from ScaleType enum
     */
    void setScaleType(ScaleType scaleType);

    /**
     * Set active notes directly from 12 pitch-class booleans.
     * Replaces root note + scale type with custom note selection.
     * Root is implicitly C (MIDI 60).
     *
     * @param notes Array of 12 booleans (C, C#, D, ..., B)
     */
    void setActiveNotes(const std::array<bool, 12>& notes);

    /**
     * Quantize frequencies to nearest scale notes.
     *
     * @param frequencies Array of frequencies in Hz
     * @param strength Quantization strength (0.0 = no quantization, 1.0 = full)
     * @return Quantized frequencies in Hz
     */
    std::vector<float> quantizeFrequencies(const std::vector<float>& frequencies, float strength = 1.0f);

    /**
     * Quantize entire spectrum to scale with optional drift.
     *
     * Maps energy from each frequency bin to the nearest scale frequency,
     * optionally applying pitch drift (in cents) to add organic variation.
     *
     * Phase 2A improvements applied:
     * - Accumulation normalization (sqrt(N) for N contributors)
     * - Total energy normalization (maintains input energy)
     * - Phase continuity (uses persistent phase accumulators)
     *
     * Phase 2B improvements:
     * - Spectral envelope preservation (pass preShiftEnvelope for accurate timbre)
     * - Transient detection bypass
     *
     * @param magnitude Magnitude spectrum
     * @param phase Phase spectrum (used only for bypassed bins when strength < 1)
     * @param sampleRate Sample rate in Hz
     * @param fftSize FFT size
     * @param shiftHz Continuous Hz offset applied to each bin before scale snap.
     *                Sub-bin precision is preserved by the two-nearest weighted distribution,
     *                so this is the right hook for a continuous in-key frequency shifter.
     * @param strength Quantization strength (0-1)
     * @param driftCents Optional array of drift values per bin (in cents)
     * @param preShiftEnvelope Optional pre-captured envelope from INPUT before any processing
     * @param preEnvelopeMagnitudeOut Optional: if non-null, receives the post-quantize,
     *                energy-normalized magnitude BEFORE spectral-envelope preservation is
     *                applied. Callers feeding a feedback loop should recirculate THIS rather
     *                than the returned (envelope-boosted) magnitude, so the per-band envelope
     *                make-up gain does not compound around the loop.
     * @return Pair of (quantized_magnitude, quantized_phase)
     */
    std::pair<std::vector<float>, std::vector<float>> quantizeSpectrum(
        const std::vector<float>& magnitude,
        const std::vector<float>& phase,
        double sampleRate,
        int fftSize,
        float shiftHz,
        float strength = 1.0f,
        const std::vector<float>* driftCents = nullptr,
        const std::vector<float>* preShiftEnvelope = nullptr,
        std::vector<float>* preEnvelopeMagnitudeOut = nullptr);

    /**
     * Capture spectral envelope from magnitude spectrum.
     * Call this on the INPUT signal BEFORE shift/quantization.
     * Pass the result to quantizeSpectrum's preShiftEnvelope parameter.
     * OPTIMIZED: Uses pre-computed lookup tables when available.
     */
    std::vector<float> getSpectralEnvelope(
        const std::vector<float>& magnitude,
        double sampleRate,
        int fftSize) const
    {
        // Build lookup tables if needed
        buildEnvelopeLookupTables(sampleRate, fftSize);
        // Use fast method if tables are ready
        if (!bandBinRanges.empty())
            return captureSpectralEnvelopeFast(magnitude);
        // Fallback to original method
        return captureSpectralEnvelope(magnitude, sampleRate, fftSize);
    }

    /**
     * Get all scale frequencies in a given range.
     *
     * @param minFreq Minimum frequency in Hz
     * @param maxFreq Maximum frequency in Hz
     * @return Vector of frequencies in scale within range
     */
    std::vector<float> getScaleFrequencies(float minFreq = 20.0f, float maxFreq = 20000.0f) const;

    // Phase 2B: Envelope preservation and transient detection setters
    void setPreserveAmount(float amount) { preserveAmount = std::clamp(amount, 0.0f, 1.0f); }
    void setTransientAmount(float amount) { transientAmount = std::clamp(amount, 0.0f, 1.0f); }
    void setTransientSensitivity(float sensitivity) { transientSensitivity = std::clamp(sensitivity, 0.0f, 1.0f); }

    // Peak-region snapping + sines/noise split.
    //  - peakSnap: when true, only tonal peaks (and their main-lobe skirts) are scale-snapped,
    //    moved as rigid units; non-peak ("noise") bins pass through instead of being snapped.
    //  - noiseMix: 0 = drop the broadband residual (peaks only); 1 = full natural residual.
    //  - peakSensitivity: 0 = only the strongest peaks count as tonal; 1 = many peaks counted.
    void setPeakSnap(bool enabled) { peakSnapEnabled = enabled; }
    void setNoiseMix(float amount) { noiseMix = std::clamp(amount, 0.0f, 1.0f); }
    void setPeakSensitivity(float sensitivity) { peakSnapSensitivity = std::clamp(sensitivity, 0.0f, 1.0f); }

    // Getters
    int getRootMidi() const { return rootMidi; }
    ScaleType getScaleType() const { return scaleType; }
    const std::vector<int>& getScaleDegrees() const { return scaleDegrees; }

private:
    /**
     * Quantize a single frequency to the scale.
     */
    float quantizeFrequency(float frequency, float strength) const;

    /**
     * Apply cents drift to a frequency.
     * @param frequency Base frequency in Hz
     * @param cents Drift amount in cents (100 cents = 1 semitone)
     * @return Drifted frequency in Hz
     */
    static float applyDriftCents(float frequency, float cents);

    /**
     * Strategy A: Find the two nearest scale frequencies and their weights.
     * Distributes energy based on inverse distance weighting.
     *
     * @param frequency Input frequency in Hz
     * @param[out] lowerFreq Lower scale frequency
     * @param[out] upperFreq Upper scale frequency
     * @param[out] lowerWeight Weight for lower frequency (0-1)
     * @param[out] upperWeight Weight for upper frequency (0-1)
     */
    void findTwoNearestScaleFrequencies(float frequency,
                                         float& lowerFreq, float& upperFreq,
                                         float& lowerWeight, float& upperWeight) const;

    /**
     * Strategy C: Apply magnitude smoothing (3-tap moving average).
     * Kernel: [0.25, 0.5, 0.25]
     *
     * @param magnitude Input magnitude spectrum (modified in place)
     */
    static void applyMagnitudeSmoothing(std::vector<float>& magnitude);

    /**
     * Phase 2B.1: Capture spectral envelope at ~1/3 octave resolution.
     * Uses peak magnitude per band (not average) for better transient response.
     *
     * @param magnitude Magnitude spectrum
     * @param sampleRate Sample rate in Hz
     * @param fftSize FFT size
     * @return Envelope values per frequency band
     */
    std::vector<float> captureSpectralEnvelope(
        const std::vector<float>& magnitude,
        double sampleRate,
        int fftSize) const;

    /**
     * Phase 2B.1: Apply spectral envelope to magnitude spectrum.
     * Reimpose the original envelope shape on the quantized spectrum.
     *
     * @param magnitude Magnitude spectrum (modified in place)
     * @param originalEnvelope Envelope captured before quantization
     * @param sampleRate Sample rate in Hz
     * @param fftSize FFT size
     * @param preserveStrength How much to apply (0-1)
     */
    void applySpectralEnvelope(
        std::vector<float>& magnitude,
        const std::vector<float>& originalEnvelope,
        double sampleRate,
        int fftSize,
        float preserveStrength) const;

    /**
     * Phase 2B.2: Detect if current frame is a transient.
     * Compares total spectral energy to previous frame.
     *
     * @param magnitude Current magnitude spectrum
     * @return Transient reduction factor (0 = no transient, 1 = strong transient)
     */
    float detectTransient(const std::vector<float>& magnitude);

    int rootMidi;
    ScaleType scaleType;
    std::vector<int> scaleDegrees;

    // Phase 2A: Phase continuity state
    // Persistent phase accumulators indexed by MIDI note (0-127)
    // This allows consistent phase across different FFT sizes
    static constexpr int NUM_MIDI_NOTES = 128;
    std::array<float, NUM_MIDI_NOTES> midiPhaseAccumulators{};

    // Silent frame counter per MIDI note - tracks how long since significant energy
    // Resets phase accumulator after SILENCE_FRAMES_TO_RESET consecutive silent frames
    std::array<int, NUM_MIDI_NOTES> silentFrameCount{};
    static constexpr int SILENCE_FRAMES_TO_RESET = 4;  // halve the phase-accumulator ring-out after a note decays

    // Magnitude threshold for "active" note (linear, roughly -60dB)
    static constexpr float MAGNITUDE_THRESHOLD = 0.004f;  // ~-48dB: declare a decaying note silent sooner (was -60dB)

    // Cached parameters for phase increment calculation
    double cachedSampleRate = 0.0;
    int cachedHopSize = 0;

    // Flag to track if prepare() has been called
    bool prepared = false;

    // Phase 2B: Envelope preservation parameters
    float preserveAmount = 0.0f;  // 0.0 - 1.0

    // Phase 2B: Transient detection parameters
    float transientAmount = 0.0f;       // 0.0 - 1.0 (how much transients bypass quantization)
    float transientSensitivity = 0.5f;  // 0.0 - 1.0 (detection threshold)

    // Peak-region snapping + sines/noise split parameters
    bool  peakSnapEnabled = false;      // false = legacy per-bin snapping (unchanged behaviour)
    float noiseMix = 0.7f;              // 0.0 - 1.0 (broadband residual passthrough level)
    float peakSnapSensitivity = 0.5f;   // 0.0 - 1.0 (peak-detection threshold)

    // Peak-snap temporal state (keyed by MIDI note / scalar — safe with the single shared
    // quantizer instance, like the phase accumulators; NOT per-bin).
    std::array<float, NUM_MIDI_NOTES> midiNoteActivity{};  // existence hysteresis (sticky peaks)
    float peakPrevEnergy = 0.0f;                            // previous-frame input energy (attack detect)

    // Transient detection state
    float previousFrameEnergy = 0.0f;
    float transientRampValue = 0.0f;    // Current ramp-down value (1 = in transient, decays to 0)
    static constexpr int TRANSIENT_RAMP_FRAMES = 4;  // Frames to ramp quantization back up
    static constexpr float ENVELOPE_FLOOR = 1e-6f;   // Floor to avoid division by near-zero

    // Spectral envelope band parameters
    // Standard resolution: 48 bands at ~1/5 octave resolution
    static constexpr int NUM_ENVELOPE_BANDS = 48;

    // ========== CPU OPTIMIZATION: Pre-computed lookup tables ==========
    // These avoid expensive std::log() calls in the real-time audio path

    // Cached FFT parameters for lookup table validity
    mutable int cachedFftSizeForLookup = 0;
    mutable double cachedSampleRateForLookup = 0.0;

    // Pre-computed bin-to-band mapping (avoids nested loop with log calls)
    // Index = bin number, Value = closest envelope band index
    mutable std::vector<int> binToBandLookup;

    // Pre-computed band bin ranges for envelope capture
    // Each pair is (lowBin, highBin) for that band
    mutable std::vector<std::pair<int, int>> bandBinRanges;

    /**
     * Build lookup tables for current FFT size / sample rate.
     * Called once when parameters change, not every frame.
     */
    void buildEnvelopeLookupTables(double sampleRate, int fftSize) const;

    /**
     * Optimized envelope capture using pre-computed lookup tables.
     */
    std::vector<float> captureSpectralEnvelopeFast(
        const std::vector<float>& magnitude) const;

    /**
     * Optimized envelope application using pre-computed lookup tables.
     */
    void applySpectralEnvelopeFast(
        std::vector<float>& magnitude,
        const std::vector<float>& originalEnvelope,
        const std::vector<float>& postEnvelope,
        float preserveStrength) const;
};

} // namespace fshift
