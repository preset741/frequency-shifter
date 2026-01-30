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

// Phase 2B: Log-spaced band center frequencies (Hz)
// 48 bands from 20Hz to 20kHz at ~1/5 octave resolution
// Geometrically spaced: f[i] = 20 * (20000/20)^(i/47) = 20 * 1000^(i/47)
static constexpr float ENVELOPE_BAND_CENTERS[48] = {
    20.0f, 23.1f, 26.7f, 30.8f, 35.6f, 41.1f, 47.5f, 54.9f, 63.4f, 73.2f, 84.6f, 97.7f,      // 0-11
    112.9f, 130.4f, 150.6f, 173.9f, 200.9f, 232.0f, 268.0f, 309.5f, 357.5f, 412.9f, 476.8f, 550.7f,  // 12-23
    636.0f, 734.6f, 848.4f, 979.8f, 1131.5f, 1306.8f, 1509.2f, 1743.1f, 2013.2f, 2325.0f, 2685.2f, 3101.2f,  // 24-35
    3581.2f, 4135.6f, 4776.0f, 5515.7f, 6370.1f, 7356.8f, 8496.6f, 9812.3f, 11331.3f, 13085.9f, 15112.5f, 17453.4f  // 36-47
};

std::vector<float> MusicalQuantizer::captureSpectralEnvelope(
    const std::vector<float>& magnitude,
    double sampleRate,
    int fftSize) const
{
    // Phase 2B.1: Capture spectral envelope at ~1/6 octave resolution
    // Use RMS energy per band for stable envelope estimation

    std::vector<float> envelope(NUM_ENVELOPE_BANDS, 0.0f);
    int numBins = static_cast<int>(magnitude.size());
    float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    float nyquist = static_cast<float>(sampleRate) / 2.0f;

    for (int band = 0; band < NUM_ENVELOPE_BANDS; ++band)
    {
        float centerFreq = ENVELOPE_BAND_CENTERS[band];
        if (centerFreq >= nyquist)
            break;

        // ~1/6 octave bandwidth: factor of 2^(1/6) ≈ 1.122
        float lowFreq = centerFreq / 1.122f;
        float highFreq = centerFreq * 1.122f;

        // Clamp to nyquist
        highFreq = std::min(highFreq, nyquist);

        // Convert to bin indices
        int lowBin = static_cast<int>(std::floor(lowFreq / binResolution));
        int highBin = static_cast<int>(std::ceil(highFreq / binResolution));
        lowBin = std::clamp(lowBin, 0, numBins - 1);
        highBin = std::clamp(highBin, 0, numBins - 1);

        // Calculate RMS energy in this band (more stable than peak)
        float sumSquares = 0.0f;
        int binCount = 0;
        for (int k = lowBin; k <= highBin; ++k)
        {
            float mag = magnitude[static_cast<size_t>(k)];
            sumSquares += mag * mag;
            binCount++;
        }

        // RMS = sqrt(sum of squares / count)
        if (binCount > 0)
        {
            envelope[static_cast<size_t>(band)] = std::sqrt(sumSquares / static_cast<float>(binCount));
        }
    }

    return envelope;
}

