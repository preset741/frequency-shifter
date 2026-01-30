#pragma once

#include <JuceHeader.h>
#include "dsp/STFT.h"
#include "dsp/PhaseVocoder.h"
#include "dsp/FrequencyShifter.h"
#include "dsp/MusicalQuantizer.h"
#include "dsp/DriftModulator.h"
#include "dsp/SpectralMask.h"
#include "dsp/SpectralDelay.h"

// Size of spectrum data for visualization (half of max FFT size)
static constexpr int SPECTRUM_SIZE = 2048;

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
    static constexpr const char* PARAM_SMEAR = "smear";  // 5-123ms continuous control
    static constexpr const char* PARAM_LOG_SCALE = "logScale";
    static constexpr const char* PARAM_DRIFT_AMOUNT = "driftAmount";
    static constexpr const char* PARAM_DRIFT_RATE = "driftRate";
    static constexpr const char* PARAM_DRIFT_MODE = "driftMode";
    static constexpr const char* PARAM_STOCHASTIC_TYPE = "stochasticType";
    static constexpr const char* PARAM_STOCHASTIC_DENSITY = "stochasticDensity";
    static constexpr const char* PARAM_STOCHASTIC_SMOOTHNESS = "stochasticSmoothness";
    static constexpr const char* PARAM_MASK_ENABLED = "maskEnabled";
    static constexpr const char* PARAM_MASK_MODE = "maskMode";
    static constexpr const char* PARAM_MASK_LOW_FREQ = "maskLowFreq";
    static constexpr const char* PARAM_MASK_HIGH_FREQ = "maskHighFreq";
    static constexpr const char* PARAM_MASK_TRANSITION = "maskTransition";
    static constexpr const char* PARAM_DELAY_ENABLED = "delayEnabled";
    static constexpr const char* PARAM_DELAY_TIME = "delayTime";
    static constexpr const char* PARAM_DELAY_SLOPE = "delaySlope";
    static constexpr const char* PARAM_DELAY_FEEDBACK = "delayFeedback";
    static constexpr const char* PARAM_DELAY_DAMPING = "delayDamping";
    static constexpr const char* PARAM_DELAY_MIX = "delayMix";
    static constexpr const char* PARAM_DELAY_GAIN = "delayGain";

    // Valid FFT sizes for SMEAR control (at 44.1kHz)
    // 256 (~6ms), 512 (~12ms), 1024 (~23ms), 2048 (~46ms), 4096 (~93ms)
    static constexpr int FFT_SIZES[] = { 256, 512, 1024, 2048, 4096 };
    static constexpr int NUM_FFT_SIZES = 5;
    static constexpr int MAX_FFT_SIZE = 4096;  // Fixed latency reported to host
    static constexpr float MIN_SMEAR_MS = 5.0f;
    static constexpr float MAX_SMEAR_MS = 123.0f;

    // Get current latency in samples
    int getLatencySamples() const;

    // Spectrum data access for visualization
    // Returns true if new data is available
    bool getSpectrumData(std::array<float, SPECTRUM_SIZE>& data);
    double getSampleRate() const { return currentSampleRate; }
    int getCurrentFFTSize() const { return currentFftSizes[0]; }  // Primary FFT size for display

    // Mask data access for visualization
    const fshift::SpectralMask& getSpectralMask() const { return spectralMask; }
    bool isMaskEnabled() const { return maskEnabled.load(); }

