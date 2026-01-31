#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <complex>

namespace fshift
{

/**
 * SpectralDelay - Frequency-dependent delay in the spectral domain.
 *
 * Each FFT bin has its own delay line, allowing different frequencies
 * to be delayed by different amounts. This creates natural diffusion
 * and smearing effects without additional FFT/IFFT overhead.
 *
 * Features:
 * - Base delay time (affects all frequencies)
 * - Frequency slope (low frequencies delayed more or less than high)
 * - Feedback with high-frequency damping
 * - Wet/dry mix
 */
class SpectralDelay
{
public:
    SpectralDelay() = default;
    ~SpectralDelay() = default;

    /**
     * Prepare the delay for processing.
     * @param sampleRate Audio sample rate
     * @param fftSize FFT size
     * @param hopSize Hop size between frames
     * @param maxDelayMs Maximum delay time in milliseconds
     */
    void prepare(double newSampleRate, int newFftSize, int newHopSize, float maxDelayMs = 2000.0f)
    {
        sampleRate = newSampleRate;
        fftSize = newFftSize;
        hopSize = newHopSize;
        numBins = fftSize / 2;

        // Calculate max delay in frames
        double frameRate = sampleRate / static_cast<double>(hopSize);
        maxDelayFrames = static_cast<int>(std::ceil(maxDelayMs / 1000.0 * frameRate));

        // Allocate delay buffers for each bin (magnitude and phase)
        magnitudeBuffers.resize(static_cast<size_t>(numBins));
        phaseBuffers.resize(static_cast<size_t>(numBins));

        for (int bin = 0; bin < numBins; ++bin)
        {
            magnitudeBuffers[static_cast<size_t>(bin)].resize(static_cast<size_t>(maxDelayFrames), 0.0f);
            phaseBuffers[static_cast<size_t>(bin)].resize(static_cast<size_t>(maxDelayFrames), 0.0f);
        }

        writePositions.resize(static_cast<size_t>(numBins), 0);

        // Pre-compute delay times and damping curve
        computeDelayTimes();
        computeDampingCurve();
    }

    /**
     * Reset all delay buffers.
     */
    void reset()
    {
        for (int bin = 0; bin < numBins; ++bin)
        {
            std::fill(magnitudeBuffers[static_cast<size_t>(bin)].begin(),
                      magnitudeBuffers[static_cast<size_t>(bin)].end(), 0.0f);
            std::fill(phaseBuffers[static_cast<size_t>(bin)].begin(),
                      phaseBuffers[static_cast<size_t>(bin)].end(), 0.0f);
            writePositions[static_cast<size_t>(bin)] = 0;
        }
    }

    /**
     * Set base delay time in milliseconds.
     */
    void setDelayTime(float ms)
    {
        delayTimeMs = std::clamp(ms, 0.0f, 2000.0f);
        computeDelayTimes();
    }
    float getDelayTime() const { return delayTimeMs; }

    /**
     * Set frequency slope (-100% to +100%).
     * Negative: low frequencies delayed more
     * Positive: high frequencies delayed more
     */
    void setFrequencySlope(float slope)
    {
        frequencySlope = std::clamp(slope, -100.0f, 100.0f);
        computeDelayTimes();
    }
    float getFrequencySlope() const { return frequencySlope; }

    /**
     * Set feedback amount (0-95%).
     */
    void setFeedback(float fb)
    {
        feedback = std::clamp(fb, 0.0f, 0.95f);
    }
    float getFeedback() const { return feedback; }

    /**
     * Set high-frequency damping (0-100%).
     * Higher values = more HF absorption per repeat.
     */
    void setDamping(float damp)
    {
        damping = std::clamp(damp, 0.0f, 100.0f);
        computeDampingCurve();
    }
    float getDamping() const { return damping; }

    /**
     * Set wet/dry mix for delay (0-100%).
     */
    void setMix(float mixPercent)
    {
        mix = std::clamp(mixPercent, 0.0f, 100.0f) / 100.0f;
    }
    float getMix() const { return mix * 100.0f; }

    /**
     * Set gain for delayed signal in dB (-12 to +24 dB).
     */
    void setGain(float gainDb)
    {
        gainDb = std::clamp(gainDb, -12.0f, 24.0f);
        gain = std::pow(10.0f, gainDb / 20.0f);
    }
    float getGainDb() const { return 20.0f * std::log10(gain); }

