#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace fshift
{

/**
 * SpectralMask - Frequency-selective processing mask.
 *
 * Creates a smooth mask curve that determines how much of the wet (processed)
 * signal vs dry (original) signal is used for each frequency bin.
 *
 * Supports shelf filters (low-pass, high-pass, band-pass) with smooth
 * Hermite transitions measured in octaves.
 */
class SpectralMask
{
public:
    enum class Mode
    {
        LowPass = 0,   // Affect frequencies below cutoff
        HighPass = 1,  // Affect frequencies above cutoff
        BandPass = 2   // Affect frequencies between low and high cutoff
    };

    SpectralMask() = default;
    ~SpectralMask() = default;

    /**
     * Set mask mode.
     * @param mode LowPass, HighPass, or BandPass
     */
    void setMode(Mode newMode) { mode = newMode; }
    Mode getMode() const { return mode; }

    /**
     * Set low cutoff frequency (used by LowPass and BandPass).
     * @param freq Frequency in Hz
     */
    void setLowFreq(float freq) { lowFreq = std::max(20.0f, freq); }
    float getLowFreq() const { return lowFreq; }

    /**
     * Set high cutoff frequency (used by HighPass and BandPass).
     * @param freq Frequency in Hz
     */
    void setHighFreq(float freq) { highFreq = std::max(20.0f, freq); }
    float getHighFreq() const { return highFreq; }

    /**
     * Set transition width in octaves.
     * Controls how gradually the mask transitions from 0 to 1.
     * @param octaves Transition width (0.1 = sharp, 2.0 = very gradual)
     */
    void setTransition(float octaves) { transition = std::clamp(octaves, 0.05f, 4.0f); }
    float getTransition() const { return transition; }

    /**
     * Get mask value at a specific frequency.
     * @param frequencyHz Frequency in Hz
     * @return Mask value 0-1 (0 = dry/bypass, 1 = fully processed)
     */
    float getMaskAt(float frequencyHz) const
    {
        if (frequencyHz <= 0.0f)
            return 0.0f;

        switch (mode)
        {
            case Mode::LowPass:
                return getLowPassMask(frequencyHz);
            case Mode::HighPass:
                return getHighPassMask(frequencyHz);
            case Mode::BandPass:
                return getBandPassMask(frequencyHz);
            default:
                return 1.0f;
        }
    }

    /**
     * Pre-compute mask curve for all FFT bins.
     * Call this when parameters change to avoid per-sample calculations.
     * @param sampleRate Audio sample rate
     * @param fftSize FFT size
     */
    void computeMaskCurve(double sampleRate, int fftSize)
    {
        int numBins = fftSize / 2;
        float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);

        maskCurve.resize(static_cast<size_t>(numBins));

        for (int bin = 0; bin < numBins; ++bin)
        {
            float freq = static_cast<float>(bin) * binResolution;
            maskCurve[static_cast<size_t>(bin)] = getMaskAt(freq);
        }
    }

    /**
     * Get pre-computed mask value for a bin.
     * @param bin Bin index
     * @return Mask value 0-1
     */
    float getMaskForBin(int bin) const
    {
        if (bin < 0 || static_cast<size_t>(bin) >= maskCurve.size())
            return 0.0f;
        return maskCurve[static_cast<size_t>(bin)];
    }

    /**
     * Apply mask to blend wet and dry spectra.
     * @param wetMagnitude Processed magnitude spectrum (modified in place)
     * @param dryMagnitude Original magnitude spectrum
     */
    void applyMask(std::vector<float>& wetMagnitude,
                   const std::vector<float>& dryMagnitude) const
    {
        size_t numBins = std::min(wetMagnitude.size(),
                                   std::min(dryMagnitude.size(), maskCurve.size()));

        for (size_t bin = 0; bin < numBins; ++bin)
        {
            float mask = maskCurve[bin];
            // Linear blend: output = wet * mask + dry * (1 - mask)
            wetMagnitude[bin] = wetMagnitude[bin] * mask + dryMagnitude[bin] * (1.0f - mask);
        }
    }

    /**
     * Apply mask to blend wet and dry phase spectra.
     * Uses circular interpolation for phase values.
     * @param wetPhase Processed phase spectrum (modified in place)
     * @param dryPhase Original phase spectrum
     */
    void applyMaskToPhase(std::vector<float>& wetPhase,
                          const std::vector<float>& dryPhase) const
    {
        size_t numBins = std::min(wetPhase.size(),
                                   std::min(dryPhase.size(), maskCurve.size()));

        for (size_t bin = 0; bin < numBins; ++bin)
        {
            float mask = maskCurve[bin];
            if (mask < 0.999f)
            {
                // For phase, we need to handle wraparound
                // Simple approach: if mask is low, use dry phase
                // This avoids phase interpolation artifacts
                if (mask < 0.5f)
                {
                    wetPhase[bin] = dryPhase[bin];
                }
                // else keep wet phase (when mask > 0.5)
            }
        }
    }

    /**
     * Get the pre-computed mask curve for visualization.
     */
    const std::vector<float>& getMaskCurve() const { return maskCurve; }

private:
    Mode mode = Mode::BandPass;
    float lowFreq = 200.0f;      // Hz
    float highFreq = 5000.0f;    // Hz
    float transition = 1.0f;     // Octaves

    std::vector<float> maskCurve;

    /**
     * Hermite smoothstep function for smooth transitions.
     */
    static float smoothstep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /**
     * Get low-pass shelf mask value.
     * Returns 1 below cutoff, smoothly transitions to 0 above.
     */
    float getLowPassMask(float freq) const
    {
        if (freq <= 0.0f)
            return 1.0f;

        // Guard against invalid lowFreq or transition
        if (lowFreq <= 0.0f || transition <= 0.0f)
            return 1.0f;

        // Calculate octaves from cutoff
        float octavesFromCutoff = std::log2(freq / lowFreq);

        // Transition centered on cutoff
        float t = (octavesFromCutoff / transition) * 0.5f + 0.5f;

        // Invert for low-pass (1 below, 0 above)
        return 1.0f - smoothstep(t);
    }

    /**
     * Get high-pass shelf mask value.
     * Returns 0 below cutoff, smoothly transitions to 1 above.
     */
    float getHighPassMask(float freq) const
    {
        if (freq <= 0.0f)
            return 0.0f;

        // Guard against invalid highFreq or transition
        if (highFreq <= 0.0f || transition <= 0.0f)
            return 1.0f;

        // Calculate octaves from cutoff
        float octavesFromCutoff = std::log2(freq / highFreq);

        // Transition centered on cutoff
        float t = (octavesFromCutoff / transition) * 0.5f + 0.5f;

        return smoothstep(t);
    }

    /**
     * Get band-pass mask value.
     * Returns 1 between cutoffs, smoothly transitions to 0 outside.
     */
    float getBandPassMask(float freq) const
    {
        if (freq <= 0.0f)
            return 0.0f;

        // Combine high-pass from low cutoff and low-pass from high cutoff
        float lowPassPart = getLowPassMaskAt(freq, highFreq);
        float highPassPart = getHighPassMaskAt(freq, lowFreq);

        // Multiply for band-pass effect
        return lowPassPart * highPassPart;
    }

    /**
     * Helper: low-pass mask at arbitrary cutoff.
     */
    float getLowPassMaskAt(float freq, float cutoff) const
    {
        if (freq <= 0.0f)
            return 1.0f;

        // Guard against invalid cutoff or transition
        if (cutoff <= 0.0f || transition <= 0.0f)
            return 1.0f;

        float octavesFromCutoff = std::log2(freq / cutoff);
        float t = (octavesFromCutoff / transition) * 0.5f + 0.5f;
        return 1.0f - smoothstep(t);
    }

    /**
     * Helper: high-pass mask at arbitrary cutoff.
     */
    float getHighPassMaskAt(float freq, float cutoff) const
    {
        if (freq <= 0.0f)
            return 0.0f;

        // Guard against invalid cutoff or transition
        if (cutoff <= 0.0f || transition <= 0.0f)
            return 1.0f;

        float octavesFromCutoff = std::log2(freq / cutoff);
        float t = (octavesFromCutoff / transition) * 0.5f + 0.5f;
        return smoothstep(t);
    }
};

} // namespace fshift
