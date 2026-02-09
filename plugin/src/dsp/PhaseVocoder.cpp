#include "PhaseVocoder.h"
#include <algorithm>
#include <cmath>

namespace fshift
{

PhaseVocoder::PhaseVocoder(int fftSize, int hopSize, double sampleRate)
    : hopSize(hopSize),
      numBins(fftSize / 2 + 1),
      sampleRate(sampleRate),
      firstFrame(true),
      peakThresholdDb(-40.0f),
      regionSize(4),
      usePhaseLocking(true)
{
    // Initialize state vectors
    prevMagnitude.resize(numBins, 0.0f);
    prevPhase.resize(numBins, 0.0f);
    prevSynthPhase.resize(numBins, 0.0f);

    // Pre-compute bin frequencies
    binFrequencies.resize(numBins);
    for (int i = 0; i < numBins; ++i)
    {
        // Bin frequency formula: bin_freq[k] = k * sample_rate / fft_size
        // Since numBins = fftSize/2 + 1, we have fftSize = 2*(numBins-1)
        // So this is equivalent to: k * sample_rate / fft_size
        binFrequencies[i] = static_cast<float>(i) * static_cast<float>(sampleRate)
                            / static_cast<float>(2 * (numBins - 1));
    }

    // Pre-compute expected phase advance per hop
    expectedPhaseAdvance.resize(numBins);
    for (int i = 0; i < numBins; ++i)
    {
        expectedPhaseAdvance[i] = 2.0f * std::numbers::pi_v<float> * binFrequencies[i]
                                  * static_cast<float>(hopSize) / static_cast<float>(sampleRate);
    }
}

void PhaseVocoder::reset()
{
    std::fill(prevMagnitude.begin(), prevMagnitude.end(), 0.0f);
    std::fill(prevPhase.begin(), prevPhase.end(), 0.0f);
    std::fill(prevSynthPhase.begin(), prevSynthPhase.end(), 0.0f);
    firstFrame = true;
}

float PhaseVocoder::wrapPhase(float phase)
{
    // Wrap to [-pi, pi] using atan2(sin, cos)
    return std::atan2(std::sin(phase), std::cos(phase));
}

std::vector<bool> PhaseVocoder::detectPeaks(const std::vector<float>& magnitude)
{
    std::vector<bool> peaks(numBins, false);

    if (numBins < 3)
        return peaks;

    // Convert to dB and find threshold
    float maxMagDb = -std::numeric_limits<float>::infinity();
    std::vector<float> magDb(numBins);

    for (int i = 0; i < numBins; ++i)
    {
        magDb[i] = 20.0f * std::log10(magnitude[i] + 1e-10f);
        maxMagDb = std::max(maxMagDb, magDb[i]);
    }

    float threshold = maxMagDb + peakThresholdDb;

    // Find local maxima above threshold
    for (int i = 1; i < numBins - 1; ++i)
    {
        if (magDb[i] > threshold && magDb[i] > magDb[i - 1] && magDb[i] > magDb[i + 1])
        {
            peaks[i] = true;
        }
    }

    return peaks;
}

std::vector<float> PhaseVocoder::computeInstantaneousFrequency(const std::vector<float>& phasePrev,
                                                                const std::vector<float>& phaseCurr)
{
    std::vector<float> instFreq(numBins);

    for (int i = 0; i < numBins; ++i)
    {
        // Actual phase difference
        float phaseDiff = phaseCurr[i] - phasePrev[i];

        // Wrap phase difference to [-pi, pi]
        phaseDiff = wrapPhase(phaseDiff);

        // Phase deviation from expected
        float phaseDeviation = phaseDiff - expectedPhaseAdvance[i];

        // Wrap deviation to [-pi, pi]
        phaseDeviation = wrapPhase(phaseDeviation);

        // Instantaneous frequency = bin frequency + deviation
        instFreq[i] = binFrequencies[i]
                      + phaseDeviation * static_cast<float>(sampleRate)
                        / (2.0f * std::numbers::pi_v<float> * static_cast<float>(hopSize));
    }

    return instFreq;
}

std::vector<float> PhaseVocoder::phaseLockVertical(const std::vector<float>& phase,
                                                    [[maybe_unused]] const std::vector<float>& magnitude,
                                                    const std::vector<bool>& peaks)
{
    std::vector<float> lockedPhase = phase;

    // For each peak, lock phases in region of influence
    for (int peakIdx = 0; peakIdx < numBins; ++peakIdx)
    {
        if (!peaks[peakIdx])
            continue;

        // Define region of influence
        int start = std::max(0, peakIdx - regionSize);
        int end = std::min(numBins, peakIdx + regionSize + 1);

        // Use the peak's phase as reference
        float peakPhase = phase[peakIdx];

        for (int i = start; i < end; ++i)
        {
            if (i != peakIdx)
            {
                // Lock non-peak bins to the peak's phase
                // This maintains coherence within the region of influence
                lockedPhase[i] = peakPhase;
            }
        }
    }

    return lockedPhase;
}

std::vector<float> PhaseVocoder::synthesizePhase(const std::vector<float>& instFreq,
                                                  const std::vector<float>& phasePrevSynth,
                                                  float shiftHz)
{
    std::vector<float> newPhase(numBins);

    for (int i = 0; i < numBins; ++i)
    {
        // Apply frequency shift to instantaneous frequency
        float shiftedFreq = instFreq[i] + shiftHz;

        // Phase advance based on shifted frequency
        float phaseAdvance = 2.0f * std::numbers::pi_v<float> * shiftedFreq
                             * static_cast<float>(hopSize) / static_cast<float>(sampleRate);

        // Synthesize new phase
        newPhase[i] = wrapPhase(phasePrevSynth[i] + phaseAdvance);
    }

    return newPhase;
}

std::vector<float> PhaseVocoder::process(const std::vector<float>& magnitude,
                                          const std::vector<float>& phase,
                                          float shiftHz)
{
    std::vector<float> outputPhase(numBins);

    if (firstFrame)
    {
        // First frame: just copy phase
        outputPhase = phase;
        prevSynthPhase = phase;
        firstFrame = false;
    }
    else
    {
        // Apply phase locking if enabled
        std::vector<float> lockedPhase = phase;
        if (usePhaseLocking)
        {
            std::vector<bool> peaks = detectPeaks(magnitude);
            lockedPhase = phaseLockVertical(phase, magnitude, peaks);
        }

        // Compute instantaneous frequencies using the (potentially locked) phase
        std::vector<float> instFreq = computeInstantaneousFrequency(prevPhase, lockedPhase);

        // Synthesize phase for shifted frequencies
        outputPhase = synthesizePhase(instFreq, prevSynthPhase, shiftHz);

        // Update synthesis phase history
        prevSynthPhase = outputPhase;
    }

    // Update analysis history
    prevMagnitude = magnitude;
    prevPhase = phase;

    return outputPhase;
}

} // namespace fshift