void MusicalQuantizer::applySpectralEnvelope(
    std::vector<float>& magnitude,
    const std::vector<float>& originalEnvelope,
    double sampleRate,
    int fftSize,
    float preserveStrength) const
{
    // Phase 2B+ Enhanced: More extreme correction at high settings
    if (preserveStrength <= 0.0f)
        return;

    // Non-linear scaling: make top end more aggressive
    // pow(x, 0.7) gives gentler curve with more effect at high settings
    float effectiveStrength = std::pow(preserveStrength, 0.7f);

    // Dynamic clamp based on preserve setting:
    // At 50%: ±18dB (ratio 0.125 to 8)
    // At 100%: ±48dB (ratio 0.004 to 256) - nearly unclamped
    float clampDb = 18.0f + (effectiveStrength * 30.0f);  // 18 to 48 dB
    float minRatio = std::pow(10.0f, -clampDb / 20.0f);
    float maxRatio = std::pow(10.0f, clampDb / 20.0f);

    int numBins = static_cast<int>(magnitude.size());
    float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    float nyquist = static_cast<float>(sampleRate) / 2.0f;

    // First capture the post-quantization envelope
    std::vector<float> postEnvelope = captureSpectralEnvelope(magnitude, sampleRate, fftSize);

    // For each bin, find its envelope band and apply correction
    for (int k = 1; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq >= nyquist || binFreq < ENVELOPE_BAND_CENTERS[0])
            continue;

        // Find which band this bin belongs to using binary-search-like approach
        // Since bands are logarithmically spaced, use log-frequency for lookup
        float logFreq = std::log(binFreq);
        int closestBand = 0;
        float minDist = std::abs(logFreq - std::log(ENVELOPE_BAND_CENTERS[0]));

        for (int band = 1; band < NUM_ENVELOPE_BANDS; ++band)
        {
            if (ENVELOPE_BAND_CENTERS[band] >= nyquist)
                break;
            float dist = std::abs(logFreq - std::log(ENVELOPE_BAND_CENTERS[band]));
            if (dist < minDist)
            {
                minDist = dist;
                closestBand = band;
            }
        }

        // Calculate ratio: original / post-quantization
        float originalVal = originalEnvelope[static_cast<size_t>(closestBand)];
        float postVal = postEnvelope[static_cast<size_t>(closestBand)];

        // Floor threshold to avoid division by near-zero
        if (postVal < ENVELOPE_FLOOR)
            postVal = ENVELOPE_FLOOR;
        if (originalVal < ENVELOPE_FLOOR)
            originalVal = ENVELOPE_FLOOR;

        float ratio = originalVal / postVal;

        // Apply dynamic clamp
        ratio = std::clamp(ratio, minRatio, maxRatio);

        // Apply ratio scaled by effectiveStrength (non-linear)
        // At 0%: no correction (ratio of 1.0)
        // At 100%: full correction with expanded dynamic range
        float blendedRatio = 1.0f + effectiveStrength * (ratio - 1.0f);

        magnitude[static_cast<size_t>(k)] *= blendedRatio;
    }
}

float MusicalQuantizer::detectTransient(const std::vector<float>& magnitude)
{
    // Phase 2B.2: Detect if current frame is a transient
    // Compare total spectral energy to previous frame

    // Calculate current frame energy
    float currentEnergy = 0.0f;
    for (const float& mag : magnitude)
    {
        currentEnergy += mag * mag;
    }

    // Calculate energy ratio
    float ratio = 1.0f;
    if (previousFrameEnergy > ENVELOPE_FLOOR)
    {
        ratio = currentEnergy / previousFrameEnergy;
    }

    // Store for next frame
    previousFrameEnergy = currentEnergy;

    // Convert sensitivity (0-100%) to threshold ratio
    // 0% = 3.0x ratio (less sensitive)
    // 100% = 1.2x ratio (more sensitive)
    // Default 50% = 1.5x ratio
    float thresholdRatio = 3.0f - transientSensitivity * 1.8f;  // Linear interp from 3.0 to 1.2

    // Check if this is a transient
    bool isTransient = (ratio > thresholdRatio);

    // Update ramp value
    if (isTransient)
    {
        // Snap to 1.0 on transient detection
        transientRampValue = 1.0f;
    }
    else
    {
        // Decay over TRANSIENT_RAMP_FRAMES
        float decayRate = 1.0f / static_cast<float>(TRANSIENT_RAMP_FRAMES);
        transientRampValue = std::max(0.0f, transientRampValue - decayRate);
    }

    // Return transient factor scaled by transientAmount
    return transientRampValue * transientAmount;
}

