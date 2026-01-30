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
    // Initialize all phase accumulators to zero
    midiPhaseAccumulators.fill(0.0f);
}

void MusicalQuantizer::prepare(double sampleRate, int /*fftSize*/, int hopSize)
{
    // Only reinitialize if parameters changed
    if (sampleRate == cachedSampleRate && hopSize == cachedHopSize && prepared)
        return;

    cachedSampleRate = sampleRate;
    cachedHopSize = hopSize;

    // Reset phase accumulators when settings change
    midiPhaseAccumulators.fill(0.0f);

    prepared = true;
}

void MusicalQuantizer::reset()
{
    midiPhaseAccumulators.fill(0.0f);
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
    // If a bin's only contributor is itself (source == target), we should preserve input phase
    std::vector<bool> binWasRemapped(static_cast<size_t>(numBins), false);

    // Track the strongest contributor's phase for each target bin (for non-remapped bins)
    std::vector<float> maxMagnitudeAtBin(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> strongestContributorPhase(static_cast<size_t>(numBins), 0.0f);

    // Phase 2A.2: Calculate total energy BEFORE quantization
    float energyBefore = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        energyBefore += magnitude[static_cast<size_t>(k)] * magnitude[static_cast<size_t>(k)];
    }

    // Calculate bin frequencies and target bins
    for (int k = 0; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq <= 0.0f)
            continue;

        // Get the target MIDI note for this frequency
        float midiNote = tuning::freqToMidi(binFreq);
        int quantizedMidi = tuning::quantizeToScale(midiNote, rootMidi, scaleDegrees);
        float quantizedFreq = tuning::midiToFreq(static_cast<float>(quantizedMidi));

        // Interpolate frequency based on strength
        float targetFreq = (1.0f - strength) * binFreq + strength * quantizedFreq;

        // Apply drift if provided
        if (driftCents != nullptr && static_cast<size_t>(k) < driftCents->size())
        {
            targetFreq = applyDriftCents(targetFreq, (*driftCents)[static_cast<size_t>(k)]);
        }

        // Calculate target bin
        int targetBin = static_cast<int>(std::round(targetFreq / binResolution));
        targetBin = std::clamp(targetBin, 0, numBins - 1);

        // Track if this bin was remapped (source != target)
        if (targetBin != k)
        {
            binWasRemapped[static_cast<size_t>(targetBin)] = true;
        }

        // Accumulate magnitude (will normalize later)
        quantizedMagnitude[static_cast<size_t>(targetBin)] += magnitude[static_cast<size_t>(k)];
        contributorCount[static_cast<size_t>(targetBin)]++;

        // Track strongest contributor's phase (for preserving input phase on non-remapped bins)
        if (magnitude[static_cast<size_t>(k)] > maxMagnitudeAtBin[static_cast<size_t>(targetBin)])
        {
            maxMagnitudeAtBin[static_cast<size_t>(targetBin)] = magnitude[static_cast<size_t>(k)];
            strongestContributorPhase[static_cast<size_t>(targetBin)] = phase[static_cast<size_t>(k)];
        }

        // Store target MIDI note for phase continuity (only for remapped bins)
        // Use the MIDI note for the target frequency at full strength
        if (strength > 0.5f && targetBin != k)
        {
            targetMidiNotes[static_cast<size_t>(targetBin)] = std::clamp(quantizedMidi, 0, NUM_MIDI_NOTES - 1);
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

    // Phase 2A.2: Calculate total energy AFTER quantization and normalize
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

    // Phase 2A.3: Phase continuity - but ONLY for bins that were remapped
    // For bins that weren't remapped (source == target), preserve input phase
    // This allows phase vocoder's coherent phases to pass through for non-quantized bins
    if (prepared && cachedSampleRate > 0.0 && cachedHopSize > 0)
    {
        // Update phase accumulators for ALL MIDI notes (maintains continuity even when notes aren't active)
        for (int midi = 0; midi < NUM_MIDI_NOTES; ++midi)
        {
            float noteFreq = tuning::midiToFreq(static_cast<float>(midi));
            // phaseIncrement = 2*PI * frequency * hopSize / sampleRate
            float phaseIncrement = TWO_PI * noteFreq * static_cast<float>(cachedHopSize) / static_cast<float>(cachedSampleRate);
            midiPhaseAccumulators[static_cast<size_t>(midi)] += phaseIncrement;

            // Wrap to [-PI, PI] for numerical stability
            while (midiPhaseAccumulators[static_cast<size_t>(midi)] > PI)
                midiPhaseAccumulators[static_cast<size_t>(midi)] -= TWO_PI;
            while (midiPhaseAccumulators[static_cast<size_t>(midi)] < -PI)
                midiPhaseAccumulators[static_cast<size_t>(midi)] += TWO_PI;
        }

        // Assign phases to output bins
        for (int k = 0; k < numBins; ++k)
        {
            if (quantizedMagnitude[static_cast<size_t>(k)] > 1e-10f)
            {
                if (binWasRemapped[static_cast<size_t>(k)])
                {
                    // This bin received energy from a different source bin - use phase accumulator
                    int midiNote = targetMidiNotes[static_cast<size_t>(k)];
                    if (midiNote >= 0 && midiNote < NUM_MIDI_NOTES)
                    {
                        // Use the persistent phase for this MIDI note
                        quantizedPhase[static_cast<size_t>(k)] = midiPhaseAccumulators[static_cast<size_t>(midiNote)];
                    }
                    else
                    {
                        // Fallback: use strongest contributor's phase
                        quantizedPhase[static_cast<size_t>(k)] = strongestContributorPhase[static_cast<size_t>(k)];
                    }
                }
                else
                {
                    // This bin was NOT remapped (source == target)
                    // Preserve the input phase (which may have phase vocoder coherence)
                    quantizedPhase[static_cast<size_t>(k)] = strongestContributorPhase[static_cast<size_t>(k)];
                }
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
