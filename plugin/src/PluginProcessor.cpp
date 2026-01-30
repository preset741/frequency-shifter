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
    parameters.addParameterListener(PARAM_DELAY_SLOPE, this);
    parameters.addParameterListener(PARAM_DELAY_FEEDBACK, this);
    parameters.addParameterListener(PARAM_DELAY_DAMPING, this);
    parameters.addParameterListener(PARAM_DELAY_MIX, this);
    parameters.addParameterListener(PARAM_DELAY_GAIN, this);

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
    parameters.removeParameterListener(PARAM_DELAY_SLOPE, this);
    parameters.removeParameterListener(PARAM_DELAY_FEEDBACK, this);
    parameters.removeParameterListener(PARAM_DELAY_DAMPING, this);
    parameters.removeParameterListener(PARAM_DELAY_MIX, this);
    parameters.removeParameterListener(PARAM_DELAY_GAIN, this);
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

    // Delay mix (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_MIX, 1 },
        "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Delay gain (-12 to +24 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DELAY_GAIN, 1 },
        "Delay Gain",
        juce::NormalisableRange<float>(-12.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

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
    }
    else if (parameterID == PARAM_DELAY_MIX)
    {
        delayMix.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setMix(newValue);
    }
    else if (parameterID == PARAM_DELAY_GAIN)
    {
        delayGain.store(newValue);
        for (auto& chDelays : spectralDelays)
            for (auto& delay : chDelays)
                delay.setGain(newValue);
    }
}

void FrequencyShifterProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

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
    }

    // Prepare drift modulator with max FFT size for consistency
    driftModulator.prepare(currentSampleRate, MAX_FFT_SIZE / 2);
    driftModulator.reset();

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
            spectralDelays[ch][proc].setFeedback(delayFeedback.load() / 100.0f);
            spectralDelays[ch][proc].setDamping(delayDamping.load());
            spectralDelays[ch][proc].setMix(delayMix.load());
            spectralDelays[ch][proc].setGain(delayGain.load());
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
                // Write input sample to circular buffer
                inputBuf[static_cast<size_t>(inWritePos)] = drySignal[static_cast<size_t>(i)];
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

                            std::tie(magnitude, phase) = quantizer->quantizeSpectrum(
                                magnitude, phase, currentSampleRate, fftSize, currentQuantizeStrength, driftPtr);
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
                procOutput[static_cast<size_t>(i)] = outputBuf[static_cast<size_t>(outReadPos)];
                outputBuf[static_cast<size_t>(outReadPos)] = 0.0f;  // Clear for next overlap-add
                outReadPos = (outReadPos + 1) % static_cast<int>(outputBuf.size());
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
            channelData[i] = delayCompBuffers[channel][static_cast<size_t>(readIdx)];
        }

        // Apply dry/wet mix
        if (currentDryWet < 0.99f)
        {
            // For dry signal, we also need delay compensation
            for (int i = 0; i < numSamples; ++i)
            {
                // Use the same effective delay for dry signal
                int delayIdx = (i + MAX_FFT_SIZE < numSamples) ? i : i;  // Simplified - dry already stored
                float drySample = drySignal[static_cast<size_t>(i)];
                channelData[i] = drySample * (1.0f - currentDryWet) + channelData[i] * currentDryWet;
            }
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
