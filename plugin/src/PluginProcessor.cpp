#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/Scales.h"

FrequencyShifterProcessor::FrequencyShifterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("FrequencyShifter"), createParameterLayout())
{
    // Add parameter listeners
    parameters.addParameterListener(PARAM_SHIFT_HZ, this);
    parameters.addParameterListener(PARAM_QUANTIZE_STRENGTH, this);
    parameters.addParameterListener(PARAM_ROOT_NOTE, this);
    parameters.addParameterListener(PARAM_SCALE_TYPE, this);
    parameters.addParameterListener(PARAM_DRY_WET, this);
    parameters.addParameterListener(PARAM_PHASE_VOCODER, this);
    parameters.addParameterListener(PARAM_SMEAR, this);
    parameters.addParameterListener(PARAM_DRIFT_AMOUNT, this);
    parameters.addParameterListener(PARAM_DRIFT_RATE, this);
    parameters.addParameterListener(PARAM_DRIFT_MODE, this);
    parameters.addParameterListener(PARAM_STOCHASTIC_TYPE, this);
    parameters.addParameterListener(PARAM_STOCHASTIC_DENSITY, this);
    parameters.addParameterListener(PARAM_STOCHASTIC_SMOOTHNESS, this);
    parameters.addParameterListener(PARAM_MASK_ENABLED, this);
    parameters.addParameterListener(PARAM_MASK_MODE, this);
    parameters.addParameterListener(PARAM_MASK_LOW_FREQ, this);
    parameters.addParameterListener(PARAM_MASK_HIGH_FREQ, this);
    parameters.addParameterListener(PARAM_MASK_TRANSITION, this);
    parameters.addParameterListener(PARAM_DELAY_ENABLED, this);
    parameters.addParameterListener(PARAM_DELAY_TIME, this);
    parameters.addParameterListener(PARAM_DELAY_SYNC, this);
    parameters.addParameterListener(PARAM_DELAY_DIVISION, this);
    parameters.addParameterListener(PARAM_DELAY_SLOPE, this);
    parameters.addParameterListener(PARAM_DELAY_FEEDBACK, this);
    parameters.addParameterListener(PARAM_DELAY_DAMPING, this);
    parameters.addParameterListener(PARAM_DELAY_DIFFUSE, this);
    parameters.addParameterListener(PARAM_DELAY_MIX, this);
    parameters.addParameterListener(PARAM_DELAY_GAIN, this);
    parameters.addParameterListener(PARAM_PRESERVE, this);
    parameters.addParameterListener(PARAM_TRANSIENTS, this);
    parameters.addParameterListener(PARAM_SENSITIVITY, this);

    // Initialize quantizer with default scale (C Major)
    quantizer = std::make_unique<fshift::MusicalQuantizer>(60, fshift::ScaleType::Major);
}

