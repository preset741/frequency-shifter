#pragma once

#include <vector>
#include <utility>
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
     * @param magnitude Magnitude spectrum
     * @param phase Phase spectrum (used only for bypassed bins when strength < 1)
     * @param sampleRate Sample rate in Hz
     * @param fftSize FFT size
     * @param strength Quantization strength (0-1)
     * @param driftCents Optional array of drift values per bin (in cents)
     * @return Pair of (quantized_magnitude, quantized_phase)
     */
    std::pair<std::vector<float>, std::vector<float>> quantizeSpectrum(
        const std::vector<float>& magnitude,
        const std::vector<float>& phase,
        double sampleRate,
        int fftSize,
        float strength = 1.0f,
        const std::vector<float>* driftCents = nullptr);

    /**
     * Get all scale frequencies in a given range.
     *
     * @param minFreq Minimum frequency in Hz
     * @param maxFreq Maximum frequency in Hz
     * @return Vector of frequencies in scale within range
     */
    std::vector<float> getScaleFrequencies(float minFreq = 20.0f, float maxFreq = 20000.0f) const;

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

    int rootMidi;
    ScaleType scaleType;
    std::vector<int> scaleDegrees;

    // Phase 2A: Phase continuity state
    // Persistent phase accumulators indexed by MIDI note (0-127)
    // This allows consistent phase across different FFT sizes
    static constexpr int NUM_MIDI_NOTES = 128;
    std::array<float, NUM_MIDI_NOTES> midiPhaseAccumulators{};

    // Cached parameters for phase increment calculation
    double cachedSampleRate = 0.0;
    int cachedHopSize = 0;

    // Flag to track if prepare() has been called
    bool prepared = false;
};

} // namespace fshift
