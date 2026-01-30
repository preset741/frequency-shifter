#include "MusicalQuantizer.h"
#include <algorithm>
#include <cmath>

namespace fshift
{

// Two-pi constant for phase calculations
static constexpr float TWO_PI = 6.283185307179586f;
static constexpr float PI = 3.14159265359f;

MusicalQuantizer::MusicalQuantizer(int rootMidi, ScaleType scaleType)
    : rootMidi(rootMidi),
      scaleType(scaleType),
      scaleDegrees(fshift::getScaleDegrees(scaleType))
{
    // Initialize all phase accumulators and silence counters to zero
    midiPhaseAccumulators.fill(0.0f);
    silentFrameCount.fill(0);
}

void MusicalQuantizer::prepare(double sampleRate, int /*fftSize*/, int hopSize)
{
    // Only reinitialize if parameters changed
    if (sampleRate == cachedSampleRate && hopSize == cachedHopSize && prepared)
        return;

    cachedSampleRate = sampleRate;
    cachedHopSize = hopSize;

    // Reset phase accumulators and silence counters when settings change
    midiPhaseAccumulators.fill(0.0f);
    silentFrameCount.fill(0);

    prepared = true;
}

void MusicalQuantizer::reset()
{
    midiPhaseAccumulators.fill(0.0f);
    silentFrameCount.fill(0);
}

void MusicalQuantizer::setRootNote(int newRootMidi)
{
    rootMidi = std::clamp(newRootMidi, 0, 127);
}

void MusicalQuantizer::setScaleType(ScaleType newScaleType)
{
    scaleType = newScaleType;
    scaleDegrees = fshift::getScaleDegrees(scaleType);
}

float MusicalQuantizer::quantizeFrequency(float frequency, float strength) const
{
    if (frequency <= 0.0f)
        return 0.0f;

    // Convert to MIDI
    float midiNote = tuning::freqToMidi(frequency);

    // Quantize to scale
    int quantizedMidi = tuning::quantizeToScale(midiNote, rootMidi, scaleDegrees);

    // Convert back to frequency
    float quantizedFreq = tuning::midiToFreq(static_cast<float>(quantizedMidi));

    // Interpolate based on strength
    return (1.0f - strength) * frequency + strength * quantizedFreq;
}

std::vector<float> MusicalQuantizer::quantizeFrequencies(const std::vector<float>& frequencies, float strength)
{
    if (strength <= 0.0f)
        return frequencies;

    strength = std::clamp(strength, 0.0f, 1.0f);

    std::vector<float> quantized(frequencies.size());
    for (size_t i = 0; i < frequencies.size(); ++i)
    {
        quantized[i] = quantizeFrequency(frequencies[i], strength);
    }

    return quantized;
}

float MusicalQuantizer::applyDriftCents(float frequency, float cents)
{
    // Convert cents to frequency ratio: ratio = 2^(cents/1200)
    // 100 cents = 1 semitone, 1200 cents = 1 octave
    float ratio = std::pow(2.0f, cents / 1200.0f);
    return frequency * ratio;
}

void MusicalQuantizer::findTwoNearestScaleFrequencies(float frequency,
                                                       float& lowerFreq, float& upperFreq,
                                                       float& lowerWeight, float& upperWeight) const
{
    // Strategy A: Find two nearest scale frequencies and distribute energy by inverse distance
    if (frequency <= 0.0f)
    {
        lowerFreq = upperFreq = 0.0f;
        lowerWeight = upperWeight = 0.0f;
        return;
    }

    // Convert to MIDI for easier scale calculations
    float midiNote = tuning::freqToMidi(frequency);

    // Get the primary quantized note (nearest)
    int nearestMidi = tuning::quantizeToScale(midiNote, rootMidi, scaleDegrees);
    float nearestFreq = tuning::midiToFreq(static_cast<float>(nearestMidi));

    // Determine if we need to look up or down for the second-nearest
    int secondMidi;
    if (frequency >= nearestFreq)
    {
        // Look for the next scale note above
        secondMidi = nearestMidi;
        for (int searchMidi = nearestMidi + 1; searchMidi <= nearestMidi + 12; ++searchMidi)
        {
            int quantized = tuning::quantizeToScale(static_cast<float>(searchMidi), rootMidi, scaleDegrees);
            if (quantized > nearestMidi)
            {
                secondMidi = quantized;
                break;
            }
        }
        lowerFreq = nearestFreq;
        upperFreq = tuning::midiToFreq(static_cast<float>(secondMidi));
    }
    else
    {
        // Look for the next scale note below
        secondMidi = nearestMidi;
        for (int searchMidi = nearestMidi - 1; searchMidi >= nearestMidi - 12; --searchMidi)
        {
            int quantized = tuning::quantizeToScale(static_cast<float>(searchMidi), rootMidi, scaleDegrees);
            if (quantized < nearestMidi)
            {
                secondMidi = quantized;
                break;
            }
        }
        lowerFreq = tuning::midiToFreq(static_cast<float>(secondMidi));
        upperFreq = nearestFreq;
    }

    // Calculate inverse distance weights
    // Use log-frequency distance (cents) for perceptually uniform weighting
    float distToLower = std::abs(tuning::centsDifference(frequency, lowerFreq));
    float distToUpper = std::abs(tuning::centsDifference(frequency, upperFreq));

    // Avoid division by zero
    float totalDist = distToLower + distToUpper;
    if (totalDist < 0.001f)
    {
        // Frequency is exactly on a scale note
        lowerWeight = 1.0f;
        upperWeight = 0.0f;
    }
    else
    {
        // Inverse distance weighting: closer gets more weight
        lowerWeight = distToUpper / totalDist;  // Inverse: farther from upper = closer to lower
        upperWeight = distToLower / totalDist;
    }
}

void MusicalQuantizer::applyMagnitudeSmoothing(std::vector<float>& magnitude)
{
    // Strategy C: 3-tap moving average [0.25, 0.5, 0.25]
    // This creates gentle spectral blur to reduce sharp peaks/pits

    if (magnitude.size() < 3)
        return;

    size_t numBins = magnitude.size();
    std::vector<float> smoothed(numBins);

    // First bin: just use original (boundary condition)
    smoothed[0] = magnitude[0];

    // Middle bins: apply 3-tap kernel
    for (size_t k = 1; k < numBins - 1; ++k)
    {
        smoothed[k] = 0.25f * magnitude[k - 1] +
                      0.50f * magnitude[k] +
                      0.25f * magnitude[k + 1];
    }

    // Last bin: just use original (boundary condition)
    smoothed[numBins - 1] = magnitude[numBins - 1];

    // Copy back
    magnitude = std::move(smoothed);
}

std::pair<std::vector<float>, std::vector<float>> MusicalQuantizer::quantizeSpectrum(
    const std::vector<float>& magnitude,
    const std::vector<float>& phase,
    double sampleRate,
    int fftSize,
    float strength,
    const std::vector<float>* driftCents)
{
    if (strength <= 0.0f)
        return { magnitude, phase };

    strength = std::clamp(strength, 0.0f, 1.0f);

    int numBins = static_cast<int>(magnitude.size());
    float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);

