#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"
#include "dsp/STFT.h"
#include "dsp/MusicalQuantizer.h"
#include "dsp/SpectralMask.h"
#include "dsp/SpectralDelay.h"
#include "dsp/HilbertShifter.h"

/**
 * FrequencyShifterProcessor - Main audio processor for the Frequency Shifter plugin.
 *
 * This processor implements harmonic-preserving frequency shifting with:
 * - Enhanced phase vocoder for artifact reduction
 * - Musical scale quantization
 * - Stereo processing support
 */
class FrequencyShifterProcessor : public juce::AudioProcessor,
                                   public juce::AudioProcessorValueTreeState::Listener,
                                   private juce::AsyncUpdater
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

    // Preset management
    PresetManager& getPresetManager() { return presetManager; }

    // Parameter IDs
    static constexpr const char* PARAM_SHIFT_HZ = "shiftHz";
    static constexpr const char* PARAM_QUANTIZE_STRENGTH = "quantizeStrength";
    static constexpr const char* PARAM_ROOT_NOTE = "rootNote";      // Deprecated (backward compat)
    static constexpr const char* PARAM_SCALE_TYPE = "scaleType";    // Deprecated (backward compat)

    // Per-note scale selection (replaces rootNote + scaleType)
    static constexpr const char* PARAM_SCALE_NOTE_PREFIX = "scaleNote";  // scaleNote0..scaleNote11
    static constexpr const char* PARAM_DRY_WET = "dryWet";
    static constexpr const char* PARAM_SMEAR = "smear";  // 5-123ms continuous control
    static constexpr const char* PARAM_LOG_SCALE = "logScale";

    // LFO modulation parameters (modulates frequency shift amount)
    static constexpr const char* PARAM_LFO_DEPTH = "lfoDepth";
    static constexpr const char* PARAM_LFO_DEPTH_MODE = "lfoDepthMode";  // 0 = Hz, 1 = Degrees
    static constexpr const char* PARAM_LFO_RATE = "lfoRate";
    static constexpr const char* PARAM_LFO_SYNC = "lfoSync";
    static constexpr const char* PARAM_LFO_DIVISION = "lfoDivision";
    static constexpr const char* PARAM_LFO_SHAPE = "lfoShape";
    static constexpr const char* PARAM_LFO_ENABLED = "lfoEnabled";  // R3: LFO on/off

    // Delay Time LFO parameters (modulates delay time for dub/tape wobble effects)
    static constexpr const char* PARAM_DLY_LFO_DEPTH = "dlyLfoDepth";
    static constexpr const char* PARAM_DLY_LFO_RATE = "dlyLfoRate";
    static constexpr const char* PARAM_DLY_LFO_SYNC = "dlyLfoSync";
    static constexpr const char* PARAM_DLY_LFO_DIVISION = "dlyLfoDivision";
    static constexpr const char* PARAM_DLY_LFO_SHAPE = "dlyLfoShape";
    static constexpr const char* PARAM_DLY_LFO_ENABLED = "dlyLfoEnabled";  // R3: delay LFO on/off

    static constexpr const char* PARAM_MASK_ENABLED = "maskEnabled";
    static constexpr const char* PARAM_MASK_MODE = "maskMode";
    static constexpr const char* PARAM_MASK_LOW_FREQ = "maskLowFreq";
    static constexpr const char* PARAM_MASK_HIGH_FREQ = "maskHighFreq";
    static constexpr const char* PARAM_MASK_TRANSITION = "maskTransition";
    static constexpr const char* PARAM_DELAY_ENABLED = "delayEnabled";
    static constexpr const char* PARAM_DELAY_TIME = "delayTime";
    static constexpr const char* PARAM_DELAY_SYNC = "delaySync";       // Tempo sync toggle
    static constexpr const char* PARAM_DELAY_DIVISION = "delayDivision"; // Tempo division when synced
    static constexpr const char* PARAM_DELAY_SLOPE = "delaySlope";
    static constexpr const char* PARAM_DELAY_FEEDBACK = "delayFeedback";
    static constexpr const char* PARAM_DELAY_DAMPING = "delayDamping";
    static constexpr const char* PARAM_DELAY_GAIN = "delayGain";

    // Phase 2B: Envelope preservation and transient detection
    static constexpr const char* PARAM_PRESERVE = "preserve";          // 0-100%
    static constexpr const char* PARAM_TRANSIENTS = "transients";      // 0-100%
    static constexpr const char* PARAM_SENSITIVITY = "sensitivity";    // 0-100%
    static constexpr const char* PARAM_PEAK_SNAP = "peakSnap";         // bool: peak-region snapping
    static constexpr const char* PARAM_NOISE_MIX = "noiseMix";         // 0-100%: residual passthrough
    static constexpr const char* PARAM_PEAK_SENS = "peakSens";         // 0-100%: peak threshold

    // Processing mode: Classic (Hilbert) vs Spectral (FFT)
    static constexpr const char* PARAM_PROCESSING_MODE = "processingMode";  // 0=Classic, 1=Spectral

    // WARM: Vintage bandwidth limiting (~10-12kHz rolloff on wet signal)
    static constexpr const char* PARAM_WARM = "warm";

    // Valid FFT sizes for SMEAR control (at 44.1kHz)
    // 256 (~6ms), 512 (~12ms), 1024 (~23ms), 2048 (~46ms), 4096 (~93ms)
    static constexpr int FFT_SIZES[] = { 256, 512, 1024, 2048, 4096 };
    static constexpr int NUM_FFT_SIZES = 5;
    static constexpr int MAX_FFT_SIZE = 4096;  // Fixed latency reported to host for Spectral mode
    static constexpr float MIN_SMEAR_MS = 5.0f;
    static constexpr float MAX_SMEAR_MS = 123.0f;
    static constexpr int CLASSIC_MODE_LATENCY = 12;  // ~0.3ms at 44.1kHz (allpass group delay)

    // Get current latency in samples
    int getLatencySamples() const;

    // Mask data access for visualization
    const fshift::SpectralMask& getSpectralMask() const { return spectralMask; }
    bool isMaskEnabled() const { return maskEnabled.load(); }

    // Stereo decorrelation control (testing feature)
    void setStereoDecorrelate(bool enabled) { stereoDecorrelateEnabled.store(enabled); }
    bool getStereoDecorrelate() const { return stereoDecorrelateEnabled.load(); }