FrequencyShifterProcessor::~FrequencyShifterProcessor()
{
    parameters.removeParameterListener(PARAM_SHIFT_HZ, this);
    parameters.removeParameterListener(PARAM_QUANTIZE_STRENGTH, this);
    parameters.removeParameterListener(PARAM_ROOT_NOTE, this);
    parameters.removeParameterListener(PARAM_SCALE_TYPE, this);
    parameters.removeParameterListener(PARAM_DRY_WET, this);
    parameters.removeParameterListener(PARAM_PHASE_VOCODER, this);
    parameters.removeParameterListener(PARAM_SMEAR, this);
    parameters.removeParameterListener(PARAM_DRIFT_AMOUNT, this);
    parameters.removeParameterListener(PARAM_DRIFT_RATE, this);
    parameters.removeParameterListener(PARAM_DRIFT_MODE, this);
    parameters.removeParameterListener(PARAM_STOCHASTIC_TYPE, this);
    parameters.removeParameterListener(PARAM_STOCHASTIC_DENSITY, this);
    parameters.removeParameterListener(PARAM_STOCHASTIC_SMOOTHNESS, this);
    parameters.removeParameterListener(PARAM_MASK_ENABLED, this);
    parameters.removeParameterListener(PARAM_MASK_MODE, this);
    parameters.removeParameterListener(PARAM_MASK_LOW_FREQ, this);
    parameters.removeParameterListener(PARAM_MASK_HIGH_FREQ, this);
    parameters.removeParameterListener(PARAM_MASK_TRANSITION, this);
    parameters.removeParameterListener(PARAM_DELAY_ENABLED, this);
    parameters.removeParameterListener(PARAM_DELAY_TIME, this);
    parameters.removeParameterListener(PARAM_DELAY_SYNC, this);
    parameters.removeParameterListener(PARAM_DELAY_DIVISION, this);
    parameters.removeParameterListener(PARAM_DELAY_SLOPE, this);
    parameters.removeParameterListener(PARAM_DELAY_FEEDBACK, this);
    parameters.removeParameterListener(PARAM_DELAY_DAMPING, this);
    parameters.removeParameterListener(PARAM_DELAY_DIFFUSE, this);
    parameters.removeParameterListener(PARAM_DELAY_MIX, this);
    parameters.removeParameterListener(PARAM_DELAY_GAIN, this);
    parameters.removeParameterListener(PARAM_PRESERVE, this);
    parameters.removeParameterListener(PARAM_TRANSIENTS, this);
    parameters.removeParameterListener(PARAM_SENSITIVITY, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout FrequencyShifterProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Frequency shift (-20000 to +20000 Hz) - covers full audible range
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_SHIFT_HZ, 1 },
        "Shift (Hz)",
        juce::NormalisableRange<float>(-20000.0f, 20000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Quantize strength (0-100%) with log scale for fine control near 0
    auto quantizeRange = juce::NormalisableRange<float>(0.0f, 100.0f,
        [](float start, float end, float normalised) {
            // Log scale: more resolution near 0
            return start + std::pow(normalised, 2.0f) * (end - start);
        },
        [](float start, float end, float value) {
            // Inverse: value to normalised
            return std::sqrt((value - start) / (end - start));
        },
        [](float start, float end, float value) {
            // Snap to 0.1 resolution
            return std::round(value * 10.0f) / 10.0f;
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_QUANTIZE_STRENGTH, 1 },
        "Quantize",
        quantizeRange,
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Root note (12 pitch classes only - octave is irrelevant for scale quantization)
    juce::StringArray noteNames{ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_ROOT_NOTE, 1 },
        "Root Note",
        noteNames,
        0));  // Default to C

    // Scale type
    juce::StringArray scaleNames;
    for (const auto& name : fshift::getScaleNames())
    {
        scaleNames.add(name);
    }
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_SCALE_TYPE, 1 },
        "Scale",
        scaleNames,
        0));  // Default to Major

    // Dry/Wet mix (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DRY_WET, 1 },
        "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Phase vocoder toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_PHASE_VOCODER, 1 },
        "Enhanced Mode",
        true));

    // SMEAR control (5-123ms) - replaces quality mode dropdown
    // Crossfades between FFT sizes for continuous latency/quality control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_SMEAR, 1 },
        "Smear",
        juce::NormalisableRange<float>(MIN_SMEAR_MS, MAX_SMEAR_MS, 0.1f),
        93.0f,  // Default to max quality (~93ms)
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Log scale toggle for frequency shift control
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_LOG_SCALE, 1 },
        "Log Scale",
        false));  // Default to linear

    // Drift amount (0-100%) - how much pitch drift to apply
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DRIFT_AMOUNT, 1 },
        "Drift",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Drift rate (0.1-10 Hz) - speed of pitch drift modulation
    auto driftRateRange = juce::NormalisableRange<float>(0.1f, 10.0f,
        [](float start, float end, float normalised) {
            // Log scale for rate
            return start * std::pow(end / start, normalised);
        },
        [](float start, float end, float value) {
            return std::log(value / start) / std::log(end / start);
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DRIFT_RATE, 1 },
        "Drift Rate",
        driftRateRange,
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Drift mode (LFO, Perlin, or Stochastic)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_DRIFT_MODE, 1 },
        "Drift Mode",
        juce::StringArray{ "LFO", "Perlin", "Stochastic" },
        0));  // Default to LFO

    // Stochastic type (Poisson, RandomWalk, JumpDiffusion)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_STOCHASTIC_TYPE, 1 },
        "Stochastic Type",
        juce::StringArray{ "Poisson", "Random Walk", "Jump Diffusion" },
        0));  // Default to Poisson

    // Stochastic density (0-100%) - how frequently events occur
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_STOCHASTIC_DENSITY, 1 },
        "Density",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Stochastic smoothness (0-100%) - sharp pops to slow swells
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_STOCHASTIC_SMOOTHNESS, 1 },
        "Smoothness",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // === Spectral Mask Parameters ===

    // Mask enabled toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_MASK_ENABLED, 1 },
        "Mask Enabled",
        false));

    // Mask mode (LowPass, HighPass, BandPass)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_MASK_MODE, 1 },
        "Mask Mode",
        juce::StringArray{ "Low Pass", "High Pass", "Band Pass" },
        2));  // Default to BandPass

    // Mask low frequency (20-20000 Hz, log scale)
    auto maskLowFreqRange = juce::NormalisableRange<float>(20.0f, 20000.0f,
        [](float start, float end, float normalised) {
            return start * std::pow(end / start, normalised);
        },
        [](float start, float end, float value) {
            return std::log(value / start) / std::log(end / start);
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_MASK_LOW_FREQ, 1 },
        "Mask Low",
        maskLowFreqRange,
        200.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Mask high frequency (20-20000 Hz, log scale)
    auto maskHighFreqRange = juce::NormalisableRange<float>(20.0f, 20000.0f,
        [](float start, float end, float normalised) {
            return start * std::pow(end / start, normalised);
        },
        [](float start, float end, float value) {
            return std::log(value / start) / std::log(end / start);
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_MASK_HIGH_FREQ, 1 },
        "Mask High",
        maskHighFreqRange,
        5000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Mask transition width (0.1-4 octaves)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_MASK_TRANSITION, 1 },
        "Mask Transition",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.1f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("oct")));

    // === Spectral Delay Parameters ===

    // Delay enabled toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_DELAY_ENABLED, 1 },
        "Delay Enabled",
        false));

    // Delay time (10-2000 ms, log scale)
    auto delayTimeRange = juce::NormalisableRange<float>(10.0f, 2000.0f,
        [](float start, float end, float normalised) {
            return start * std::pow(end / start, normalised);
        },
        [](float start, float end, float value) {
            return std::log(value / start) / std::log(end / start);
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_TIME, 1 },
        "Delay Time",
        delayTimeRange,
        200.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Delay tempo sync toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_DELAY_SYNC, 1 },
        "Sync",
        false));  // Default to free-running (ms)

    // Delay tempo division (when synced)
    juce::StringArray divisionNames{
        "1/32", "1/16T", "1/16", "1/16D",
        "1/8T", "1/8", "1/8D",
        "1/4T", "1/4", "1/4D",
        "1/2T", "1/2", "1/2D",
        "1/1", "2/1", "4/1"
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_DELAY_DIVISION, 1 },
        "Division",
        divisionNames,
        8));  // Default to 1/4

    // Delay frequency slope (-100 to +100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_SLOPE, 1 },
        "Freq Slope",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Delay feedback (0-95%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_FEEDBACK, 1 },
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 95.0f, 0.1f),
        30.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Delay damping (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_DAMPING, 1 },
        "Damping",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        30.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Delay diffuse (0-100%) - spectral delay wet/dry (smear effect)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_DIFFUSE, 1 },
        "Diffuse",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Delay mix (0-100%) - time-domain delay echo level
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_MIX, 1 },
        "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Delay gain (-12 to +24 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_GAIN, 1 },
        "Delay Gain",
        juce::NormalisableRange<float>(-12.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // === Phase 2B: Envelope Preservation and Transient Detection ===

    // PRESERVE: Spectral envelope preservation (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_PRESERVE, 1 },
        "Preserve",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // TRANSIENTS: How much transient frames bypass quantization (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_TRANSIENTS, 1 },
        "Transients",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // SENSITIVITY: Transient detection threshold (0-100%)
    // 0% = 3x energy ratio, 100% = 1.2x ratio, default 50% = 1.5x
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_SENSITIVITY, 1 },
        "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    return { params.begin(), params.end() };
}

void FrequencyShifterProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_SHIFT_HZ)
    {
        shiftHz.store(newValue);
    }
    else if (parameterID == PARAM_QUANTIZE_STRENGTH)
    {
        quantizeStrength.store(newValue / 100.0f);
    }
    else if (parameterID == PARAM_ROOT_NOTE)
    {
        // Index is now 0-11 (pitch class), use middle octave (C4=60) as reference
        int midiNote = static_cast<int>(newValue) + 60;  // C=60, C#=61, ..., B=71
        rootNote.store(midiNote);
        if (quantizer)
        {
            quantizer->setRootNote(midiNote);
        }
    }
    else if (parameterID == PARAM_SCALE_TYPE)
    {
        int scale = static_cast<int>(newValue);
        scaleType.store(scale);
        if (quantizer)
        {
            quantizer->setScaleType(static_cast<fshift::ScaleType>(scale));
        }
    }
    else if (parameterID == PARAM_DRY_WET)
    {
        dryWetMix.store(newValue / 100.0f);
    }
    else if (parameterID == PARAM_PHASE_VOCODER)
    {
        usePhaseVocoder.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_SMEAR)
    {
        float oldSmear = smearMs.load();
        if (std::abs(newValue - oldSmear) > 0.1f)
        {
            smearMs.store(newValue);
            needsReinit.store(true);
        }
    }
    else if (parameterID == PARAM_DRIFT_AMOUNT)
    {
        driftAmount.store(newValue / 100.0f);
        driftModulator.setDepth(newValue / 100.0f);
    }
    else if (parameterID == PARAM_DRIFT_RATE)
    {
        driftRate.store(newValue);
        driftModulator.setRate(newValue);
    }
    else if (parameterID == PARAM_DRIFT_MODE)
    {
        int mode = static_cast<int>(newValue);
        driftMode.store(mode);
        if (mode == 0)
            driftModulator.setMode(fshift::DriftModulator::Mode::LFO);
        else if (mode == 1)
            driftModulator.setMode(fshift::DriftModulator::Mode::Perlin);
        else
            driftModulator.setMode(fshift::DriftModulator::Mode::Stochastic);
    }
    else if (parameterID == PARAM_STOCHASTIC_TYPE)
    {
        int type = static_cast<int>(newValue);
        stochasticType.store(type);
        driftModulator.setStochasticType(static_cast<fshift::DriftModulator::StochasticType>(type));
    }
    else if (parameterID == PARAM_STOCHASTIC_DENSITY)
    {
        stochasticDensity.store(newValue / 100.0f);
        driftModulator.setDensity(newValue / 100.0f);
    }
    else if (parameterID == PARAM_STOCHASTIC_SMOOTHNESS)
    {
        stochasticSmoothness.store(newValue / 100.0f);
        driftModulator.setSmoothness(newValue / 100.0f);
    }
    else if (parameterID == PARAM_MASK_ENABLED)
    {
        maskEnabled.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_MASK_MODE)
    {
        int mode = static_cast<int>(newValue);
        maskMode.store(mode);
        spectralMask.setMode(static_cast<fshift::SpectralMask::Mode>(mode));
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_MASK_LOW_FREQ)
    {
        maskLowFreq.store(newValue);
        spectralMask.setLowFreq(newValue);
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_MASK_HIGH_FREQ)
    {
        maskHighFreq.store(newValue);
        spectralMask.setHighFreq(newValue);
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_MASK_TRANSITION)
    {
        maskTransition.store(newValue);
        spectralMask.setTransition(newValue);
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_DELAY_ENABLED)
    {
        delayEnabled.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_DELAY_TIME)
    {
        delayTime.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setDelayTime(newValue);
    }
    else if (parameterID == PARAM_DELAY_SYNC)
    {
        delaySync.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_DELAY_DIVISION)
    {
        delayDivision.store(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_DELAY_SLOPE)
    {
        delaySlope.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setFrequencySlope(newValue);
    }
    else if (parameterID == PARAM_DELAY_FEEDBACK)
    {
        delayFeedback.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setFeedback(newValue / 100.0f);
    }
    else if (parameterID == PARAM_DELAY_DAMPING)
    {
        delayDamping.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setDamping(newValue);

        // Calculate feedback filter coefficient for time-domain damping
        // 0% damping = 12kHz cutoff, 100% damping = 1kHz cutoff (logarithmic)
        float dampNorm = newValue / 100.0f;
        float cutoffHz = 12000.0f * std::pow(1000.0f / 12000.0f, dampNorm);
        // One-pole lowpass: coeff = exp(-2*pi*fc/fs)
        feedbackFilterCoeff = std::exp(-2.0f * static_cast<float>(M_PI) * cutoffHz / static_cast<float>(currentSampleRate));
    }
    else if (parameterID == PARAM_DELAY_DIFFUSE)
    {
        delayDiffuse.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setMix(newValue);  // Spectral delay uses "mix" internally for diffuse
    }
    else if (parameterID == PARAM_DELAY_MIX)
    {
        delayMix.store(newValue);
        // This controls time-domain delay echo level, used in processBlock
    }
    else if (parameterID == PARAM_DELAY_GAIN)
    {
        delayGain.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setGain(newValue);
    }
    else if (parameterID == PARAM_PRESERVE)
    {
        preserveAmount.store(newValue / 100.0f);
        if (quantizer)
            quantizer->setPreserveAmount(newValue / 100.0f);
    }
    else if (parameterID == PARAM_TRANSIENTS)
    {
        transientAmount.store(newValue / 100.0f);
        if (quantizer)
            quantizer->setTransientAmount(newValue / 100.0f);
    }
    else if (parameterID == PARAM_SENSITIVITY)
    {
        transientSensitivity.store(newValue / 100.0f);
        if (quantizer)
            quantizer->setTransientSensitivity(newValue / 100.0f);
    }
}

void FrequencyShifterProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Phase 2B+ Amplitude envelope follower coefficients
    // Attack: ~1ms for fast transient response
    // Release: ~50ms for smooth decay
    float attackTimeMs = 1.0f;
    float releaseTimeMs = 50.0f;
    envAttackCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * attackTimeMs / 1000.0f));
    envReleaseCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * releaseTimeMs / 1000.0f));

    // Reset envelope states
    inputEnvelope.fill(0.0f);
    outputEnvelope.fill(0.0f);

    // Initialize with current quality mode
    reinitializeDsp();
}

int FrequencyShifterProcessor::fftSizeFromMs(float ms) const
{
    // Convert ms to samples, then find nearest FFT size
    int targetSamples = static_cast<int>(ms * currentSampleRate / 1000.0);

    // Find the closest FFT size
    int closest = FFT_SIZES[0];
    int minDiff = std::abs(targetSamples - closest);

    for (int i = 1; i < NUM_FFT_SIZES; ++i)
    {
        int diff = std::abs(targetSamples - FFT_SIZES[i]);
        if (diff < minDiff)
        {
            minDiff = diff;
            closest = FFT_SIZES[i];
        }
    }
    return closest;
}

void FrequencyShifterProcessor::getBlendParameters(float smearMsValue, int& fftSize1, int& fftSize2, float& crossfade) const
{
    // Convert ms to samples
    float targetSamples = smearMsValue * static_cast<float>(currentSampleRate) / 1000.0f;

    // Find which two FFT sizes we're between
    fftSize1 = FFT_SIZES[0];
    fftSize2 = FFT_SIZES[0];
    crossfade = 0.0f;

    for (int i = 0; i < NUM_FFT_SIZES - 1; ++i)
    {
        float lowSize = static_cast<float>(FFT_SIZES[i]);
        float highSize = static_cast<float>(FFT_SIZES[i + 1]);

        if (targetSamples >= lowSize && targetSamples <= highSize)
        {
            fftSize1 = FFT_SIZES[i];
            fftSize2 = FFT_SIZES[i + 1];
            // Linear interpolation between sizes
            crossfade = (targetSamples - lowSize) / (highSize - lowSize);
            return;
        }
    }

    // If we're at or above the max, use the largest size
    if (targetSamples >= FFT_SIZES[NUM_FFT_SIZES - 1])
    {
        fftSize1 = FFT_SIZES[NUM_FFT_SIZES - 1];
        fftSize2 = FFT_SIZES[NUM_FFT_SIZES - 1];
        crossfade = 0.0f;
    }
}

void FrequencyShifterProcessor::reinitializeDsp()
{
    // Get the two FFT sizes we need based on SMEAR setting
    float smear = smearMs.load();
    int fftSize1, fftSize2;
    float crossfade;
    getBlendParameters(smear, fftSize1, fftSize2, crossfade);

    currentFftSizes[0] = fftSize1;
    currentFftSizes[1] = fftSize2;
    currentHopSizes[0] = fftSize1 / 4;  // Standard 75% overlap
    currentHopSizes[1] = fftSize2 / 4;
    currentCrossfade = crossfade;

    // If crossfade is very close to 0 or 1, use single processor mode
    useSingleProcessor = (crossfade < 0.01f || crossfade > 0.99f || fftSize1 == fftSize2);

    const int numChannels = getTotalNumInputChannels();

    // Initialize DSP components for each channel and each processor
    for (int ch = 0; ch < std::min(numChannels, MAX_CHANNELS); ++ch)
    {
        for (int proc = 0; proc < NUM_PROCESSORS; ++proc)
        {
            int fftSize = currentFftSizes[proc];
            int hopSize = currentHopSizes[proc];

            stftProcessors[ch][proc] = std::make_unique<fshift::STFT>(fftSize, hopSize);
            stftProcessors[ch][proc]->prepare(currentSampleRate);

            phaseVocoders[ch][proc] = std::make_unique<fshift::PhaseVocoder>(fftSize, hopSize, currentSampleRate);

            frequencyShifters[ch][proc] = std::make_unique<fshift::FrequencyShifter>(currentSampleRate, fftSize);

            // Initialize overlap-add buffers
            inputBuffers[ch][proc].resize(static_cast<size_t>(fftSize) * 2, 0.0f);
            outputBuffers[ch][proc].resize(static_cast<size_t>(fftSize) * 2, 0.0f);
            inputWritePos[ch][proc] = 0;
            outputReadPos[ch][proc] = 0;
        }

        // Initialize delay compensation buffer
        // Fixed latency is MAX_FFT_SIZE samples, we add delay when using smaller FFT
        delayCompBuffers[ch].resize(static_cast<size_t>(MAX_FFT_SIZE) * 2, 0.0f);
        delayCompWritePos[ch] = 0;
        delayCompReadPos[ch] = 0;

        // Initialize dry signal delay buffer
        // Dry signal must be delayed by full reported latency to align with wet
        dryDelayBuffers[ch].resize(static_cast<size_t>(MAX_FFT_SIZE) + 1, 0.0f);
        dryDelayWritePos[ch] = 0;
    }

    // Prepare drift modulator with max FFT size for consistency
    driftModulator.prepare(currentSampleRate, MAX_FFT_SIZE / 2);
    driftModulator.reset();

    // Prepare quantizer with primary FFT settings for phase continuity (Phase 2A.3)
    if (quantizer)
    {
        quantizer->prepare(currentSampleRate, currentFftSizes[0], currentHopSizes[0]);
    }

    // Pre-compute spectral mask curve with max FFT size
    spectralMask.computeMaskCurve(currentSampleRate, MAX_FFT_SIZE);
    maskNeedsUpdate.store(false);

    // Prepare spectral delays for both processors
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
    {
        for (int proc = 0; proc < NUM_PROCESSORS; ++proc)
        {
            int fftSize = currentFftSizes[proc];
            int hopSize = currentHopSizes[proc];
            spectralDelays[ch][proc].prepare(currentSampleRate, fftSize, hopSize);
            spectralDelays[ch][proc].setDelayTime(delayTime.load());
            spectralDelays[ch][proc].setFrequencySlope(delaySlope.load());
            spectralDelays[ch][proc].setFeedback(0.0f);  // Disable spectral delay internal feedback
            spectralDelays[ch][proc].setDamping(delayDamping.load());
            spectralDelays[ch][proc].setMix(delayDiffuse.load());  // Spectral delay uses "mix" for diffuse amount
            spectralDelays[ch][proc].setGain(delayGain.load());
        }

        // Initialize time-domain feedback buffer for cascading pitch shifts
        feedbackBuffers[static_cast<size_t>(ch)].resize(MAX_FEEDBACK_DELAY_SAMPLES, 0.0f);
        feedbackWritePos[static_cast<size_t>(ch)] = 0;
        feedbackFilterState[static_cast<size_t>(ch)] = 0.0f;
    }

    // Calculate initial feedback filter coefficient (lowpass for damping)
    float dampNorm = delayDamping.load() / 100.0f;
    float cutoffHz = 12000.0f * std::pow(1000.0f / 12000.0f, dampNorm);
    feedbackFilterCoeff = std::exp(-2.0f * static_cast<float>(M_PI) * cutoffHz / static_cast<float>(currentSampleRate));

    // Calculate highpass filter coefficients (80Hz, Q=0.707, Butterworth)
    // This prevents low frequency buildup in the feedback loop
    {
        const float hpfCutoff = 80.0f;
        const float Q = 0.707f;
        const float omega = 2.0f * static_cast<float>(M_PI) * hpfCutoff / static_cast<float>(currentSampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * Q);

        const float a0 = 1.0f + alpha;
        feedbackHpfCoeffs[0] = (1.0f + cosOmega) / 2.0f / a0;  // b0
        feedbackHpfCoeffs[1] = -(1.0f + cosOmega) / a0;         // b1
        feedbackHpfCoeffs[2] = (1.0f + cosOmega) / 2.0f / a0;  // b2
        feedbackHpfCoeffs[3] = -(-2.0f * cosOmega) / a0;        // -a1 (negated for direct form)
        feedbackHpfCoeffs[4] = -(1.0f - alpha) / a0;            // -a2 (negated for direct form)

        // Reset HPF state
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        {
            feedbackHpfState[static_cast<size_t>(ch)].fill(0.0f);
        }
    }

    // Always report fixed latency of MAX_FFT_SIZE samples to host
    setLatencySamples(MAX_FFT_SIZE);
    needsReinit.store(false);
}