std::pair<std::vector<float>, std::vector<float>> MusicalQuantizer::quantizeSpectrum(
    const std::vector<float>& magnitude,
    const std::vector<float>& phase,
    double sampleRate,
    int fftSize,
    float strength,
    const std::vector<float>* driftCents,
    const std::vector<float>* preShiftEnvelope)
{
    if (strength <= 0.0f)
        return { magnitude, phase };

    strength = std::clamp(strength, 0.0f, 1.0f);

    // Phase 2B.1: Use pre-shift envelope if provided (from INPUT before any processing)
    // Otherwise capture from current magnitude (less accurate but backward compatible)
    std::vector<float> originalEnvelope;
    if (preserveAmount > 0.0f)
    {
        if (preShiftEnvelope != nullptr && !preShiftEnvelope->empty())
        {
            // Use the pre-captured envelope from INPUT signal (before shift)
            originalEnvelope = *preShiftEnvelope;
        }
        else
        {
            // Fallback: capture from current magnitude (post-shift, less accurate)
            originalEnvelope = captureSpectralEnvelope(magnitude, sampleRate, fftSize);
        }
    }

    // Phase 2B.2: Detect transient and reduce quantization strength if needed
    float transientFactor = 0.0f;
    if (transientAmount > 0.0f)
    {
        transientFactor = detectTransient(magnitude);
    }

    // Reduce quantization strength during transients
    // transientFactor = 1.0 means full transient, reduce strength toward 0
    float effectiveStrength = strength * (1.0f - transientFactor);

    // If effective strength is very low, just return original
    if (effectiveStrength <= 0.001f)
    {
        // Still need to update transient detection state for next frame
        return { magnitude, phase };
    }

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

        // Interpolate based on effectiveStrength (blend between original and quantized)
        // This respects transient detection which may reduce quantization during attacks
        float lowerTargetFreq = (1.0f - effectiveStrength) * binFreq + effectiveStrength * lowerFreq;
        float upperTargetFreq = (1.0f - effectiveStrength) * binFreq + effectiveStrength * upperFreq;

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

    // Phase 2B+ Enhanced: Apply spectral envelope preservation
    // Use high-resolution envelope (96 bands) when PRESERVE > 75% for tighter matching
    if (preserveAmount > 0.0f && !originalEnvelope.empty())
    {
        if (preserveAmount > 0.75f)
        {
            // High resolution mode: recapture at higher resolution and apply
            std::vector<float> hiResOriginal = captureSpectralEnvelopeHighRes(magnitude, sampleRate, fftSize);
            applySpectralEnvelopeHighRes(quantizedMagnitude, hiResOriginal, sampleRate, fftSize, preserveAmount);
        }
        else
        {
            // Standard resolution mode
            applySpectralEnvelope(quantizedMagnitude, originalEnvelope, sampleRate, fftSize, preserveAmount);
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

                        // FIX: Blend between input phase and quantized phase based on effectiveStrength
                        // At strength=0: 100% input phase (phase vocoder if enabled)
                        // At strength=1: 100% quantized phase (phase accumulator)
                        // This allows Enhanced Mode to affect the non-quantized portion
                        // Also respects transient detection which reduces quantization during attacks

                        // Phase interpolation needs to handle wraparound
                        // Use circular interpolation to avoid jumps at +/- PI boundary
                        float phaseDiff = quantizedPhaseValue - inputPhase;

                        // Normalize phase difference to [-PI, PI]
                        while (phaseDiff > PI) phaseDiff -= TWO_PI;
                        while (phaseDiff < -PI) phaseDiff += TWO_PI;

                        // Interpolate: inputPhase + effectiveStrength * (difference)
                        outputPhase = inputPhase + effectiveStrength * phaseDiff;

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

// Phase 2B+ High-resolution (96 bands) envelope band centers
// For PRESERVE > 75%, using ~1/10 octave resolution for tighter spectral matching
// Geometrically spaced: f[i] = 20 * (20000/20)^(i/95) = 20 * 1000^(i/95)
static constexpr float ENVELOPE_BAND_CENTERS_HIGH_RES[96] = {
    // Bands 0-23: 20-120 Hz
    20.0f, 21.5f, 23.1f, 24.8f, 26.7f, 28.7f, 30.8f, 33.1f, 35.6f, 38.3f, 41.1f, 44.2f,
    47.5f, 51.1f, 54.9f, 59.0f, 63.4f, 68.1f, 73.2f, 78.7f, 84.6f, 90.9f, 97.7f, 105.0f,
    // Bands 24-47: 113-640 Hz
    112.9f, 121.3f, 130.4f, 140.1f, 150.6f, 161.9f, 173.9f, 186.9f, 200.9f, 215.9f, 232.0f, 249.4f,
    268.0f, 288.1f, 309.5f, 332.7f, 357.5f, 384.3f, 412.9f, 443.7f, 476.8f, 512.5f, 550.7f, 591.9f,
    // Bands 48-71: 636-3600 Hz
    636.0f, 683.5f, 734.6f, 789.5f, 848.4f, 911.7f, 979.8f, 1053.0f, 1131.5f, 1216.2f, 1306.8f, 1404.3f,
    1509.2f, 1621.9f, 1743.1f, 1873.3f, 2013.2f, 2163.6f, 2325.0f, 2498.5f, 2685.2f, 2886.0f, 3101.2f, 3332.2f,
    // Bands 72-95: 3580-20000 Hz
    3580.0f, 3847.3f, 4135.4f, 4445.0f, 4777.8f, 5135.3f, 5519.6f, 5932.4f, 6376.1f, 6853.0f, 7365.7f, 7917.0f,
    8509.7f, 9147.0f, 9832.0f, 10568.0f, 11358.9f, 12208.5f, 13121.0f, 14101.0f, 15153.0f, 16282.4f, 17494.8f, 18796.0f
};

std::vector<float> MusicalQuantizer::captureSpectralEnvelopeHighRes(
    const std::vector<float>& magnitude,
    double sampleRate,
    int fftSize) const
{
    // High-resolution envelope capture (~1/10 octave resolution)
    // Uses 96 bands for tighter spectral matching when PRESERVE > 75%

    std::vector<float> envelope(NUM_ENVELOPE_BANDS_HIGH_RES, 0.0f);
    int numBins = static_cast<int>(magnitude.size());
    float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    float nyquist = static_cast<float>(sampleRate) / 2.0f;

    for (int band = 0; band < NUM_ENVELOPE_BANDS_HIGH_RES; ++band)
    {
        float centerFreq = ENVELOPE_BAND_CENTERS_HIGH_RES[band];
        if (centerFreq >= nyquist)
            break;

        // ~1/10 octave bandwidth: factor of 2^(1/10) ≈ 1.072
        float lowFreq = centerFreq / 1.072f;
        float highFreq = centerFreq * 1.072f;

        // Clamp to nyquist
        highFreq = std::min(highFreq, nyquist);

        // Convert to bin indices
        int lowBin = static_cast<int>(std::floor(lowFreq / binResolution));
        int highBin = static_cast<int>(std::ceil(highFreq / binResolution));
        lowBin = std::clamp(lowBin, 0, numBins - 1);
        highBin = std::clamp(highBin, 0, numBins - 1);

        // Calculate RMS energy in this band
        float sumSquares = 0.0f;
        int binCount = 0;
        for (int k = lowBin; k <= highBin; ++k)
        {
            float mag = magnitude[static_cast<size_t>(k)];
            sumSquares += mag * mag;
            binCount++;
        }

        if (binCount > 0)
        {
            envelope[static_cast<size_t>(band)] = std::sqrt(sumSquares / static_cast<float>(binCount));
        }
    }

    return envelope;
}

void MusicalQuantizer::applySpectralEnvelopeHighRes(
    std::vector<float>& magnitude,
    const std::vector<float>& originalEnvelope,
    double sampleRate,
    int fftSize,
    float preserveStrength) const
{
    // High-resolution envelope application for PRESERVE > 75%
    // Uses 96 bands for tighter spectral matching

    if (preserveStrength <= 0.0f)
        return;

    // Non-linear scaling: make top end more aggressive
    float effectiveStrength = std::pow(preserveStrength, 0.7f);

    // At high preserve (> 75%), use very wide clamp range (nearly unclamped)
    // Map 75%-100% to ±36dB to ±60dB
    float clampDb = 36.0f + ((preserveStrength - 0.75f) / 0.25f) * 24.0f;  // 36 to 60 dB
    float minRatio = std::pow(10.0f, -clampDb / 20.0f);
    float maxRatio = std::pow(10.0f, clampDb / 20.0f);

    int numBins = static_cast<int>(magnitude.size());
    float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    float nyquist = static_cast<float>(sampleRate) / 2.0f;

    // Capture post-quantization envelope at high resolution
    std::vector<float> postEnvelope = captureSpectralEnvelopeHighRes(magnitude, sampleRate, fftSize);

    // Apply correction per bin
    for (int k = 1; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq >= nyquist || binFreq < ENVELOPE_BAND_CENTERS_HIGH_RES[0])
            continue;

        // Find closest band using log-frequency lookup
        float logFreq = std::log(binFreq);
        int closestBand = 0;
        float minDist = std::abs(logFreq - std::log(ENVELOPE_BAND_CENTERS_HIGH_RES[0]));

        for (int band = 1; band < NUM_ENVELOPE_BANDS_HIGH_RES; ++band)
        {
            if (ENVELOPE_BAND_CENTERS_HIGH_RES[band] >= nyquist)
                break;
            float dist = std::abs(logFreq - std::log(ENVELOPE_BAND_CENTERS_HIGH_RES[band]));
            if (dist < minDist)
            {
                minDist = dist;
                closestBand = band;
            }
        }

        // Calculate correction ratio
        float originalVal = originalEnvelope[static_cast<size_t>(closestBand)];
        float postVal = postEnvelope[static_cast<size_t>(closestBand)];

        if (postVal < ENVELOPE_FLOOR)
            postVal = ENVELOPE_FLOOR;
        if (originalVal < ENVELOPE_FLOOR)
            originalVal = ENVELOPE_FLOOR;

        float ratio = originalVal / postVal;
        ratio = std::clamp(ratio, minRatio, maxRatio);

        // Apply with non-linear effective strength
        float blendedRatio = 1.0f + effectiveStrength * (ratio - 1.0f);
        magnitude[static_cast<size_t>(k)] *= blendedRatio;
    }
}

} // namespace fshift