private:
    // Create parameter layout
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Process a single channel
    void processChannel(int channel, juce::AudioBuffer<float>& buffer);

    // Parameter tree state
    juce::AudioProcessorValueTreeState parameters;

    // Preset manager (must be after parameters)
    PresetManager presetManager;

    // DSP components (per channel for stereo)
    // Dual processors for crossfade between FFT sizes
    static constexpr int MAX_CHANNELS = 2;
    static constexpr int NUM_PROCESSORS = 2;  // For crossfade between two FFT sizes

    // Arrays: [channel][processor_index]
    std::array<std::array<std::unique_ptr<fshift::STFT>, NUM_PROCESSORS>, MAX_CHANNELS> stftProcessors;
    // Per-channel quantizers. Each owns its own per-MIDI-note phase accumulators and
    // transient/peak history, so stereo channels don't share (and double-advance) that
    // temporal state — the shared single instance detuned quantized notes in stereo.
    std::array<std::unique_ptr<fshift::MusicalQuantizer>, MAX_CHANNELS> quantizers;
    fshift::SpectralMask spectralMask;
    std::array<std::array<fshift::SpectralDelay, NUM_PROCESSORS>, MAX_CHANNELS> spectralDelays;

    // Hilbert shifter for Classic mode (per channel)
    std::array<fshift::HilbertShifter, MAX_CHANNELS> hilbertShifters;

    // Processing parameters (atomic for thread safety)
    std::atomic<float> shiftHz{ 0.0f };
    std::atomic<float> quantizeStrength{ 0.0f };
    std::atomic<float> dryWetMix{ 1.0f };
    std::atomic<int> rootNote{ 60 };  // C4 (deprecated, backward compat)
    std::atomic<int> scaleType{ 0 };  // Major (deprecated, backward compat)
    std::array<std::atomic<bool>, 12> scaleNotes{};  // Per-note selection (C=0, B=11)
    std::atomic<float> smearMs{ 93.0f };  // Default to max quality (~93ms at 44.1kHz)

    // LFO modulation state
    std::atomic<float> lfoDepth{ 0.0f };      // 0-5000 Hz or degrees
    std::atomic<int> lfoDepthMode{ 0 };       // 0 = Hz, 1 = Degrees
    std::atomic<float> lfoRate{ 1.0f };       // 0.01-60 Hz when not synced
    std::atomic<bool> lfoSync{ false };       // Tempo sync on/off
    std::atomic<int> lfoDivision{ 5 };        // Tempo division index 5 = 1/4 (matches param-layout default)
    std::atomic<int> lfoShape{ 0 };           // 0=Sine, 1=Tri, 2=Saw, 3=InvSaw, 4=Random
    std::atomic<bool> lfoEnabled{ false };    // R3: LFO on/off (default off, like the delay)

    // LFO tempo sync division multipliers (in beats, i.e. quarter notes)
    static constexpr int NUM_LFO_DIVISIONS = 14;
    static constexpr float LFO_DIVISION_BEATS[NUM_LFO_DIVISIONS] = {
        0.125f,  // 1/32
        0.25f,   // 1/16
        0.375f,  // 1/16 D (dotted)
        0.5f,    // 1/8
        0.75f,   // 1/8 D (dotted)
        1.0f,    // 1/4
        1.5f,    // 1/4 D (dotted)
        2.0f,    // 1/2
        4.0f,    // 1/1 (1 bar)
        8.0f,    // 2/1 (2 bars)
        12.0f,   // 3/1 (3 bars)
        16.0f,   // 4/1 (4 bars)
        32.0f,   // 8/1 (8 bars)
        64.0f    // 16/1 (16 bars)
    };

    // LFO phase (0-1)
    double lfoPhase = 0.0;
    float lastRandomValue = 0.0f;  // For Random shape S&H

    // Transport state for LFO retrigger-on-play (edge-detected against host isPlaying)
    bool wasPlaying = false;

    // Delay Time LFO state (independent from frequency LFO)
    std::atomic<float> dlyLfoDepth{ 0.0f };    // 0-1000 ms
    std::atomic<float> dlyLfoRate{ 1.0f };     // 0.01-20 Hz when not synced
    std::atomic<bool> dlyLfoSync{ false };     // Tempo sync on/off
    std::atomic<int> dlyLfoDivision{ 5 };      // Tempo division index 5 = 1/4 (matches param-layout default)
    std::atomic<int> dlyLfoShape{ 0 };         // 0=Sine, 1=Tri, 2=Saw, 3=InvSaw, 4=Random
    std::atomic<bool> dlyLfoEnabled{ false };  // R3: delay LFO on/off (default off)

    // Delay LFO phase (0-1) - independent from frequency LFO
    double dlyLfoPhase = 0.0;
    float dlyLastRandomValue = 0.0f;  // For Random shape S&H

    std::atomic<bool> maskEnabled{ false };
    std::atomic<int> maskMode{ 2 };  // 0 = LowPass, 1 = HighPass, 2 = BandPass
    std::atomic<float> maskLowFreq{ 200.0f };
    std::atomic<float> maskHighFreq{ 5000.0f };
    std::atomic<float> maskTransition{ 1.0f };  // Octaves
    std::atomic<bool> maskNeedsUpdate{ true };
    std::atomic<bool> delayEnabled{ false };
    std::atomic<float> delayTime{ 200.0f };
    std::atomic<bool> delaySync{ false };      // Tempo sync on/off
    std::atomic<int> delayDivision{ 5 };       // Tempo division index 5 = 1/4 (matches param-layout default)
    std::atomic<float> delaySlope{ 0.0f };
    std::atomic<double> hostBpm{ 120.0 };      // Cached host tempo
    std::atomic<float> delayFeedback{ 30.0f };
    std::atomic<float> delayDamping{ 30.0f };
    std::atomic<float> delayGain{ 0.0f };  // dB
    std::atomic<bool> delayNeedsUpdate{ false };  // Flag for thread-safe deferred updates

    // Phase 2B: Envelope preservation and transient detection
    std::atomic<float> preserveAmount{ 0.0f };     // 0.0 - 1.0
    std::atomic<float> transientAmount{ 0.0f };    // 0.0 - 1.0
    std::atomic<float> transientSensitivity{ 0.5f }; // 0.0 - 1.0 (default 50%)

    // Peak-region snapping + sines/noise split (mirror the quantizer members; defaults must match)
    std::atomic<bool>  peakSnapEnabled{ false };
    std::atomic<float> noiseMix{ 0.7f };           // 0.0 - 1.0
    std::atomic<float> peakSensitivity{ 0.5f };    // 0.0 - 1.0

    // Processing mode: 0=Classic (Hilbert), 1=Spectral (FFT)
    std::atomic<int> processingMode{ 1 };  // Default to Spectral mode

    // WARM: Vintage bandwidth limiting
    std::atomic<bool> warmEnabled{ false };

    // WARM lowpass filter state (2-pole Butterworth ~10-12kHz)
    // Applied to wet signal only, before feedback path for "melting" effect
    std::array<std::array<float, 4>, MAX_CHANNELS> warmFilterState{};  // [x1, x2, y1, y2] per channel
    std::array<float, 5> warmFilterCoeffs{};  // Biquad coefficients [b0, b1, b2, a1, a2]

    // Mode switching crossfade state
    std::atomic<bool> needsModeSwitch{ false };
    float modeCrossfadeProgress = 1.0f;  // 1.0 = stable, <1.0 = transitioning
    int targetMode = 1;  // Mode we're switching to
    int previousMode = 1;  // Mode we're switching from
    static constexpr float MODE_CROSSFADE_MS = 15.0f;  // 15ms crossfade duration

    // Stereo decorrelation (testing feature)
    // Applies 0.06ms delay to left channel to reduce phase-locked resonance
    std::atomic<bool> stereoDecorrelateEnabled{ false };
    std::vector<float> leftDecorrelateBuffer;
    int decorrelateWritePos = 0;
    int decorrelateDelaySamples = 0;

    // Phase 2B+ Amplitude envelope tracking state (per channel)
    // For matching output amplitude dynamics to input dynamics
    std::array<float, MAX_CHANNELS> inputEnvelope{};   // Input amplitude follower
    std::array<float, MAX_CHANNELS> outputEnvelope{};  // Output amplitude follower
    // Envelope follower coefficients (computed in prepareToPlay)
    float envAttackCoeff = 0.0f;   // ~1ms attack
    float envReleaseCoeff = 0.0f;  // ~50ms release

    // "Tones Only" (peak-snap) amplitude-envelope imposition state. The source's fast amplitude
    // envelope (from the latency-aligned dry input, used ONLY as a control signal) is imposed on
    // the fully-shifted wet, so the source's punch and dynamics survive without any dry signal
    // entering the output — the audio stays 100% the effect. See processBlock for the gain law.
    std::array<float, MAX_CHANNELS> srcEnv{};   // source (control-only) amplitude envelope
    std::array<float, MAX_CHANNELS> wetEnv{};   // wet (shifted) amplitude envelope
    float punchEnvAtkCoeff = 0.0f;   // ~0.5ms — catch the onset
    float punchEnvRelCoeff = 0.0f;   // ~50ms — track decays, duck the ringing tail

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

    // Latency changes must never call setLatencySamples() from the audio thread: it notifies
    // the host synchronously and can block the audio thread on a WaitableEvent (the pluginval
    // automation hang on mode switches). requestLatency() applies it directly on the message
    // thread, else defers it to the message thread via AsyncUpdater.
    std::atomic<int> pendingLatencySamples{ -1 };
    void requestLatency(int latencySamples);
    void handleAsyncUpdate() override;

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

    // Dry signal delay buffer (to align dry with wet when mixing)
    // Must delay by full reported latency (MAX_FFT_SIZE samples)
    std::array<std::vector<float>, MAX_CHANNELS> dryDelayBuffers;
    std::array<int, MAX_CHANNELS> dryDelayWritePos{};

    // Time-domain feedback buffer for cascading pitch shifts
    // Feedback routes back to INPUT of shifter, so each repeat gets shifted again
    // Signal flow: Input + Feedback → FFT → Shift → Spectral Delay → IFFT → Output
    //                      ↑_____________________________________________↓
    static constexpr int MAX_FEEDBACK_DELAY_SAMPLES = 96000;  // ~2 seconds at 48kHz
    std::array<std::vector<float>, MAX_CHANNELS> feedbackBuffers;
    std::array<int, MAX_CHANNELS> feedbackWritePos{};

    // Feedback-loop source (per channel): overlap-add target for the IFFT of the
    // pre-envelope-preservation spectrum. Recirculating THIS instead of the audible,
    // envelope-boosted output keeps the spectral-envelope make-up gain out of the loop.
    // Sized to match outputBuffers[ch][0] so read/write positions stay aligned.
    std::array<std::vector<float>, MAX_CHANNELS> feedbackSourceBuffers;

    // Per-sample smoothed delay time (in samples) for glitch-free LFO modulation
    // One-pole exponential filter mimics analog tape motor inertia
    std::array<float, MAX_CHANNELS> smoothedDelayTimeSamples{};
    float delayTimeSmoothCoeff = 0.0f;  // One-pole coefficient (~5ms time constant)

    // One-pole smoother for frequency LFO depth (prevents clicks on slider jumps)
    float smoothedLfoDepth = 0.0f;
    float lfoDepthSmoothCoeff = 0.0f;  // ~20ms time constant

    // Additional one-pole smoothers for params that would otherwise produce zipper noise
    // when changed during sustained audio. Block-rate updates; coeff is raised to numSamples.
    float smoothedShiftHz = 0.0f;
    float shiftHzSmoothCoeff = 0.0f;          // ~10ms — fastest, most audible if delayed
    float smoothedQuantizeStrength = 0.0f;
    float quantizeStrengthSmoothCoeff = 0.0f; // ~30ms — modulates spectral redistribution
    float smoothedLfoRate = 0.0f;
    float lfoRateSmoothCoeff = 0.0f;          // ~50ms — slowest, large jumps are jarring

    // Simple one-pole lowpass for feedback damping
    std::array<float, MAX_CHANNELS> feedbackFilterState{};
    float feedbackFilterCoeff = 0.5f;  // Calculated from damping parameter

    // Two-pole highpass filter (150Hz) to prevent low frequency buildup
    // Biquad state: [x1, x2, y1, y2] per channel
    std::array<std::array<float, 4>, MAX_CHANNELS> feedbackHpfState{};
    // Biquad coefficients: [b0, b1, b2, a1, a2] (a0 normalized to 1)
    std::array<float, 5> feedbackHpfCoeffs{};

    // 4-pole (24dB/oct) lowpass filter (~4kHz) for Classic mode feedback
    // Aggressive filtering prevents aliasing artifacts from accumulating in delay taps
    // Two cascaded biquad stages for steeper rolloff (Eventide H3000/Orville style)
    std::array<std::array<float, 4>, MAX_CHANNELS> feedbackLpf1State{};  // First biquad
    std::array<std::array<float, 4>, MAX_CHANNELS> feedbackLpf2State{};  // Second biquad
    std::array<float, 5> feedbackLpfCoeffs{};  // Shared coefficients for both stages

    // Cross-coupled feedback for Classic mode (L→R, R→L)
    // Creates complex interference and stereo width like Eventide Orville
    std::array<float, MAX_CHANNELS> crossFeedbackSample{};

    // Drift LFO for Classic mode - subtle modulation keeps feedback alive
    double driftLfoPhase = 0.0;
    static constexpr float DRIFT_LFO_RATE = 0.2f;   // ~0.2Hz for slow organic movement
    static constexpr float DRIFT_LFO_DEPTH = 0.5f;  // ~0.5Hz variation

    // Eventide-style feedback filters for Classic mode
    // HPF (150Hz) prevents low-frequency buildup during delay modulation
    // 2nd order LPF provides anti-aliasing without ringing artifacts
    std::array<float, MAX_CHANNELS> classicDcBlockState{};  // Legacy (unused, kept for compat)
    std::array<std::array<float, 4>, MAX_CHANNELS> classicFbHpfState{};  // 150Hz HPF biquad state per channel
    std::array<std::array<float, 8>, MAX_CHANNELS> classicFbLpfState{};  // LPF state (only first stage used now)
    std::array<float, 10> classicFbLpfCoeffs{};  // Coefficients for 2 cascaded biquads (5 each)
    std::array<float, MAX_CHANNELS> classicFbDampState{};  // R5: one-pole damping LPF state (Classic feedback)

    // Anti-aliasing LPF on Classic mode output (~16kHz, 2nd order Butterworth)
    // Removes Nyquist-aliased content from large upward frequency shifts
    std::array<std::array<float, 4>, MAX_CHANNELS> classicOutputLpfState{};
    std::array<float, 5> classicOutputLpfCoeffs{};

    // Tempo sync division multipliers (relative to quarter note)
    static constexpr int NUM_TEMPO_DIVISIONS = 14;
    static constexpr float TEMPO_DIVISION_MULTIPLIERS[NUM_TEMPO_DIVISIONS] = {
        0.125f,   // 1/32
        0.25f,    // 1/16
        0.375f,   // 1/16 D
        0.5f,     // 1/8
        0.75f,    // 1/8 D
        1.0f,     // 1/4
        1.5f,     // 1/4 D
        2.0f,     // 1/2
        4.0f,     // 1/1
        8.0f,     // 2/1
        12.0f,    // 3/1
        16.0f,    // 4/1
        32.0f,    // 8/1
        64.0f     // 16/1
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyShifterProcessor)
};
