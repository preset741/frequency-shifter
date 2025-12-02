#include "MusicalQuantizer.h"
#include <algorithm>
#include <cmath>

namespace fshift
{

MusicalQuantizer::MusicalQuantizer(int rootMidi, ScaleType scaleType)
    : rootMidi(rootMidi),
      scaleType(scaleType),
      scaleDegrees(fshift::getScaleDegrees(scaleType))
{
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
    std::vector<float> quantizedMagnitude(numBins, 0.0f);
    std::vector<float> quantizedPhase(numBins, 0.0f);

    // Track which bins have received energy and their max magnitude
    std::vector<float> maxMagnitudeAtBin(numBins, 0.0f);

    // Calculate bin frequencies and target bins
    for (int k = 0; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq <= 0.0f)
            continue;

        // Quantize bin frequency
        float quantizedFreq = quantizeFrequency(binFreq, strength);

        // Apply drift if provided
        if (driftCents != nullptr && static_cast<size_t>(k) < driftCents->size())
        {
            quantizedFreq = applyDriftCents(quantizedFreq, (*driftCents)[static_cast<size_t>(k)]);
        }

        // Calculate target bin
        int targetBin = static_cast<int>(std::round(quantizedFreq / binResolution));
        targetBin = std::clamp(targetBin, 0, numBins - 1);

        // Accumulate magnitude (energy conservation)
        quantizedMagnitude[targetBin] += magnitude[k];

        // Use phase from strongest contributor
        if (magnitude[k] > maxMagnitudeAtBin[targetBin])
        {
            maxMagnitudeAtBin[targetBin] = magnitude[k];
            quantizedPhase[targetBin] = phase[k];
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