    /**
     * Process spectrum through delay.
     * @param magnitude Input/output magnitude spectrum
     * @param phase Input/output phase spectrum
     */
    void process(std::vector<float>& magnitude, std::vector<float>& phase)
    {
        // Early exit if not properly initialized or delay is too short
        if (numBins <= 0 || delayFramesPerBin.empty() || dampingCurve.empty() || delayTimeMs < 0.1f)
            return;

        // Process only the bins we have (min of magnitude size and our numBins)
        const int binsToProcess = std::min(static_cast<int>(magnitude.size()), numBins);

        for (int bin = 0; bin < binsToProcess; ++bin)
        {
            size_t binIdx = static_cast<size_t>(bin);
            int writePos = writePositions[binIdx];
            int delayFrames = delayFramesPerBin[binIdx];

            // Read from delay line
            int readPos = (writePos - delayFrames + maxDelayFrames) % maxDelayFrames;
            float delayedMag = magnitudeBuffers[binIdx][static_cast<size_t>(readPos)];
            float delayedPhase = phaseBuffers[binIdx][static_cast<size_t>(readPos)];

            // Get damping factor for this bin
            float dampFactor = dampingCurve[binIdx];

            // Apply feedback with damping
            float feedbackMag = delayedMag * feedback * dampFactor;

            // Store dry magnitude before writing to delay line
            float dryMag = magnitude[binIdx];
            float dryPhase = phase[binIdx];

            // Write to delay line (input + feedback)
            magnitudeBuffers[binIdx][static_cast<size_t>(writePos)] = dryMag + feedbackMag;
            phaseBuffers[binIdx][static_cast<size_t>(writePos)] = dryPhase;

            // Mix: crossfade between dry and wet (delayed) signal
            // At mix=0: 100% dry, 0% wet
            // At mix=100: 0% dry, 100% wet
            float wetMag = delayedMag * gain;
            magnitude[binIdx] = dryMag * (1.0f - mix) + wetMag * mix;

            // Blend phase based on mix ratio
            if (mix > 0.01f && delayedMag > 0.001f)
            {
                phase[binIdx] = dryPhase * (1.0f - mix) + delayedPhase * mix;
            }

            // Advance write position
            writePositions[binIdx] = (writePos + 1) % maxDelayFrames;
        }
    }

private:
    double sampleRate = 44100.0;
    int fftSize = 4096;
    int hopSize = 1024;
    int numBins = 2048;
    int maxDelayFrames = 100;

    // Parameters
    float delayTimeMs = 200.0f;
    float frequencySlope = 0.0f;   // -100 to +100
    float feedback = 0.3f;
    float damping = 30.0f;         // 0-100%
    float mix = 0.5f;              // 0-1
    float gain = 1.0f;             // Linear gain for delayed signal

    // Per-bin delay buffers
    std::vector<std::vector<float>> magnitudeBuffers;
    std::vector<std::vector<float>> phaseBuffers;
    std::vector<int> writePositions;

    // Pre-computed per-bin values
    std::vector<int> delayFramesPerBin;
    std::vector<float> dampingCurve;

    /**
     * Compute delay time for each bin based on slope.
     */
    void computeDelayTimes()
    {
        delayFramesPerBin.resize(static_cast<size_t>(numBins));

        double frameRate = sampleRate / static_cast<double>(hopSize);
        float baseDelayFrames = static_cast<float>(delayTimeMs / 1000.0 * frameRate);

        for (int bin = 0; bin < numBins; ++bin)
        {
            // Normalized bin position (0 = DC, 1 = Nyquist)
            float binNorm = static_cast<float>(bin) / static_cast<float>(numBins);

            // Apply slope: negative slope = more delay at low frequencies
            // Map slope from -100..+100 to multiplier
            float slopeFactor = 1.0f + (frequencySlope / 100.0f) * (binNorm - 0.5f) * 2.0f;
            slopeFactor = std::max(0.1f, slopeFactor);  // Prevent negative/zero delay

            int delayFrames = static_cast<int>(baseDelayFrames * slopeFactor);
            delayFrames = std::clamp(delayFrames, 1, maxDelayFrames - 1);

            delayFramesPerBin[static_cast<size_t>(bin)] = delayFrames;
        }
    }

    /**
     * Compute damping curve (HF absorption).
     */
    void computeDampingCurve()
    {
        dampingCurve.resize(static_cast<size_t>(numBins));

        for (int bin = 0; bin < numBins; ++bin)
        {
            float binNorm = static_cast<float>(bin) / static_cast<float>(numBins);

            // Exponential damping: more absorption at higher frequencies
            // damping=0: no absorption, damping=100: strong HF absorption
            float dampAmount = damping / 100.0f;
            float dampFactor = 1.0f - dampAmount * binNorm * binNorm;
            dampFactor = std::max(0.0f, dampFactor);

            dampingCurve[static_cast<size_t>(bin)] = dampFactor;
        }
    }
};

} // namespace fshift