    // Initialize output arrays
    std::vector<float> quantizedMagnitude(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> quantizedPhase(static_cast<size_t>(numBins), 0.0f);

    // Phase 2A.1: Track contributor count per target bin for accumulation normalization
    std::vector<int> contributorCount(static_cast<size_t>(numBins), 0);

    // Track target MIDI note for each target bin (for phase continuity)
    std::vector<int> targetMidiNotes(static_cast<size_t>(numBins), -1);

    // Track whether each target bin received energy from a DIFFERENT source bin (was remapped)
    std::vector<bool> binWasRemapped(static_cast<size_t>(numBins), false);

    // Track the strongest contributor's phase for each target bin
    std::vector<float> maxMagnitudeAtBin(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> strongestContributorPhase(static_cast<size_t>(numBins), 0.0f);

    // Track which MIDI notes received energy this frame (for decay tracking)
    std::array<float, NUM_MIDI_NOTES> midiNoteMagnitude{};
    midiNoteMagnitude.fill(0.0f);

    // Phase 2A.2: Calculate total energy BEFORE quantization
    float energyBefore = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        energyBefore += magnitude[static_cast<size_t>(k)] * magnitude[static_cast<size_t>(k)];
    }

    // Calculate bin frequencies and target bins
    // Strategy A: Use weighted energy distribution to two nearest scale bins
    for (int k = 0; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq <= 0.0f)
            continue;

        float sourceMag = magnitude[static_cast<size_t>(k)];
        float sourcePhase = phase[static_cast<size_t>(k)];

        // Get the two nearest scale frequencies and their weights
        float lowerFreq, upperFreq, lowerWeight, upperWeight;
        findTwoNearestScaleFrequencies(binFreq, lowerFreq, upperFreq, lowerWeight, upperWeight);

        // Interpolate based on strength (blend between original and quantized)
        float lowerTargetFreq = (1.0f - strength) * binFreq + strength * lowerFreq;
        float upperTargetFreq = (1.0f - strength) * binFreq + strength * upperFreq;

        // Apply drift if provided (to both targets)
        if (driftCents != nullptr && static_cast<size_t>(k) < driftCents->size())
        {
            lowerTargetFreq = applyDriftCents(lowerTargetFreq, (*driftCents)[static_cast<size_t>(k)]);
            upperTargetFreq = applyDriftCents(upperTargetFreq, (*driftCents)[static_cast<size_t>(k)]);
        }

        // Calculate target bins
        int lowerBin = static_cast<int>(std::round(lowerTargetFreq / binResolution));
        int upperBin = static_cast<int>(std::round(upperTargetFreq / binResolution));
        lowerBin = std::clamp(lowerBin, 0, numBins - 1);
        upperBin = std::clamp(upperBin, 0, numBins - 1);

        // Get MIDI notes for both targets (for phase tracking)
        int lowerMidi = tuning::quantizeToScale(tuning::freqToMidi(lowerFreq), rootMidi, scaleDegrees);
        int upperMidi = tuning::quantizeToScale(tuning::freqToMidi(upperFreq), rootMidi, scaleDegrees);

        // Distribute energy to lower target bin (if weight > 0)
        if (lowerWeight > 0.001f)
        {
            float contrib = sourceMag * lowerWeight;
            quantizedMagnitude[static_cast<size_t>(lowerBin)] += contrib;
            contributorCount[static_cast<size_t>(lowerBin)]++;

            // Track if remapped
            if (lowerBin != k)
            {
                binWasRemapped[static_cast<size_t>(lowerBin)] = true;

                // Track MIDI note magnitude
                if (lowerMidi >= 0 && lowerMidi < NUM_MIDI_NOTES)
                {
                    midiNoteMagnitude[static_cast<size_t>(lowerMidi)] += contrib;
                    targetMidiNotes[static_cast<size_t>(lowerBin)] = lowerMidi;
                }
            }

            // Track strongest contributor's phase
            if (contrib > maxMagnitudeAtBin[static_cast<size_t>(lowerBin)])
            {
                maxMagnitudeAtBin[static_cast<size_t>(lowerBin)] = contrib;
                strongestContributorPhase[static_cast<size_t>(lowerBin)] = sourcePhase;
            }
        }

        // Distribute energy to upper target bin (if weight > 0 and different from lower)
        if (upperWeight > 0.001f && upperBin != lowerBin)
        {
            float contrib = sourceMag * upperWeight;
            quantizedMagnitude[static_cast<size_t>(upperBin)] += contrib;
            contributorCount[static_cast<size_t>(upperBin)]++;

            // Track if remapped
            if (upperBin != k)
            {
                binWasRemapped[static_cast<size_t>(upperBin)] = true;

                // Track MIDI note magnitude
                if (upperMidi >= 0 && upperMidi < NUM_MIDI_NOTES)
                {
                    midiNoteMagnitude[static_cast<size_t>(upperMidi)] += contrib;
                    targetMidiNotes[static_cast<size_t>(upperBin)] = upperMidi;
                }
            }

            // Track strongest contributor's phase
            if (contrib > maxMagnitudeAtBin[static_cast<size_t>(upperBin)])
            {
                maxMagnitudeAtBin[static_cast<size_t>(upperBin)] = contrib;
                strongestContributorPhase[static_cast<size_t>(upperBin)] = sourcePhase;
            }
        }
    }

