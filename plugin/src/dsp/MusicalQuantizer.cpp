#include "MusicalQuantizer.h"
#include <algorithm>
#include <cmath>

namespace fshift
{

// Two-pi constant for phase calculations
static constexpr float TWO_PI = 6.283185307179586f;
static constexpr float PI = 3.14159265359f;

// Quick-win #3: fraction of the measured (input) phase to KEEP even at full Quantize,
// so remapped bins never lock to a perfectly steady oscillator ramp (= a pure sine).
// The pull toward the per-note phase accumulator is capped at (1 - PHASE_TEXTURE_RETAIN),
// leaving this much of the source bin's real, frame-to-frame phase wobble intact. That
// residual irregularity is what keeps quantized output sounding like the source instead
// of a glassy sine bank. 0.0f = original behaviour (100% lock); 0.15-0.35 is the useful
// range; higher = noisier / less "in-tune". Promote to an APVTS knob if it earns its keep.
static constexpr float PHASE_TEXTURE_RETAIN = 0.20f;

// Peak-region snapping: how many bins on each side of a detected peak are treated as part
// of its main lobe and shifted with it (capped; the walk stops earlier at the lobe valley).
// A Hann main lobe at fftSize 4096 is only a few bins wide, so this is a generous cap.
static constexpr int PEAK_REGION_HALF_WIDTH = 4;

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
    midiNoteActivity.fill(0.0f);
    peakPrevEnergy = 0.0f;
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

void MusicalQuantizer::setActiveNotes(const std::array<bool, 12>& notes)
{
    scaleDegrees.clear();
    for (int i = 0; i < 12; ++i)
    {
        if (notes[i])
            scaleDegrees.push_back(i);
    }
    // If no notes selected, treat as chromatic (pass-through)
    if (scaleDegrees.empty())
    {
        for (int i = 0; i < 12; ++i)
            scaleDegrees.push_back(i);
    }
    rootMidi = 60;  // C4
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

// Pre-computed log values for band centers (avoids log() calls in real-time)
static constexpr float ENVELOPE_BAND_LOG_CENTERS[48] = {
    2.996f, 3.140f, 3.285f, 3.427f, 3.572f, 3.716f, 3.861f, 4.005f, 4.150f, 4.293f, 4.438f, 4.582f,
    4.727f, 4.871f, 5.015f, 5.158f, 5.303f, 5.447f, 5.591f, 5.735f, 5.879f, 6.023f, 6.167f, 6.312f,
    6.456f, 6.600f, 6.744f, 6.888f, 7.032f, 7.176f, 7.320f, 7.464f, 7.607f, 7.752f, 7.896f, 8.040f,
    8.184f, 8.328f, 8.472f, 8.616f, 8.759f, 8.903f, 9.048f, 9.191f, 9.336f, 9.479f, 9.624f, 9.768f
};

// ========== OPTIMIZED ENVELOPE FUNCTIONS ==========

void MusicalQuantizer::buildEnvelopeLookupTables(double sampleRate, int fftSize) const
{
    // Skip if already built for these parameters
    if (fftSize == cachedFftSizeForLookup && sampleRate == cachedSampleRateForLookup)
        return;

    cachedFftSizeForLookup = fftSize;
    cachedSampleRateForLookup = sampleRate;

    int numBins = fftSize / 2 + 1;
    float binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    float nyquist = static_cast<float>(sampleRate) / 2.0f;

    // Build bin-to-band lookup table
    binToBandLookup.resize(static_cast<size_t>(numBins));
    for (int k = 0; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq < ENVELOPE_BAND_CENTERS[0] || binFreq >= nyquist)
        {
            binToBandLookup[static_cast<size_t>(k)] = -1;  // Invalid
            continue;
        }

        // Find closest band using pre-computed log values
        float logFreq = std::log(binFreq);
        int closestBand = 0;
        float minDist = std::abs(logFreq - ENVELOPE_BAND_LOG_CENTERS[0]);

        for (int band = 1; band < NUM_ENVELOPE_BANDS; ++band)
        {
            if (ENVELOPE_BAND_CENTERS[band] >= nyquist)
                break;
            float dist = std::abs(logFreq - ENVELOPE_BAND_LOG_CENTERS[band]);
            if (dist < minDist)
            {
                minDist = dist;
                closestBand = band;
            }
        }
        binToBandLookup[static_cast<size_t>(k)] = closestBand;
    }

    // Build band bin ranges for fast envelope capture
    bandBinRanges.resize(NUM_ENVELOPE_BANDS);
    for (int band = 0; band < NUM_ENVELOPE_BANDS; ++band)
    {
        float centerFreq = ENVELOPE_BAND_CENTERS[band];
        if (centerFreq >= nyquist)
        {
            bandBinRanges[static_cast<size_t>(band)] = {-1, -1};
            continue;
        }

        float lowFreq = centerFreq / 1.122f;
        float highFreq = std::min(centerFreq * 1.122f, nyquist);

        int lowBin = static_cast<int>(std::floor(lowFreq / binResolution));
        int highBin = static_cast<int>(std::ceil(highFreq / binResolution));
        lowBin = std::clamp(lowBin, 0, numBins - 1);
        highBin = std::clamp(highBin, 0, numBins - 1);

        bandBinRanges[static_cast<size_t>(band)] = {lowBin, highBin};
    }
}

std::vector<float> MusicalQuantizer::captureSpectralEnvelopeFast(
    const std::vector<float>& magnitude) const
{
    // OPTIMIZED: Uses pre-computed band bin ranges
    std::vector<float> envelope(NUM_ENVELOPE_BANDS, 0.0f);
    const int magSize = static_cast<int>(magnitude.size());

    for (int band = 0; band < NUM_ENVELOPE_BANDS; ++band)
    {
        auto [lowBin, highBin] = bandBinRanges[static_cast<size_t>(band)];
        if (lowBin < 0)
            continue;

        // Bounds check: clamp highBin to magnitude size
        highBin = std::min(highBin, magSize - 1);
        if (lowBin > highBin)
            continue;

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

void MusicalQuantizer::applySpectralEnvelopeFast(
    std::vector<float>& magnitude,
    const std::vector<float>& originalEnvelope,
    const std::vector<float>& postEnvelope,
    float preserveStrength) const
{
    // The PRESERVE slider exponentiates the per-band envelope correction:
    //   0%    -> exponent 0.0 -> bandRatio = 1.0          (no envelope correction)
    //   ~33%  -> exponent 1.0 -> bandRatio = rawRatio     (exact envelope match)
    //   100%  -> exponent 3.0 -> bandRatio = rawRatio^3   (triple-dB over-emphasis;
    //                                                      formants/transients get exaggerated)
    // The raw ratio (original / post) is clamped to ±12 dB before the pow, so the
    // worst-case band correction at slider=100% is bounded at ±36 dB.
    const float exponent = preserveStrength * 3.0f;

    constexpr float minRatio = 0.25f;  // raw cap -12dB
    constexpr float maxRatio = 4.0f;   // raw cap +12dB

    int numBins = static_cast<int>(magnitude.size());

    // Pre-compute ratios per band (48 divisions vs 2000+ bins)
    float bandRatios[NUM_ENVELOPE_BANDS];
    for (int band = 0; band < NUM_ENVELOPE_BANDS; ++band)
    {
        float originalVal = originalEnvelope[static_cast<size_t>(band)];
        float postVal = postEnvelope[static_cast<size_t>(band)];

        if (postVal < ENVELOPE_FLOOR) postVal = ENVELOPE_FLOOR;
        if (originalVal < ENVELOPE_FLOOR) originalVal = ENVELOPE_FLOOR;

        float rawRatio = std::clamp(originalVal / postVal, minRatio, maxRatio);
        bandRatios[band] = std::pow(rawRatio, exponent);
    }

    // Apply ratios using lookup table (single loop, no nested search)
    int lookupSize = static_cast<int>(binToBandLookup.size());
    for (int k = 1; k < numBins && k < lookupSize; ++k)
    {
        int band = binToBandLookup[static_cast<size_t>(k)];
        if (band >= 0)
        {
            magnitude[static_cast<size_t>(k)] *= bandRatios[band];
        }
    }
}

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
    // See applySpectralEnvelopeFast for the design rationale — same exponent-based
    // over-emphasis curve, here on the log-search fallback path.
    if (preserveStrength <= 0.0f)
        return;

    const float exponent = preserveStrength * 3.0f;
    constexpr float minRatio = 0.25f;
    constexpr float maxRatio = 4.0f;

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

        float rawRatio = std::clamp(originalVal / postVal, minRatio, maxRatio);
        magnitude[static_cast<size_t>(k)] *= std::pow(rawRatio, exponent);
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
    float shiftHz,
    float strength,
    const std::vector<float>* driftCents,
    const std::vector<float>* preShiftEnvelope,
    std::vector<float>* preEnvelopeMagnitudeOut)
{
    // Short-circuit only when both shift and snap are no-ops; either alone still needs the loop.
    if (strength <= 0.0f && std::abs(shiftHz) < 0.001f)
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

    // Only bail out when there is genuinely nothing to do: no scale snap AND no shift.
    // A nonzero shift with (near-)zero effective strength still needs the distribution loop
    // below, which applies the pure continuous shift (target freq == srcFreq == binFreq+shiftHz).
    // Returning unconditionally here used to make the Shift knob dead at Quantize 0% and caused
    // a pitch snap-back to the unshifted signal on transients (effectiveStrength -> 0 on attacks).
    if (effectiveStrength <= 0.001f && std::abs(shiftHz) < 0.001f)
    {
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

    // ================= Peak-region snapping + sines/noise split =================
    // Snapping every bin turns broadband/noise energy into a sparse line spectrum (the
    // "glassy sines"). Instead: detect tonal peaks, move each peak together with its
    // main-lobe skirt to the snapped scale frequency as a rigid unit, and let the
    // non-peak ("noise") bins pass straight through (scaled by noiseMix). The shared
    // downstream stages (phase continuity + PhaseTex, envelope) then apply as usual.
    if (peakSnapEnabled)
    {
        // 1. Peak detection: local maxima above a sensitivity-controlled threshold.
        float maxMag = 0.0f;
        for (int k = 1; k < numBins; ++k)
            maxMag = std::max(maxMag, magnitude[static_cast<size_t>(k)]);

        // The non-tonal "residual" body passes at a high fixed level so the source's broadband
        // timbre survives instead of collapsing to a sparse sine-bank (the "quieter sine-wavy"
        // complaint). The attack is NOT manufactured here anymore: a real transient the long STFT
        // window has already smeared cannot be rebuilt spectrally, so punch is reinjected from the
        // latency-aligned DRY signal in PluginProcessor's mix (transient-gated). noiseMix is now
        // the dry-injection amount ("Texture") consumed there, not a residual scaler.
        const float effNoiseMix = 0.85f;

        // sensitivity 0 -> only peaks within ~18 dB of the max; 1 -> down to ~72 dB.
        float thresholdDb = -18.0f - peakSnapSensitivity * 54.0f;
        float peakThreshold = maxMag * std::pow(10.0f, thresholdDb / 20.0f);
        float stickyThreshold = peakThreshold * 0.7f;  // tighter existence-hysteresis so tails let go sooner

        std::vector<bool> isTonal(static_cast<size_t>(numBins), false);
        std::vector<int> peakBins;
        for (int k = 1; k < numBins - 1; ++k)
        {
            float m = magnitude[static_cast<size_t>(k)];
            bool localMax = (m >= magnitude[static_cast<size_t>(k - 1)] &&
                             m >  magnitude[static_cast<size_t>(k + 1)]);
            if (!localMax)
                continue;
            if (m > peakThreshold)
            {
                peakBins.push_back(k);  // clear peak
            }
            else if (m > stickyThreshold)
            {
                // Existence hysteresis: keep a borderline peak alive if its scale note was
                // recently active, so peaks don't flicker in/out frame-to-frame (the chatter).
                float f = static_cast<float>(k) * binResolution + shiftHz;
                if (f > 0.0f)
                {
                    int nm = tuning::quantizeToScale(tuning::freqToMidi(f), rootMidi, scaleDegrees);
                    if (nm >= 0 && nm < NUM_MIDI_NOTES && midiNoteActivity[static_cast<size_t>(nm)] > 0.3f)
                        peakBins.push_back(k);
                }
            }
        }

        // 2. Mark each peak's main lobe (walk out to the valley, capped) as tonal.
        for (int p : peakBins)
        {
            isTonal[static_cast<size_t>(p)] = true;
            for (int k = p + 1; k < numBins && (k - p) <= PEAK_REGION_HALF_WIDTH; ++k)
            {
                if (magnitude[static_cast<size_t>(k)] > magnitude[static_cast<size_t>(k - 1)])
                    break;  // rising again -> past the lobe edge
                isTonal[static_cast<size_t>(k)] = true;
            }
            for (int k = p - 1; k >= 1 && (p - k) <= PEAK_REGION_HALF_WIDTH; --k)
            {
                if (magnitude[static_cast<size_t>(k)] > magnitude[static_cast<size_t>(k + 1)])
                    break;
                isTonal[static_cast<size_t>(k)] = true;
            }
        }

        // 3. Noise residual: non-tonal bins pass through in place, scaled by noiseMix.
        //    Left un-remapped so the phase stage keeps their original (textured) phase.
        for (int k = 1; k < numBins; ++k)
        {
            if (!isTonal[static_cast<size_t>(k)])
            {
                float contrib = magnitude[static_cast<size_t>(k)] * effNoiseMix;
                quantizedMagnitude[static_cast<size_t>(k)] += contrib;
                if (contrib > maxMagnitudeAtBin[static_cast<size_t>(k)])
                {
                    maxMagnitudeAtBin[static_cast<size_t>(k)] = contrib;
                    strongestContributorPhase[static_cast<size_t>(k)] = phase[static_cast<size_t>(k)];
                }
            }
        }

        // 4. Tonal peaks: shift each whole lobe to the snapped scale frequency as a unit.
        for (int p : peakBins)
        {
            // Refine true peak frequency via parabolic interpolation on log-magnitude.
            float frac = 0.0f;
            {
                float a = std::log(magnitude[static_cast<size_t>(p - 1)] + 1e-12f);
                float b = std::log(magnitude[static_cast<size_t>(p)]     + 1e-12f);
                float c = std::log(magnitude[static_cast<size_t>(p + 1)] + 1e-12f);
                float denom = a - 2.0f * b + c;
                if (std::abs(denom) > 1e-12f)
                    frac = std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f);
            }
            float trueFreq = (static_cast<float>(p) + frac) * binResolution;
            float srcFreq = trueFreq + shiftHz;
            if (srcFreq <= 0.0f)
                continue;

            // Snapped target frequency (blended by effectiveStrength) and its scale note.
            float snappedFreq = quantizeFrequency(srcFreq, effectiveStrength);
            int snappedMidi = tuning::quantizeToScale(tuning::freqToMidi(srcFreq), rootMidi, scaleDegrees);

            if (driftCents != nullptr && static_cast<size_t>(p) < driftCents->size())
                snappedFreq = applyDriftCents(snappedFreq, (*driftCents)[static_cast<size_t>(p)]);

            int delta = static_cast<int>(std::round(snappedFreq / binResolution)) - p;

            // Contiguous tonal run (lobe) around p, capped at the region half-width.
            int lo = p, hi = p;
            while (lo - 1 >= 1 && isTonal[static_cast<size_t>(lo - 1)] && (p - (lo - 1)) <= PEAK_REGION_HALF_WIDTH) --lo;
            while (hi + 1 < numBins && isTonal[static_cast<size_t>(hi + 1)] && ((hi + 1) - p) <= PEAK_REGION_HALF_WIDTH) ++hi;

            for (int k = lo; k <= hi; ++k)
            {
                int outk = k + delta;
                if (outk < 1 || outk >= numBins)
                    continue;
                float contrib = magnitude[static_cast<size_t>(k)];
                quantizedMagnitude[static_cast<size_t>(outk)] += contrib;
                contributorCount[static_cast<size_t>(outk)]++;
                if (outk != k)
                    binWasRemapped[static_cast<size_t>(outk)] = true;
                if (contrib > maxMagnitudeAtBin[static_cast<size_t>(outk)])
                {
                    maxMagnitudeAtBin[static_cast<size_t>(outk)] = contrib;
                    strongestContributorPhase[static_cast<size_t>(outk)] = phase[static_cast<size_t>(k)];
                }
                // Tie the moved peak centre to its scale note so the phase stage locks it.
                // (A flat-phase lock of the whole lobe was tried to chase a smooth harmonizer
                // sound; it didn't help the inherent overlap-add AM and risked glassiness, and
                // that goal was dropped in favour of Spectral-as-textural-effect — so reverted.)
                if (k == p && snappedMidi >= 0 && snappedMidi < NUM_MIDI_NOTES)
                {
                    targetMidiNotes[static_cast<size_t>(outk)] = snappedMidi;
                    midiNoteMagnitude[static_cast<size_t>(snappedMidi)] += contrib;
                    binWasRemapped[static_cast<size_t>(outk)] = true;
                }
            }
        }

        // Update per-note activity for the existence hysteresis: notes that received energy
        // this frame are fully active; the rest decay, so a note stays "sticky" for a few
        // frames after it drops out — this is what stops the peak flicker / chatter.
        for (int m = 0; m < NUM_MIDI_NOTES; ++m)
        {
            if (midiNoteMagnitude[static_cast<size_t>(m)] > MAGNITUDE_THRESHOLD)
                midiNoteActivity[static_cast<size_t>(m)] = 1.0f;
            else
                midiNoteActivity[static_cast<size_t>(m)] *= 0.45f;  // faster decay -> tonal tails stop ringing sooner
        }
    }
    else
    {
    // Calculate bin frequencies and target bins
    // Strategy A: Use weighted energy distribution to two nearest scale bins
    for (int k = 0; k < numBins; ++k)
    {
        float binFreq = static_cast<float>(k) * binResolution;
        if (binFreq <= 0.0f)
            continue;

        // Continuous frequency shift applied in the same pass as the scale snap. shiftHz is
        // real-valued and not rounded to a bin, so the two-nearest scale-tone weighting
        // (lowerWeight/upperWeight) crossfades smoothly as the Shift knob moves through scale
        // boundaries — the whole point of "in-key" Spectral mode.
        float srcFreq = binFreq + shiftHz;
        if (srcFreq <= 0.0f)
            continue;  // shift pushed this bin below DC — discard

        float sourceMag = magnitude[static_cast<size_t>(k)];
        float sourcePhase = phase[static_cast<size_t>(k)];

        // Two nearest scale freqs are based on the SHIFTED frequency, not the bin frequency.
        float lowerFreq, upperFreq, lowerWeight, upperWeight;
        findTwoNearestScaleFrequencies(srcFreq, lowerFreq, upperFreq, lowerWeight, upperWeight);

        // Blend between unsnapped shifted freq and snapped freq. At strength=0 you get a pure
        // continuous shift (no quantization); at strength=1, full scale snap. Transient
        // detection reduces effectiveStrength during attacks.
        float lowerTargetFreq = (1.0f - effectiveStrength) * srcFreq + effectiveStrength * lowerFreq;
        float upperTargetFreq = (1.0f - effectiveStrength) * srcFreq + effectiveStrength * upperFreq;

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
        else if (upperWeight > 0.001f)
        {
            // lowerBin == upperBin: both targets rounded to the same bin (input frequency sits
            // very close to a scale tone). Add the upper-weight contribution to the same bin so
            // total source-bin energy is preserved (lowerWeight + upperWeight = 1). Tracking
            // variables were already set by the lower block; phase is identical (same source
            // bin), so no other updates are needed.
            quantizedMagnitude[static_cast<size_t>(lowerBin)] += sourceMag * upperWeight;
        }
    }
    }  // end else — legacy per-bin distribution path (indentation kept to minimise diff)

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
    // This reduces sharp spectral peaks/pits that can cause resonance.
    // Skipped in peak-snap mode: we deliberately preserve each lobe's shape and the
    // untouched noise residual, so blurring would work against both.
    if (!peakSnapEnabled)
        applyMagnitudeSmoothing(quantizedMagnitude);

    // Phase 2A.2: Calculate total energy AFTER quantization+smoothing and normalize.
    // Skipped in peak-snap mode: noiseMix intentionally rebalances tonal-vs-noise energy,
    // and renormalising to the input energy would undo it (tonal energy is already
    // preserved 1:1 by the rigid lobe move).
    if (!peakSnapEnabled)
    {
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
    }

    // Hand back the energy-normalized magnitude BEFORE spectral-envelope preservation.
    // This is the "shift+quantize only" spectrum, whose energy matches the input — the
    // right thing to recirculate in a feedback loop. The per-band envelope make-up gain
    // applied just below (up to +36 dB/band) must stay on the audible output only, or it
    // compounds around the loop and runs away at high shift + Envelope.
    if (preEnvelopeMagnitudeOut != nullptr)
        *preEnvelopeMagnitudeOut = quantizedMagnitude;

    // Phase 2B+ OPTIMIZED: Apply spectral envelope preservation
    // Uses pre-computed lookup tables to avoid expensive log() calls
    if (preserveAmount > 0.0f && !originalEnvelope.empty())
    {
        // Build lookup tables if needed (only when FFT size or sample rate changes)
        buildEnvelopeLookupTables(sampleRate, fftSize);

        // Capture post-quantization envelope using fast method
        std::vector<float> postEnvelope = captureSpectralEnvelopeFast(quantizedMagnitude);

        // Apply envelope correction using fast method (single lookup per bin)
        applySpectralEnvelopeFast(quantizedMagnitude, originalEnvelope, postEnvelope, preserveAmount);
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
                        // At strength=1: pull (1 - PHASE_TEXTURE_RETAIN) toward the phase accumulator,
                        //   i.e. deliberately STOP short of a full 100% lock so a slice of the source
                        //   bin's real phase texture survives (quick-win #3 — de-glass the sines).
                        // This allows Enhanced Mode to affect the non-quantized portion
                        // Also respects transient detection which reduces quantization during attacks

                        // Phase interpolation needs to handle wraparound
                        // Use circular interpolation to avoid jumps at +/- PI boundary
                        float phaseDiff = quantizedPhaseValue - inputPhase;

                        // Normalize phase difference to [-PI, PI]
                        while (phaseDiff > PI) phaseDiff -= TWO_PI;
                        while (phaseDiff < -PI) phaseDiff += TWO_PI;

                        // Cap the pull toward the steady accumulator so we never fully lock:
                        // phaseLockAmount peaks at (1 - retain) when effectiveStrength = 1.
                        // In peak-snap mode the noise path already supplies natural texture, so
                        // the snapped tonal peaks lock FULLY to the clean accumulator (retain 0,
                        // no PhaseTex) — steady in-tune tones instead of the fizzy/gritty phase
                        // jitter PhaseTex causes on already-moving peaks. Per-bin mode keeps it.
                        // In peak-snap, a full lock (retain 0) turned every snapped tone into a
                        // dead-steady coherent sine that rings out as a "phasey/feedbacky" tail.
                        // Retain a slice of the source's real phase so tails decorrelate and decay
                        // with the source instead of sustaining.
                        float retain = peakSnapEnabled ? 0.20f : PHASE_TEXTURE_RETAIN;
                        float phaseLockAmount = effectiveStrength * (1.0f - retain);
                        outputPhase = inputPhase + phaseLockAmount * phaseDiff;

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