private:
    // Create parameter layout
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Process a single channel
    void processChannel(int channel, juce::AudioBuffer<float>& buffer);

    // Parameter tree state
    juce::AudioProcessorValueTreeState parameters;

    // DSP components (per channel for stereo)
    // Dual processors for crossfade between FFT sizes
    static constexpr int MAX_CHANNELS = 2;
    static constexpr int NUM_PROCESSORS = 2;  // For crossfade between two FFT sizes

    // Arrays: [channel][processor_index]
    std::array<std::array<std::unique_ptr<fshift::STFT>, NUM_PROCESSORS>, MAX_CHANNELS> stftProcessors;
    std::array<std::array<std::unique_ptr<fshift::PhaseVocoder>, NUM_PROCESSORS>, MAX_CHANNELS> phaseVocoders;
    std::array<std::array<std::unique_ptr<fshift::FrequencyShifter>, NUM_PROCESSORS>, MAX_CHANNELS> frequencyShifters;
    std::unique_ptr<fshift::MusicalQuantizer> quantizer;
    fshift::DriftModulator driftModulator;
    fshift::SpectralMask spectralMask;
    std::array<std::array<fshift::SpectralDelay, NUM_PROCESSORS>, MAX_CHANNELS> spectralDelays;

    // Processing parameters (atomic for thread safety)
    std::atomic<float> shiftHz{ 0.0f };
    std::atomic<float> quantizeStrength{ 0.0f };
    std::atomic<float> dryWetMix{ 1.0f };
    std::atomic<bool> usePhaseVocoder{ true };
    std::atomic<int> rootNote{ 60 };  // C4
    std::atomic<int> scaleType{ 0 };  // Major
    std::atomic<float> smearMs{ 93.0f };  // Default to max quality (~93ms at 44.1kHz)
    std::atomic<float> driftAmount{ 0.0f };
    std::atomic<float> driftRate{ 1.0f };
    std::atomic<int> driftMode{ 0 };  // 0 = LFO, 1 = Perlin, 2 = Stochastic
    std::atomic<int> stochasticType{ 0 };  // 0 = Poisson, 1 = RandomWalk, 2 = JumpDiffusion
    std::atomic<float> stochasticDensity{ 0.5f };
    std::atomic<float> stochasticSmoothness{ 0.5f };
    std::atomic<bool> maskEnabled{ false };
    std::atomic<int> maskMode{ 2 };  // 0 = LowPass, 1 = HighPass, 2 = BandPass
    std::atomic<float> maskLowFreq{ 200.0f };
    std::atomic<float> maskHighFreq{ 5000.0f };
    std::atomic<float> maskTransition{ 1.0f };  // Octaves
    std::atomic<bool> maskNeedsUpdate{ true };
    std::atomic<bool> delayEnabled{ false };
    std::atomic<float> delayTime{ 200.0f };
    std::atomic<float> delaySlope{ 0.0f };
    std::atomic<float> delayFeedback{ 30.0f };
    std::atomic<float> delayDamping{ 30.0f };
    std::atomic<float> delayMix{ 50.0f };
    std::atomic<float> delayGain{ 0.0f };  // dB

    // Processing state
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Current FFT settings for dual processors
    std::array<int, NUM_PROCESSORS> currentFftSizes = { 4096, 4096 };
    std::array<int, NUM_PROCESSORS> currentHopSizes = { 1024, 1024 };
    float currentCrossfade = 0.0f;  // 0.0 = use processor 0, 1.0 = use processor 1
    bool useSingleProcessor = true;  // True when exactly on an FFT size boundary

    // Flag to reinitialize DSP on next block
    std::atomic<bool> needsReinit{ false };

    // Reinitialize DSP components with new FFT settings
    void reinitializeDsp();

    // Helper to calculate FFT size from ms latency
    int fftSizeFromMs(float ms) const;

    // Helper to get the two FFT sizes to blend and crossfade amount
    void getBlendParameters(float smearMs, int& fftSize1, int& fftSize2, float& crossfade) const;

    // Input/output buffers for overlap-add (per channel, per processor)
    std::array<std::array<std::vector<float>, NUM_PROCESSORS>, MAX_CHANNELS> inputBuffers;
    std::array<std::array<std::vector<float>, NUM_PROCESSORS>, MAX_CHANNELS> outputBuffers;
    std::array<std::array<int, NUM_PROCESSORS>, MAX_CHANNELS> inputWritePos{};
    std::array<std::array<int, NUM_PROCESSORS>, MAX_CHANNELS> outputReadPos{};

    // Delay compensation buffers (to maintain fixed latency to host)
    std::array<std::vector<float>, MAX_CHANNELS> delayCompBuffers;
    std::array<int, MAX_CHANNELS> delayCompWritePos{};
    std::array<int, MAX_CHANNELS> delayCompReadPos{};

    // Spectrum visualization data (thread-safe FIFO)
    std::array<float, SPECTRUM_SIZE> spectrumData{};
    std::atomic<bool> spectrumDataReady{ false };
    juce::SpinLock spectrumLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyShifterProcessor)
};