void FrequencyShifterProcessor::releaseResources()
{
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
    {
        for (int proc = 0; proc < NUM_PROCESSORS; ++proc)
        {
            stftProcessors[ch][proc].reset();
            phaseVocoders[ch][proc].reset();
            frequencyShifters[ch][proc].reset();
            inputBuffers[ch][proc].clear();
            outputBuffers[ch][proc].clear();
        }
        delayCompBuffers[ch].clear();
        dryDelayBuffers[ch].clear();
    }
}

bool FrequencyShifterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support mono and stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input must match output
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void FrequencyShifterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Check if we need to reinitialize DSP (SMEAR changed)
    if (needsReinit.load())
    {
        reinitializeDsp();
    }

    // Update mask curve if parameters changed (use primary FFT size)
    if (maskNeedsUpdate.load())
    {
        spectralMask.computeMaskCurve(currentSampleRate, currentFftSizes[0]);
        maskNeedsUpdate.store(false);
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Get current parameter values
    const float currentShiftHz = shiftHz.load();
    const float currentQuantizeStrength = quantizeStrength.load();
    const float currentDryWet = dryWetMix.load();
    const bool currentUsePhaseVocoder = usePhaseVocoder.load();
    const float currentDriftAmount = driftAmount.load();
    const bool currentMaskEnabled = maskEnabled.load();
    const bool currentDelayEnabled = delayEnabled.load();
    const bool currentDelaySync = delaySync.load();
    const int currentDelayDivision = delayDivision.load();
    const float currentFeedbackAmount = delayFeedback.load() / 100.0f;  // 0-0.95
    const float currentDelayMixAmount = delayMix.load() / 100.0f;  // 0-1, controls echo level

    // Read host tempo for tempo sync
    double currentBpm = 120.0;  // Fallback
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto tempo = position->getBpm())
            {
                currentBpm = *tempo;
            }
        }
    }
    hostBpm.store(currentBpm);

    // Calculate actual delay time (either from TIME parameter or tempo sync)
    float currentDelayTimeMs;
    if (currentDelaySync && currentDelayDivision >= 0 && currentDelayDivision < NUM_TEMPO_DIVISIONS)
    {
        // Tempo synced: calculate from BPM and division
        double quarterNoteMs = 60000.0 / currentBpm;
        currentDelayTimeMs = static_cast<float>(quarterNoteMs * TEMPO_DIVISION_MULTIPLIERS[currentDelayDivision]);
    }
    else
    {
        // Free running: use TIME parameter directly
        currentDelayTimeMs = delayTime.load();
    }

    // Cache crossfade value
    const float crossfade = currentCrossfade;
    const bool singleProc = useSingleProcessor;

    // If no processing needed, still apply delay compensation for timing
    const bool bypassProcessing = (std::abs(currentShiftHz) < 0.01f && currentQuantizeStrength < 0.01f);

    // Process each channel
    for (int channel = 0; channel < std::min(numChannels, MAX_CHANNELS); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        // Store dry signal for mixing
        std::vector<float> drySignal(channelData, channelData + numSamples);

        // Temp buffers for dual processor output
        std::vector<float> proc0Output(static_cast<size_t>(numSamples), 0.0f);
        std::vector<float> proc1Output(static_cast<size_t>(numSamples), 0.0f);

        // Process through both STFT pipelines (or just one if singleProc)
        const int numProcs = singleProc ? 1 : 2;

        for (int proc = 0; proc < numProcs; ++proc)
        {
            if (!stftProcessors[channel][proc])
                continue;

            const int fftSize = currentFftSizes[proc];
            const int hopSize = currentHopSizes[proc];
            auto& inputBuf = inputBuffers[channel][proc];
            auto& outputBuf = outputBuffers[channel][proc];
            auto& inWritePos = inputWritePos[channel][proc];
            auto& outReadPos = outputReadPos[channel][proc];
            auto& procOutput = (proc == 0) ? proc0Output : proc1Output;

            // Process through STFT pipeline
            for (int i = 0; i < numSamples; ++i)
            {
                // Start with dry input sample
                float inputSample = drySignal[static_cast<size_t>(i)];

                // Add feedback from time-domain buffer (only once per sample, on proc 0)
                // This routes feedback BEFORE the shifter for cascading pitch shifts
                if (currentDelayEnabled && proc == 0 && currentFeedbackAmount > 0.01f)
                {
                    auto& fbBuffer = feedbackBuffers[static_cast<size_t>(channel)];
                    int fbBufSize = static_cast<int>(fbBuffer.size());

                    // Calculate delay in samples from TIME parameter
                    // IMPORTANT: Compensate for SMEAR-dependent FFT latency!
                    // The feedback path goes through FFT processing, which adds latency
                    // that varies with SMEAR setting. We subtract this to keep delay
                    // timing consistent regardless of SMEAR.
                    //
                    // FFT latency is approximately fftSize samples (input buffering + output overlap)
                    // We use the primary processor's FFT size (proc 0) since that's where
                    // feedback is injected.
                    int currentFftLatencySamples = currentFftSizes[0];  // SMEAR-dependent latency

                    int rawDelaySamples = static_cast<int>(currentDelayTimeMs * currentSampleRate / 1000.0f);
                    int delaySamples = rawDelaySamples - currentFftLatencySamples;

                    // Ensure minimum delay of ~10ms to prevent artifacts
                    int minDelaySamples = static_cast<int>(10.0f * currentSampleRate / 1000.0f);
                    delaySamples = std::clamp(delaySamples, minDelaySamples, fbBufSize - 1);

                    // Read from feedback buffer
                    int fbReadPos = (feedbackWritePos[static_cast<size_t>(channel)] - delaySamples + fbBufSize) % fbBufSize;
                    float feedbackSample = fbBuffer[static_cast<size_t>(fbReadPos)];

                    // Apply feedback amount (controls cascade strength)
                    feedbackSample *= currentFeedbackAmount;

                    // Soft clip feedback for safety (tanh-style)
                    if (std::abs(feedbackSample) > 0.95f)
                    {
                        feedbackSample = std::tanh(feedbackSample);
                    }

                    // MIX controls echo level added to the input
                    // At MIX=0%: No echoes added to input (clean shifted signal)
                    // At MIX=100%: Full echo level added (shifted + echoes)
                    // Note: Direct signal always passes through; MIX adds echo on top
                    inputSample += feedbackSample * currentDelayMixAmount;

                    // DEBUG: Log feedback activity (once per second per channel)
                    static int debugCounter = 0;
                    if (channel == 0 && ++debugCounter % static_cast<int>(currentSampleRate) == 0)
                    {
                        DBG("=== Delay Feedback Debug ===");
                        DBG("Feedback sample: " + juce::String(feedbackSample, 6));
                        DBG("Input after feedback: " + juce::String(inputSample, 6));
                        DBG("Requested delay: " + juce::String(currentDelayTimeMs) + " ms");
                        DBG("FFT latency compensation: " + juce::String(currentFftLatencySamples) + " samples ("
                            + juce::String(currentFftLatencySamples * 1000.0f / currentSampleRate, 1) + " ms)");
                        DBG("Raw delay samples: " + juce::String(rawDelaySamples));
                        DBG("Compensated delay samples: " + juce::String(delaySamples));
                        DBG("Feedback amount: " + juce::String(currentFeedbackAmount * 100.0f) + "%");
                    }
                }

                // Write input sample (with feedback) to circular buffer
                inputBuf[static_cast<size_t>(inWritePos)] = inputSample;
                inWritePos = (inWritePos + 1) % static_cast<int>(inputBuf.size());

                // Check if we have enough samples for an FFT frame
                if (inWritePos % hopSize == 0)
                {
                    // Get input frame
                    std::vector<float> inputFrame(static_cast<size_t>(fftSize));
                    int readPos = (inWritePos - fftSize + static_cast<int>(inputBuf.size()))
                                  % static_cast<int>(inputBuf.size());
                    for (int j = 0; j < fftSize; ++j)
                    {
                        inputFrame[static_cast<size_t>(j)] = inputBuf[static_cast<size_t>((readPos + j) % static_cast<int>(inputBuf.size()))];
                    }

                    // Perform STFT
                    auto [magnitude, phase] = stftProcessors[channel][proc]->forward(inputFrame);

                    if (!bypassProcessing)
                    {
                        // Save dry spectrum for mask blending
                        std::vector<float> dryMagnitude;
                        std::vector<float> dryPhase;
                        if (currentMaskEnabled)
                        {
                            dryMagnitude = magnitude;
                            dryPhase = phase;
                        }

                        // Phase 2B: Capture spectral envelope from INPUT before any processing
                        // This is crucial for accurate timbre preservation
                        std::vector<float> inputEnvelope;
                        const std::vector<float>* envelopePtr = nullptr;
                        float currentPreserve = preserveAmount.load();
                        if (currentPreserve > 0.01f && quantizer && currentQuantizeStrength > 0.01f)
                        {
                            inputEnvelope = quantizer->getSpectralEnvelope(magnitude, currentSampleRate, fftSize);
                            envelopePtr = &inputEnvelope;
                        }

                        // Apply phase vocoder if enabled
                        if (currentUsePhaseVocoder && std::abs(currentShiftHz) > 0.01f)
                        {
                            phase = phaseVocoders[channel][proc]->process(magnitude, phase, currentShiftHz);
                        }

                        // Apply frequency shifting
                        if (std::abs(currentShiftHz) > 0.01f)
                        {
                            std::tie(magnitude, phase) = frequencyShifters[channel][proc]->shift(magnitude, phase, currentShiftHz);
                        }

                        // Apply musical quantization with optional drift
                        if (currentQuantizeStrength > 0.01f && quantizer)
                        {
                            // Generate drift values if drift is enabled
                            std::vector<float> driftCentsVec;
                            const std::vector<float>* driftPtr = nullptr;

                            if (currentDriftAmount > 0.01f)
                            {
                                const int numBins = fftSize / 2;
                                driftCentsVec.resize(static_cast<size_t>(numBins));
                                for (int bin = 0; bin < numBins; ++bin)
                                {
                                    driftCentsVec[static_cast<size_t>(bin)] = driftModulator.getDrift(bin);
                                }
                                driftPtr = &driftCentsVec;

                                // Advance drift modulator for next frame (only once per channel 0, proc 0)
                                if (channel == 0 && proc == 0)
                                {
                                    driftModulator.advanceFrame(hopSize);
                                }
                            }

                            // Pass the pre-shift envelope for accurate timbre preservation
                            std::tie(magnitude, phase) = quantizer->quantizeSpectrum(
                                magnitude, phase, currentSampleRate, fftSize, currentQuantizeStrength, driftPtr, envelopePtr);
                        }

                        // Apply spectral mask (blend wet/dry per frequency bin)
                        if (currentMaskEnabled && !dryMagnitude.empty())
                        {
                            spectralMask.applyMask(magnitude, dryMagnitude);
                            spectralMask.applyMaskToPhase(phase, dryPhase);
                        }

                        // Apply spectral delay (frequency-dependent delay)
                        if (currentDelayEnabled)
                        {
                            // Update spectral delay with tempo-synced time (if sync enabled)
                            spectralDelays[channel][proc].setDelayTime(currentDelayTimeMs);
                            spectralDelays[channel][proc].process(magnitude, phase);
                        }
                    }

                    // Store spectrum data for visualization (only from first channel, first processor)
                    if (channel == 0 && proc == 0)
                    {
                        const juce::SpinLock::ScopedLockType lock(spectrumLock);
                        const int numBins = std::min(static_cast<int>(magnitude.size()), SPECTRUM_SIZE);
                        for (int bin = 0; bin < numBins; ++bin)
                        {
                            // Convert to dB with smoothing
                            float magDb = juce::Decibels::gainToDecibels(magnitude[static_cast<size_t>(bin)], -100.0f);
                            // Normalize to 0-1 range (-100dB to 0dB)
                            float normalized = (magDb + 100.0f) / 100.0f;
                            spectrumData[static_cast<size_t>(bin)] = std::max(0.0f, std::min(1.0f, normalized));
                        }
                        spectrumDataReady.store(true);
                    }

                    // Perform inverse STFT
                    auto outputFrame = stftProcessors[channel][proc]->inverse(magnitude, phase);

                    // Overlap-add to output buffer
                    int writePos = (outReadPos + i) % static_cast<int>(outputBuf.size());
                    for (int j = 0; j < fftSize; ++j)
                    {
                        int pos = (writePos + j) % static_cast<int>(outputBuf.size());
                        outputBuf[static_cast<size_t>(pos)] += outputFrame[static_cast<size_t>(j)];
                    }
                }

                // Read from output buffer
                float outputSample = outputBuf[static_cast<size_t>(outReadPos)];
                outputBuf[static_cast<size_t>(outReadPos)] = 0.0f;  // Clear for next overlap-add
                outReadPos = (outReadPos + 1) % static_cast<int>(outputBuf.size());

                // Write processed output to time-domain feedback buffer (only once, on proc 0)
                // This gets added to input on the next delay cycle, creating cascading pitch shifts
                if (currentDelayEnabled && proc == 0)
                {
                    auto& fbBuffer = feedbackBuffers[static_cast<size_t>(channel)];
                    int fbBufSize = static_cast<int>(fbBuffer.size());

                    // === Feedback signal chain: HPF (80Hz) → LPF (DAMP) → Write ===

                    // Step 1: Apply highpass filter (80Hz) to prevent low frequency buildup
                    // Biquad Direct Form I: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
                    auto& hpfState = feedbackHpfState[static_cast<size_t>(channel)];
                    float x0 = outputSample;
                    float x1 = hpfState[0];
                    float x2 = hpfState[1];
                    float y1 = hpfState[2];
                    float y2 = hpfState[3];

                    float hpfOutput = feedbackHpfCoeffs[0] * x0
                                    + feedbackHpfCoeffs[1] * x1
                                    + feedbackHpfCoeffs[2] * x2
                                    + feedbackHpfCoeffs[3] * y1  // Note: coeffs already negated
                                    + feedbackHpfCoeffs[4] * y2;

                    // Update HPF state
                    hpfState[1] = x1;  // x[n-2] = x[n-1]
                    hpfState[0] = x0;  // x[n-1] = x[n]
                    hpfState[3] = y1;  // y[n-2] = y[n-1]
                    hpfState[2] = hpfOutput;  // y[n-1] = y[n]

                    // Step 2: Apply damping filter (one-pole lowpass) to feedback
                    float& lpfState = feedbackFilterState[static_cast<size_t>(channel)];
                    lpfState = hpfOutput + feedbackFilterCoeff * (lpfState - hpfOutput);
                    float filteredSample = lpfState;

                    // Write to feedback buffer
                    fbBuffer[static_cast<size_t>(feedbackWritePos[static_cast<size_t>(channel)])] = filteredSample;
                    feedbackWritePos[static_cast<size_t>(channel)] = (feedbackWritePos[static_cast<size_t>(channel)] + 1) % fbBufSize;

                    // DEBUG: Log output being written to feedback buffer (once per second)
                    static int fbWriteDebugCounter = 0;
                    if (channel == 0 && ++fbWriteDebugCounter % static_cast<int>(currentSampleRate) == 0)
                    {
                        DBG("--- Feedback Write ---");
                        DBG("Output sample (raw): " + juce::String(outputSample, 6));
                        DBG("After HPF: " + juce::String(hpfOutput, 6));
                        DBG("After LPF (written to buffer): " + juce::String(filteredSample, 6));
                    }
                }

                procOutput[static_cast<size_t>(i)] = outputSample;
            }
        }

        // Crossfade between the two processor outputs (equal-power crossfade)
        // gain1^2 + gain2^2 = 1 for equal power
        const float angle = crossfade * static_cast<float>(M_PI) * 0.5f;
        const float gain0 = std::cos(angle);
        const float gain1 = std::sin(angle);

        for (int i = 0; i < numSamples; ++i)
        {
            float processed;
            if (singleProc)
            {
                processed = proc0Output[static_cast<size_t>(i)];
            }
            else
            {
                processed = proc0Output[static_cast<size_t>(i)] * gain0 + proc1Output[static_cast<size_t>(i)] * gain1;
            }

            // Apply delay compensation to maintain fixed latency to host
            // The host expects MAX_FFT_SIZE latency, so we add extra delay for smaller FFT sizes
            int effectiveFftSize = singleProc ? currentFftSizes[0] :
                static_cast<int>(static_cast<float>(currentFftSizes[0]) * gain0 * gain0 +
                                 static_cast<float>(currentFftSizes[1]) * gain1 * gain1);
            int delayNeeded = MAX_FFT_SIZE - effectiveFftSize;

            // Write to delay compensation buffer
            delayCompBuffers[channel][static_cast<size_t>(delayCompWritePos[channel])] = processed;
            delayCompWritePos[channel] = (delayCompWritePos[channel] + 1) % static_cast<int>(delayCompBuffers[channel].size());

            // Read from delay compensation buffer with the needed delay
            int readIdx = (delayCompWritePos[channel] - delayNeeded - 1 + static_cast<int>(delayCompBuffers[channel].size()))
                         % static_cast<int>(delayCompBuffers[channel].size());
            float wetSample = delayCompBuffers[channel][static_cast<size_t>(readIdx)];

            // Delay dry signal by full reported latency (MAX_FFT_SIZE samples)
            // This ensures dry and wet are time-aligned when mixed
            auto& dryBuf = dryDelayBuffers[channel];
            int bufSize = static_cast<int>(dryBuf.size());

            // Write current dry sample
            dryBuf[static_cast<size_t>(dryDelayWritePos[channel])] = drySignal[static_cast<size_t>(i)];

            // Read delayed dry sample (MAX_FFT_SIZE samples behind)
            int dryReadIdx = (dryDelayWritePos[channel] - MAX_FFT_SIZE + bufSize) % bufSize;
            float delayedDrySample = dryBuf[static_cast<size_t>(dryReadIdx)];

            // Advance write position
            dryDelayWritePos[channel] = (dryDelayWritePos[channel] + 1) % bufSize;

            // Phase 2B+ Amplitude envelope tracking and correction
            // Tied to PRESERVE control - at 100%, both spectral AND amplitude dynamics match input
            float currentPreserve = preserveAmount.load();
            if (currentPreserve > 0.01f && !bypassProcessing)
            {
                // Track input amplitude envelope (from delayed dry signal, time-aligned)
                float inputAbs = std::abs(delayedDrySample);
                if (inputAbs > inputEnvelope[static_cast<size_t>(channel)])
                {
                    // Attack: fast rise
                    inputEnvelope[static_cast<size_t>(channel)] =
                        inputAbs + envAttackCoeff * (inputEnvelope[static_cast<size_t>(channel)] - inputAbs);
                }
                else
                {
                    // Release: slow decay
                    inputEnvelope[static_cast<size_t>(channel)] =
                        inputAbs + envReleaseCoeff * (inputEnvelope[static_cast<size_t>(channel)] - inputAbs);
                }

                // Track output amplitude envelope (from wet signal before correction)
                float outputAbs = std::abs(wetSample);
                if (outputAbs > outputEnvelope[static_cast<size_t>(channel)])
                {
                    outputEnvelope[static_cast<size_t>(channel)] =
                        outputAbs + envAttackCoeff * (outputEnvelope[static_cast<size_t>(channel)] - outputAbs);
                }
                else
                {
                    outputEnvelope[static_cast<size_t>(channel)] =
                        outputAbs + envReleaseCoeff * (outputEnvelope[static_cast<size_t>(channel)] - outputAbs);
                }

                // Apply gain correction to match output envelope to input envelope
                // Use non-linear strength (same curve as spectral preserve)
                float effectiveStrength = std::pow(currentPreserve, 0.7f);
                constexpr float epsilon = 1e-6f;
                float gainCorrection = inputEnvelope[static_cast<size_t>(channel)] /
                                       (outputEnvelope[static_cast<size_t>(channel)] + epsilon);

                // Clamp correction to avoid extreme values (±12dB range)
                gainCorrection = std::clamp(gainCorrection, 0.25f, 4.0f);

                // Blend: at effectiveStrength=0, no correction; at 1, full correction
                float blendedCorrection = 1.0f + effectiveStrength * (gainCorrection - 1.0f);

                wetSample *= blendedCorrection;
            }

            // Mix delayed dry with (possibly corrected) wet
            channelData[i] = delayedDrySample * (1.0f - currentDryWet) + wetSample * currentDryWet;
        }
    }
}

int FrequencyShifterProcessor::getLatencySamples() const
{
    // Always report fixed latency for DAW timing stability
    return MAX_FFT_SIZE;
}

double FrequencyShifterProcessor::getTailLengthSeconds() const
{
    // Latency from FFT processing (use max for consistency)
    return static_cast<double>(MAX_FFT_SIZE + MAX_FFT_SIZE / 4) / currentSampleRate;
}

juce::AudioProcessorEditor* FrequencyShifterProcessor::createEditor()
{
    return new FrequencyShifterEditor(*this);
}

void FrequencyShifterProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FrequencyShifterProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

bool FrequencyShifterProcessor::getSpectrumData(std::array<float, SPECTRUM_SIZE>& data)
{
    if (!spectrumDataReady.load())
        return false;

    const juce::SpinLock::ScopedLockType lock(spectrumLock);
    data = spectrumData;
    spectrumDataReady.store(false);
    return true;
}

// Plugin instantiation
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyShifterProcessor();
}
