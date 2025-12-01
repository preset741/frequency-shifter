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
    parameters.addParameterListener(PARAM_QUALITY_MODE, this);

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
    parameters.removeParameterListener(PARAM_QUALITY_MODE, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout FrequencyShifterProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Frequency shift (-2000 to +2000 Hz)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_SHIFT_HZ, 1 },
        "Shift (Hz)",
        juce::NormalisableRange<float>(-2000.0f, 2000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Quantize strength (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_QUANTIZE_STRENGTH, 1 },
        "Quantize",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
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

    // Quality mode (latency/quality tradeoff)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_QUALITY_MODE, 1 },
        "Quality",
        juce::StringArray{ "Low Latency", "Balanced", "Quality" },
        2));  // Default to Quality mode

    // Log scale toggle for frequency shift control
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_LOG_SCALE, 1 },
        "Log Scale",
        false));  // Default to linear

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
    else if (parameterID == PARAM_QUALITY_MODE)
    {
        int mode = static_cast<int>(newValue);
        if (mode != qualityMode.load())
        {
            qualityMode.store(mode);
            needsReinit.store(true);
        }
    }
}

void FrequencyShifterProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Initialize with current quality mode
    reinitializeDsp();
}

void FrequencyShifterProcessor::reinitializeDsp()
{
    // Determine FFT/hop sizes based on quality mode
    const auto mode = static_cast<QualityMode>(qualityMode.load());
    switch (mode)
    {
        case QualityMode::LowLatency:
            currentFftSize = 1024;
            currentHopSize = 256;
            break;
        case QualityMode::Balanced:
            currentFftSize = 2048;
            currentHopSize = 512;
            break;
        case QualityMode::Quality:
        default:
            currentFftSize = 4096;
            currentHopSize = 1024;
            break;
    }

    const int numChannels = getTotalNumInputChannels();

    // Initialize DSP components for each channel
    for (int ch = 0; ch < std::min(numChannels, MAX_CHANNELS); ++ch)
    {
        stftProcessors[ch] = std::make_unique<fshift::STFT>(currentFftSize, currentHopSize);
        stftProcessors[ch]->prepare(currentSampleRate);

        phaseVocoders[ch] = std::make_unique<fshift::PhaseVocoder>(currentFftSize, currentHopSize, currentSampleRate);

        frequencyShifters[ch] = std::make_unique<fshift::FrequencyShifter>(currentSampleRate, currentFftSize);

        // Initialize overlap-add buffers
        inputBuffers[ch].resize(static_cast<size_t>(currentFftSize) * 2, 0.0f);
        outputBuffers[ch].resize(static_cast<size_t>(currentFftSize) * 2, 0.0f);
        inputWritePos[ch] = 0;
        outputReadPos[ch] = 0;
    }

    // Update latency reporting
    setLatencySamples(getLatencySamples());
    needsReinit.store(false);
}

void FrequencyShifterProcessor::releaseResources()
{
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
    {
        stftProcessors[ch].reset();
        phaseVocoders[ch].reset();
        frequencyShifters[ch].reset();
        inputBuffers[ch].clear();
        outputBuffers[ch].clear();
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

    // Check if we need to reinitialize DSP (quality mode changed)
    if (needsReinit.load())
    {
        reinitializeDsp();
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Get current parameter values
    const float currentShiftHz = shiftHz.load();
    const float currentQuantizeStrength = quantizeStrength.load();
    const float currentDryWet = dryWetMix.load();
    const bool currentUsePhaseVocoder = usePhaseVocoder.load();

    // Cache current FFT settings for this block
    const int fftSize = currentFftSize;
    const int hopSize = currentHopSize;

    // If no processing needed, just pass through
    if (std::abs(currentShiftHz) < 0.01f && currentQuantizeStrength < 0.01f)
    {
        return;
    }

    // Process each channel
    for (int channel = 0; channel < std::min(numChannels, MAX_CHANNELS); ++channel)
    {
        if (!stftProcessors[channel])
            continue;

        auto* channelData = buffer.getWritePointer(channel);

        // Store dry signal for mixing
        std::vector<float> drySignal(channelData, channelData + numSamples);

        // Process through STFT pipeline
        for (int i = 0; i < numSamples; ++i)
        {
            // Write input sample to circular buffer
            inputBuffers[channel][static_cast<size_t>(inputWritePos[channel])] = channelData[i];
            inputWritePos[channel] = (inputWritePos[channel] + 1) % static_cast<int>(inputBuffers[channel].size());

            // Check if we have enough samples for an FFT frame
            if (inputWritePos[channel] % hopSize == 0)
            {
                // Get input frame
                std::vector<float> inputFrame(static_cast<size_t>(fftSize));
                int readPos = (inputWritePos[channel] - fftSize + static_cast<int>(inputBuffers[channel].size()))
                              % static_cast<int>(inputBuffers[channel].size());
                for (int j = 0; j < fftSize; ++j)
                {
                    inputFrame[static_cast<size_t>(j)] = inputBuffers[channel][static_cast<size_t>((readPos + j) % static_cast<int>(inputBuffers[channel].size()))];
                }

                // Perform STFT
                auto [magnitude, phase] = stftProcessors[channel]->forward(inputFrame);

                // Apply phase vocoder if enabled
                if (currentUsePhaseVocoder && std::abs(currentShiftHz) > 0.01f)
                {
                    phase = phaseVocoders[channel]->process(magnitude, phase, currentShiftHz);
                }

                // Apply frequency shifting
                if (std::abs(currentShiftHz) > 0.01f)
                {
                    std::tie(magnitude, phase) = frequencyShifters[channel]->shift(magnitude, phase, currentShiftHz);
                }

                // Apply musical quantization
                if (currentQuantizeStrength > 0.01f && quantizer)
                {
                    std::tie(magnitude, phase) = quantizer->quantizeSpectrum(
                        magnitude, phase, currentSampleRate, fftSize, currentQuantizeStrength);
                }

                // Perform inverse STFT
                auto outputFrame = stftProcessors[channel]->inverse(magnitude, phase);

                // Overlap-add to output buffer
                int writePos = (outputReadPos[channel] + i) % static_cast<int>(outputBuffers[channel].size());
                for (int j = 0; j < fftSize; ++j)
                {
                    int pos = (writePos + j) % static_cast<int>(outputBuffers[channel].size());
                    outputBuffers[channel][static_cast<size_t>(pos)] += outputFrame[static_cast<size_t>(j)];
                }
            }

            // Read from output buffer
            channelData[i] = outputBuffers[channel][static_cast<size_t>(outputReadPos[channel])];
            outputBuffers[channel][static_cast<size_t>(outputReadPos[channel])] = 0.0f;  // Clear for next overlap-add
            outputReadPos[channel] = (outputReadPos[channel] + 1) % static_cast<int>(outputBuffers[channel].size());
        }

        // Apply dry/wet mix
        if (currentDryWet < 0.99f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                channelData[i] = drySignal[static_cast<size_t>(i)] * (1.0f - currentDryWet) + channelData[i] * currentDryWet;
            }
        }
    }
}

int FrequencyShifterProcessor::getLatencySamples() const
{
    return currentFftSize;
}

double FrequencyShifterProcessor::getTailLengthSeconds() const
{
    // Latency from FFT processing
    return static_cast<double>(currentFftSize + currentHopSize) / currentSampleRate;
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

// Plugin instantiation
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyShifterProcessor();
}
