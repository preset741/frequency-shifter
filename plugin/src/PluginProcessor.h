#pragma once

#include <JuceHeader.h>
#include "dsp/STFT.h"
#include "dsp/PhaseVocoder.h"
#include "dsp/FrequencyShifter.h"
#include "dsp/MusicalQuantizer.h"

/**
 * FrequencyShifterProcessor - Main audio processor for the Frequency Shifter plugin.
 *
 * This processor implements harmonic-preserving frequency shifting with:
 * - Enhanced phase vocoder for artifact reduction
 * - Musical scale quantization
 * - Stereo processing support
 */
class FrequencyShifterProcessor : public juce::AudioProcessor,
                                   public juce::AudioProcessorValueTreeState::Listener
{
public:
    FrequencyShifterProcessor();
    ~FrequencyShifterProcessor() override;

    // AudioProcessor interface
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Parameter listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Parameter tree
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Parameter IDs
    static constexpr const char* PARAM_SHIFT_HZ = "shiftHz";
    static constexpr const char* PARAM_QUANTIZE_STRENGTH = "quantizeStrength";
    static constexpr const char* PARAM_ROOT_NOTE = "rootNote";
    static constexpr const char* PARAM_SCALE_TYPE = "scaleType";
    static constexpr const char* PARAM_DRY_WET = "dryWet";
    static constexpr const char* PARAM_PHASE_VOCODER = "phaseVocoder";
    static constexpr const char* PARAM_QUALITY_MODE = "qualityMode";
    static constexpr const char* PARAM_LOG_SCALE = "logScale";

    // Quality mode enum - controls FFT size / latency tradeoff
    enum class QualityMode
    {
        LowLatency = 0,  // FFT 1024, Hop 256 (~23ms latency, more artifacts)
        Balanced = 1,    // FFT 2048, Hop 512 (~46ms latency)
        Quality = 2      // FFT 4096, Hop 1024 (~93ms latency, cleanest)
    };

    // Get current latency in samples
    int getLatencySamples() const;

private:
    // Create parameter layout
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Process a single channel
    void processChannel(int channel, juce::AudioBuffer<float>& buffer);

    // Parameter tree state
    juce::AudioProcessorValueTreeState parameters;

    // DSP components (per channel for stereo)
    static constexpr int MAX_CHANNELS = 2;
    std::array<std::unique_ptr<fshift::STFT>, MAX_CHANNELS> stftProcessors;
    std::array<std::unique_ptr<fshift::PhaseVocoder>, MAX_CHANNELS> phaseVocoders;
    std::array<std::unique_ptr<fshift::FrequencyShifter>, MAX_CHANNELS> frequencyShifters;
    std::unique_ptr<fshift::MusicalQuantizer> quantizer;

    // Processing parameters (atomic for thread safety)
    std::atomic<float> shiftHz{ 0.0f };
    std::atomic<float> quantizeStrength{ 0.0f };
    std::atomic<float> dryWetMix{ 1.0f };
    std::atomic<bool> usePhaseVocoder{ true };
    std::atomic<int> rootNote{ 60 };  // C4
    std::atomic<int> scaleType{ 0 };  // Major
    std::atomic<int> qualityMode{ static_cast<int>(QualityMode::Quality) };

    // Processing state
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Current FFT settings (updated when quality mode changes)
    int currentFftSize = 4096;
    int currentHopSize = 1024;

    // Flag to reinitialize DSP on next block
    std::atomic<bool> needsReinit{ false };

    // Reinitialize DSP components with new FFT settings
    void reinitializeDsp();

    // Input/output buffers for overlap-add
    std::array<std::vector<float>, MAX_CHANNELS> inputBuffers;
    std::array<std::vector<float>, MAX_CHANNELS> outputBuffers;
    std::array<int, MAX_CHANNELS> inputWritePos{};
    std::array<int, MAX_CHANNELS> outputReadPos{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyShifterProcessor)
};