    // Phase 2A.1: Apply accumulation normalization
    // When multiple bins map to same target, normalize by sqrt(contributorCount)
    for (int k = 0; k < numBins; ++k)
    {
        if (contributorCount[static_cast<size_t>(k)] > 1)
        {
            quantizedMagnitude[static_cast<size_t>(k)] /= std::sqrt(static_cast<float>(contributorCount[static_cast<size_t>(k)]));
        }
    }

    // Strategy C: Apply magnitude smoothing (3-tap moving average)
    // This reduces sharp spectral peaks/pits that can cause resonance
    applyMagnitudeSmoothing(quantizedMagnitude);

    // Phase 2A.2: Calculate total energy AFTER quantization+smoothing and normalize
    float energyAfter = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        energyAfter += quantizedMagnitude[static_cast<size_t>(k)] * quantizedMagnitude[static_cast<size_t>(k)];
    }

    // Apply energy normalization scale factor
    if (energyAfter > 1e-10f)
    {
        float scaleFactor = std::sqrt(energyBefore / energyAfter);
        for (int k = 0; k < numBins; ++k)
        {
            quantizedMagnitude[static_cast<size_t>(k)] *= scaleFactor;
        }
    }

    // Phase 2A.3: Phase continuity with magnitude gating and decay
    // FIX: Blend between input phase (from phase vocoder) and phase accumulator based on strength
    // This ensures Enhanced Mode affects the non-quantized portion of the signal
    if (prepared && cachedSampleRate > 0.0 && cachedHopSize > 0)
    {
        // Update silence counters and phase accumulators for each MIDI note
        for (int midi = 0; midi < NUM_MIDI_NOTES; ++midi)
        {
            if (midiNoteMagnitude[static_cast<size_t>(midi)] > MAGNITUDE_THRESHOLD)
            {
                // Note is active - reset silence counter, update phase accumulator
                silentFrameCount[static_cast<size_t>(midi)] = 0;

                float noteFreq = tuning::midiToFreq(static_cast<float>(midi));
                float phaseIncrement = TWO_PI * noteFreq * static_cast<float>(cachedHopSize) / static_cast<float>(cachedSampleRate);
                midiPhaseAccumulators[static_cast<size_t>(midi)] += phaseIncrement;

                // Wrap to [-PI, PI] for numerical stability
                while (midiPhaseAccumulators[static_cast<size_t>(midi)] > PI)
                    midiPhaseAccumulators[static_cast<size_t>(midi)] -= TWO_PI;
                while (midiPhaseAccumulators[static_cast<size_t>(midi)] < -PI)
                    midiPhaseAccumulators[static_cast<size_t>(midi)] += TWO_PI;
            }
            else
            {
                // Note is silent - increment silence counter
                silentFrameCount[static_cast<size_t>(midi)]++;

                // If silent for too long, reset the phase accumulator
                // This prevents tinnitus/ringing when input stops
                if (silentFrameCount[static_cast<size_t>(midi)] >= SILENCE_FRAMES_TO_RESET)
                {
                    midiPhaseAccumulators[static_cast<size_t>(midi)] = 0.0f;
                }
                // Don't increment phase for silent notes - let them decay naturally
            }
        }

        // Assign phases to output bins
        // FIX: Blend between input phase and quantized phase based on strength
        for (int k = 0; k < numBins; ++k)
        {
            if (quantizedMagnitude[static_cast<size_t>(k)] > 1e-10f)
            {
                // Base phase is always from input (may be phase vocoder output if Enhanced Mode on)
                float inputPhase = strongestContributorPhase[static_cast<size_t>(k)];
                float outputPhase = inputPhase;  // Default to input phase

                if (binWasRemapped[static_cast<size_t>(k)])
                {
                    // This bin received energy from a different source bin
                    int midiNote = targetMidiNotes[static_cast<size_t>(k)];
                    if (midiNote >= 0 && midiNote < NUM_MIDI_NOTES &&
                        midiNoteMagnitude[static_cast<size_t>(midiNote)] > MAGNITUDE_THRESHOLD)
                    {
                        // Get the quantized phase (from persistent phase accumulator)
                        float quantizedPhaseValue = midiPhaseAccumulators[static_cast<size_t>(midiNote)];

                        // FIX: Blend between input phase and quantized phase based on strength
                        // At strength=0: 100% input phase (phase vocoder if enabled)
                        // At strength=1: 100% quantized phase (phase accumulator)
                        // This allows Enhanced Mode to affect the non-quantized portion

                        // Phase interpolation needs to handle wraparound
                        // Use circular interpolation to avoid jumps at +/- PI boundary
                        float phaseDiff = quantizedPhaseValue - inputPhase;

                        // Normalize phase difference to [-PI, PI]
                        while (phaseDiff > PI) phaseDiff -= TWO_PI;
                        while (phaseDiff < -PI) phaseDiff += TWO_PI;

                        // Interpolate: inputPhase + strength * (difference)
                        outputPhase = inputPhase + strength * phaseDiff;

                        // Wrap result to [-PI, PI]
                        while (outputPhase > PI) outputPhase -= TWO_PI;
                        while (outputPhase < -PI) outputPhase += TWO_PI;
                    }
                    // else: Note is below threshold - outputPhase stays as inputPhase (natural decay)
                }
                // else: Bin was not remapped - outputPhase stays as inputPhase (preserve vocoder coherence)

                quantizedPhase[static_cast<size_t>(k)] = outputPhase;
            }
        }
    }
    else
    {
        // Fallback: use phase from strongest contributor (original behavior)
        for (int k = 0; k < numBins; ++k)
        {
            quantizedPhase[static_cast<size_t>(k)] = strongestContributorPhase[static_cast<size_t>(k)];
        }
    }

    // Zero DC bin to prevent low-frequency rumble/buildup
    if (numBins > 0)
    {
        quantizedMagnitude[0] = 0.0f;
        quantizedPhase[0] = 0.0f;
    }

    return { quantizedMagnitude, quantizedPhase };
}

std::vector<float> MusicalQuantizer::getScaleFrequencies(float minFreq, float maxFreq) const
{
    std::vector<float> frequencies;

    // Convert frequency range to MIDI range
    int minMidi = static_cast<int>(std::floor(tuning::freqToMidi(minFreq)));
    int maxMidi = static_cast<int>(std::ceil(tuning::freqToMidi(maxFreq)));

    // Generate all scale notes in MIDI range
    for (int midi = minMidi; midi <= maxMidi; ++midi)
    {
        // Check if this MIDI note is in the scale
        int relative = ((midi - rootMidi) % 12 + 12) % 12;

        bool inScale = false;
        for (int degree : scaleDegrees)
        {
            if (relative == degree)
            {
                inScale = true;
                break;
            }
        }

        if (inScale)
        {
            float freq = tuning::midiToFreq(static_cast<float>(midi));
            if (freq >= minFreq && freq <= maxFreq)
            {
                frequencies.push_back(freq);
            }
        }
    }

    return frequencies;
}

} // namespace fshift
