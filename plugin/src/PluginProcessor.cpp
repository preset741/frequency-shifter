#include "PluginProcessor.h"
#if HOLY_SHIFTER_USE_WEBVIEW
 #include "WebViewEditor.h"
#else
 #include "VisageHostEditor.h"
#endif
#include "dsp/Scales.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

FrequencyShifterProcessor::FrequencyShifterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("FrequencyShifter"), createParameterLayout()),
      presetManager(parameters)
{
    // Add parameter listeners
    parameters.addParameterListener(PARAM_SHIFT_HZ, this);
    parameters.addParameterListener(PARAM_QUANTIZE_STRENGTH, this);
    parameters.addParameterListener(PARAM_ROOT_NOTE, this);
    parameters.addParameterListener(PARAM_SCALE_TYPE, this);
    parameters.addParameterListener(PARAM_DRY_WET, this);
    parameters.addParameterListener(PARAM_SMEAR, this);
    parameters.addParameterListener(PARAM_LFO_DEPTH, this);
    parameters.addParameterListener(PARAM_LFO_DEPTH_MODE, this);
    parameters.addParameterListener(PARAM_LFO_RATE, this);
    parameters.addParameterListener(PARAM_LFO_SYNC, this);
    parameters.addParameterListener(PARAM_LFO_DIVISION, this);
    parameters.addParameterListener(PARAM_LFO_SHAPE, this);
    parameters.addParameterListener(PARAM_LFO_ENABLED, this);
    parameters.addParameterListener(PARAM_DLY_LFO_DEPTH, this);
    parameters.addParameterListener(PARAM_DLY_LFO_RATE, this);
    parameters.addParameterListener(PARAM_DLY_LFO_SYNC, this);
    parameters.addParameterListener(PARAM_DLY_LFO_DIVISION, this);
    parameters.addParameterListener(PARAM_DLY_LFO_SHAPE, this);
    parameters.addParameterListener(PARAM_DLY_LFO_ENABLED, this);
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
    parameters.addParameterListener(PARAM_DELAY_GAIN, this);
    parameters.addParameterListener(PARAM_PRESERVE, this);
    parameters.addParameterListener(PARAM_TRANSIENTS, this);
    parameters.addParameterListener(PARAM_SENSITIVITY, this);
    parameters.addParameterListener(PARAM_PEAK_SNAP, this);
    parameters.addParameterListener(PARAM_NOISE_MIX, this);
    parameters.addParameterListener(PARAM_PEAK_SENS, this);
    parameters.addParameterListener(PARAM_PROCESSING_MODE, this);
    parameters.addParameterListener(PARAM_WARM, this);

    // Add listeners for per-note scale selection
    for (int i = 0; i < 12; ++i)
        parameters.addParameterListener(juce::String(PARAM_SCALE_NOTE_PREFIX) + juce::String(i), this);

    // Initialize scale notes to all ON (chromatic)
    for (int i = 0; i < 12; ++i)
        scaleNotes[i].store(true);

    // Initialize per-channel quantizers with default scale (C Major)
    for (auto& q : quantizers)
        q = std::make_unique<fshift::MusicalQuantizer>(60, fshift::ScaleType::Major);
}

FrequencyShifterProcessor::~FrequencyShifterProcessor()
{
    cancelPendingUpdate();  // ensure no deferred latency callback fires during teardown
    parameters.removeParameterListener(PARAM_SHIFT_HZ, this);
    parameters.removeParameterListener(PARAM_QUANTIZE_STRENGTH, this);
    parameters.removeParameterListener(PARAM_ROOT_NOTE, this);
    parameters.removeParameterListener(PARAM_SCALE_TYPE, this);
    parameters.removeParameterListener(PARAM_DRY_WET, this);
    parameters.removeParameterListener(PARAM_SMEAR, this);
    parameters.removeParameterListener(PARAM_LFO_DEPTH, this);
    parameters.removeParameterListener(PARAM_LFO_DEPTH_MODE, this);
    parameters.removeParameterListener(PARAM_LFO_RATE, this);
    parameters.removeParameterListener(PARAM_LFO_SYNC, this);
    parameters.removeParameterListener(PARAM_LFO_DIVISION, this);
    parameters.removeParameterListener(PARAM_LFO_SHAPE, this);
    parameters.removeParameterListener(PARAM_LFO_ENABLED, this);
    parameters.removeParameterListener(PARAM_DLY_LFO_DEPTH, this);
    parameters.removeParameterListener(PARAM_DLY_LFO_RATE, this);
    parameters.removeParameterListener(PARAM_DLY_LFO_SYNC, this);
    parameters.removeParameterListener(PARAM_DLY_LFO_DIVISION, this);
    parameters.removeParameterListener(PARAM_DLY_LFO_SHAPE, this);
    parameters.removeParameterListener(PARAM_DLY_LFO_ENABLED, this);
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
    parameters.removeParameterListener(PARAM_DELAY_GAIN, this);
    parameters.removeParameterListener(PARAM_PRESERVE, this);
    parameters.removeParameterListener(PARAM_TRANSIENTS, this);
    parameters.removeParameterListener(PARAM_SENSITIVITY, this);
    parameters.removeParameterListener(PARAM_PEAK_SNAP, this);
    parameters.removeParameterListener(PARAM_NOISE_MIX, this);
    parameters.removeParameterListener(PARAM_PEAK_SENS, this);
    parameters.removeParameterListener(PARAM_PROCESSING_MODE, this);
    parameters.removeParameterListener(PARAM_WARM, this);

    for (int i = 0; i < 12; ++i)
        parameters.removeParameterListener(juce::String(PARAM_SCALE_NOTE_PREFIX) + juce::String(i), this);
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
        0.0f,  // R1: slider rests at 0; consumer remaps to a 0.2 floor (see processBlock).
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

    // Per-note scale selection (12 pitch classes)
    // Default: all notes ON (chromatic = no filtering)
    {
        juce::StringArray pitchNames{ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i)
        {
            auto paramId = juce::String(PARAM_SCALE_NOTE_PREFIX) + juce::String(i);
            params.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{ paramId, 1 },
                "Scale " + pitchNames[i],
                true));  // Default all ON
        }
    }

    // Dry/Wet mix (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DRY_WET, 1 },
        "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

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

    // === LFO Modulation Parameters ===

    // LFO depth (0-5000 Hz or scale degrees, power curve for fine control at low values)
    // 40% of slider ≈ 40 Hz, 50% ≈ 130 Hz, 75% ≈ 1100 Hz
    constexpr float lfoDepthExp = 5.3f;
    auto lfoDepthRange = juce::NormalisableRange<float>(0.0f, 5000.0f,
        [](float start, float end, float normalised) {
            return std::pow(normalised, lfoDepthExp) * (end - start) + start;
        },
        [](float start, float end, float value) {
            return std::pow((value - start) / (end - start), 1.0f / lfoDepthExp);
        },
        [](float start, float end, float value) {
            (void)start; (void)end;
            return std::round(value * 1000.0f) / 1000.0f;  // snap to 0.001 (fine low-end readout)
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_LFO_DEPTH, 1 },
        "LFO Depth",
        lfoDepthRange,
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    // LFO depth mode (Hz or Degrees)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_LFO_DEPTH_MODE, 1 },
        "Depth Mode",
        juce::StringArray{ "Hz", "Deg" },
        0));  // Default to Hz

    // LFO rate (0.01-60 Hz, log scale). Upper end intentionally well above classic
    // chorus rates (~0.5-8 Hz) so chorus sits comfortably in the middle of the
    // slider's log travel, with flutter / fast-modulation territory above.
    auto lfoRateRange = juce::NormalisableRange<float>(0.01f, 60.0f,
        [](float start, float end, float normalised) {
            return start * std::pow(end / start, normalised);
        },
        [](float start, float end, float value) {
            return std::log(value / start) / std::log(end / start);
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_LFO_RATE, 1 },
        "LFO Rate",
        lfoRateRange,
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // LFO tempo sync toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_LFO_SYNC, 1 },
        "LFO Sync",
        false));

    // LFO tempo division (when synced)
    juce::StringArray lfoDivisionNames{
        "1/32", "1/16", "1/16 D", "1/8", "1/8 D", "1/4", "1/4 D",
        "1/2", "1/1", "2/1", "3/1", "4/1", "8/1", "16/1"
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_LFO_DIVISION, 1 },
        "LFO Division",
        lfoDivisionNames,
        5));  // Default to 1/4

    // LFO shape
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_LFO_SHAPE, 1 },
        "LFO Shape",
        juce::StringArray{ "Sine", "Triangle", "Saw", "Inv Saw", "Random", "Square" },
        0));  // Default to Sine

    // LFO on/off (R3) — default off, matching the delay's enable
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_LFO_ENABLED, 1 },
        "LFO Enabled",
        false));

    // === Delay Time LFO Parameters ===

    // Delay LFO depth (0-1000 ms, power curve for fine control at low values)
    // 40% of slider ≈ 8 ms, 50% ≈ 26 ms, 75% ≈ 220 ms
    constexpr float dlyLfoDepthExp = 5.3f;
    auto dlyLfoDepthRange = juce::NormalisableRange<float>(0.0f, 1000.0f,
        [](float start, float end, float normalised) {
            return std::pow(normalised, dlyLfoDepthExp) * (end - start) + start;
        },
        [](float start, float end, float value) {
            return std::pow((value - start) / (end - start), 1.0f / dlyLfoDepthExp);
        },
        [](float start, float end, float value) {
            (void)start; (void)end;
            return std::round(value * 1000.0f) / 1000.0f;  // snap to 0.001 (fine low-end readout)
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DLY_LFO_DEPTH, 1 },
        "Delay LFO Depth",
        dlyLfoDepthRange,
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Delay LFO rate (0.01-20 Hz, log scale)
    auto dlyLfoRateRange = juce::NormalisableRange<float>(0.01f, 20.0f,
        [](float start, float end, float normalised) {
            return start * std::pow(end / start, normalised);
        },
        [](float start, float end, float value) {
            return std::log(value / start) / std::log(end / start);
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_DLY_LFO_RATE, 1 },
        "Delay LFO Rate",
        dlyLfoRateRange,
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Delay LFO tempo sync toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_DLY_LFO_SYNC, 1 },
        "Delay LFO Sync",
        false));

    // Delay LFO tempo division (when synced) - reuse same divisions as frequency LFO
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_DLY_LFO_DIVISION, 1 },
        "Delay LFO Division",
        lfoDivisionNames,
        5));  // Default to 1/4

    // Delay LFO shape
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_DLY_LFO_SHAPE, 1 },
        "Delay LFO Shape",
        juce::StringArray{ "Sine", "Triangle", "Saw", "Inv Saw", "Random", "Square" },
        0));  // Default to Sine

    // Delay LFO on/off (R3) — default off
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_DLY_LFO_ENABLED, 1 },
        "Delay LFO Enabled",
        false));

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
        "1/32", "1/16", "1/16 D", "1/8", "1/8 D", "1/4", "1/4 D",
        "1/2", "1/1", "2/1", "3/1", "4/1", "8/1", "16/1"
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_DELAY_DIVISION, 1 },
        "Division",
        divisionNames,
        5));  // Default to 1/4

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

    // === Peak-region snapping + sines/noise split (Spectral mode) ===

    // PEAK SNAP: snap only tonal peaks (+ their lobes); pass non-peak "noise" through.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_PEAK_SNAP, 1 },
        "Tones Only",
        false));  // Default off => legacy per-bin snapping (current behaviour)

    // NOISE: how much of the non-peak broadband residual passes through (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_NOISE_MIX, 1 },
        "Texture",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        70.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // PEAK SENS: peak-detection threshold — higher = more of the spectrum counts as tonal (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ PARAM_PEAK_SENS, 1 },
        "Density",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Processing mode: Classic (Hilbert) vs Spectral (FFT)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ PARAM_PROCESSING_MODE, 1 },
        "Mode",
        juce::StringArray{ "Classic", "Spectral" },
        1));  // Default to Spectral

    // WARM: Vintage bandwidth limiting (~10-12kHz rolloff on wet signal)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ PARAM_WARM, 1 },
        "Warm",
        false));  // Default to off

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
        // Deprecated — kept for backward compat state loading
        int midiNote = static_cast<int>(newValue) + 60;
        rootNote.store(midiNote);
    }
    else if (parameterID == PARAM_SCALE_TYPE)
    {
        // Deprecated — kept for backward compat state loading
        int scale = static_cast<int>(newValue);
        scaleType.store(scale);
    }
    else if (parameterID.startsWith(PARAM_SCALE_NOTE_PREFIX))
    {
        // Extract note index from "scaleNote0" .. "scaleNote11"
        int noteIdx = parameterID.substring(juce::String(PARAM_SCALE_NOTE_PREFIX).length()).getIntValue();
        if (noteIdx >= 0 && noteIdx < 12)
        {
            scaleNotes[noteIdx].store(newValue > 0.5f);
            std::array<bool, 12> notes;
            for (int i = 0; i < 12; ++i)
                notes[i] = scaleNotes[i].load();
            for (auto& q : quantizers)
                if (q) q->setActiveNotes(notes);
        }
    }
    else if (parameterID == PARAM_DRY_WET)
    {
        dryWetMix.store(newValue / 100.0f);
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
    else if (parameterID == PARAM_LFO_DEPTH)
    {
        lfoDepth.store(newValue);
    }
    else if (parameterID == PARAM_LFO_DEPTH_MODE)
    {
        lfoDepthMode.store(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_LFO_RATE)
    {
        lfoRate.store(newValue);
    }
    else if (parameterID == PARAM_LFO_SYNC)
    {
        lfoSync.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_LFO_DIVISION)
    {
        lfoDivision.store(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_LFO_SHAPE)
    {
        lfoShape.store(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_LFO_ENABLED)
    {
        lfoEnabled.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_DLY_LFO_DEPTH)
    {
        dlyLfoDepth.store(newValue);
    }
    else if (parameterID == PARAM_DLY_LFO_RATE)
    {
        dlyLfoRate.store(newValue);
    }
    else if (parameterID == PARAM_DLY_LFO_SYNC)
    {
        dlyLfoSync.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_DLY_LFO_DIVISION)
    {
        dlyLfoDivision.store(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_DLY_LFO_SHAPE)
    {
        dlyLfoShape.store(static_cast<int>(newValue));
    }
    else if (parameterID == PARAM_DLY_LFO_ENABLED)
    {
        dlyLfoEnabled.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_MASK_ENABLED)
    {
        maskEnabled.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_MASK_MODE)
    {
        int mode = static_cast<int>(newValue);
        maskMode.store(mode);
        // Defer spectralMask.setMode() to audio thread for thread safety
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_MASK_LOW_FREQ)
    {
        maskLowFreq.store(newValue);
        // Defer spectralMask.setLowFreq() to audio thread for thread safety
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_MASK_HIGH_FREQ)
    {
        maskHighFreq.store(newValue);
        // Defer spectralMask.setHighFreq() to audio thread for thread safety
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_MASK_TRANSITION)
    {
        maskTransition.store(newValue);
        // Defer spectralMask.setTransition() to audio thread for thread safety
        maskNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_DELAY_ENABLED)
    {
        delayEnabled.store(newValue > 0.5f);
    }
    else if (parameterID == PARAM_DELAY_TIME)
    {
        delayTime.store(newValue);
        // Defer spectralDelay updates to audio thread for thread safety
        delayNeedsUpdate.store(true);
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
        // Defer spectralDelay updates to audio thread for thread safety
        delayNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_DELAY_FEEDBACK)
    {
        delayFeedback.store(newValue);
        // Defer spectralDelay updates to audio thread for thread safety
        delayNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_DELAY_DAMPING)
    {
        delayDamping.store(newValue);
        // Defer spectralDelay updates to audio thread for thread safety
        delayNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_DELAY_GAIN)
    {
        delayGain.store(newValue);
        // Defer spectralDelay updates to audio thread for thread safety
        delayNeedsUpdate.store(true);
    }
    else if (parameterID == PARAM_PRESERVE)
    {
        preserveAmount.store(newValue / 100.0f);
        for (auto& q : quantizers)
            if (q) q->setPreserveAmount(newValue / 100.0f);
    }
    else if (parameterID == PARAM_TRANSIENTS)
    {
        transientAmount.store(newValue / 100.0f);
        for (auto& q : quantizers)
            if (q) q->setTransientAmount(newValue / 100.0f);
    }
    else if (parameterID == PARAM_SENSITIVITY)
    {
        transientSensitivity.store(newValue / 100.0f);
        for (auto& q : quantizers)
            if (q) q->setTransientSensitivity(newValue / 100.0f);
    }
    else if (parameterID == PARAM_PEAK_SNAP)
    {
        bool on = newValue > 0.5f;
        peakSnapEnabled.store(on);
        for (auto& q : quantizers)
            if (q) q->setPeakSnap(on);
    }
    else if (parameterID == PARAM_NOISE_MIX)
    {
        noiseMix.store(newValue / 100.0f);
        for (auto& q : quantizers)
            if (q) q->setNoiseMix(newValue / 100.0f);
    }
    else if (parameterID == PARAM_PEAK_SENS)
    {
        peakSensitivity.store(newValue / 100.0f);
        for (auto& q : quantizers)
            if (q) q->setPeakSensitivity(newValue / 100.0f);
    }
    else if (parameterID == PARAM_PROCESSING_MODE)
    {
        int newMode = static_cast<int>(newValue);
        int currentMode = processingMode.load();
        if (newMode != currentMode)
        {
            // Initiate crossfade to new mode
            previousMode = currentMode;
            targetMode = newMode;
            modeCrossfadeProgress = 0.0f;
            needsModeSwitch.store(true);
        }
    }
    else if (parameterID == PARAM_WARM)
    {
        warmEnabled.store(newValue > 0.5f);
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

    // "Tones Only" amplitude-envelope imposition: one-pole coeffs (exp(-1/(fs*tau))).
    auto onePole = [sr = static_cast<float>(sampleRate)](float ms) {
        return std::exp(-1.0f / (sr * ms / 1000.0f));
    };
    punchEnvAtkCoeff = onePole(0.5f);
    punchEnvRelCoeff = onePole(50.0f);
    srcEnv.fill(0.0f);
    wetEnv.fill(0.0f);

    // Initialize delay time smoother (~5ms one-pole for glitch-free LFO modulation)
    float smoothTimeMs = 5.0f;
    delayTimeSmoothCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * smoothTimeMs / 1000.0f));
    float defaultDelaySamples = 200.0f * static_cast<float>(sampleRate) / 1000.0f;
    smoothedDelayTimeSamples.fill(defaultDelaySamples);

    // Initialize LFO depth smoother (~20ms one-pole to prevent clicks on slider jumps)
    float lfoDepthSmoothMs = 20.0f;
    lfoDepthSmoothCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * lfoDepthSmoothMs / 1000.0f));
    smoothedLfoDepth = lfoDepth.load();

    // Additional param smoothers — time constants tuned per parameter character.
    auto makeSmoothCoeff = [sampleRate](float timeMs) {
        return std::exp(-1.0f / (static_cast<float>(sampleRate) * timeMs / 1000.0f));
    };
    shiftHzSmoothCoeff          = makeSmoothCoeff(10.0f);
    quantizeStrengthSmoothCoeff = makeSmoothCoeff(30.0f);
    lfoRateSmoothCoeff          = makeSmoothCoeff(50.0f);
    smoothedShiftHz             = shiftHz.load();
    smoothedQuantizeStrength    = quantizeStrength.load();
    smoothedLfoRate             = lfoRate.load();

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

    // OPTIMIZATION: Always snap to nearest FFT size instead of crossfading between two
    // This halves CPU usage by only running ONE FFT processor instead of two
    // Previous approach ran dual processors when SMEAR was between FFT size boundaries,
    // causing 40%+ CPU usage in the 50-90ms range

    // Find the closest FFT size
    int closestSize = FFT_SIZES[0];
    float closestDistance = std::abs(targetSamples - static_cast<float>(FFT_SIZES[0]));

    for (int i = 1; i < NUM_FFT_SIZES; ++i)
    {
        float distance = std::abs(targetSamples - static_cast<float>(FFT_SIZES[i]));
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestSize = FFT_SIZES[i];
        }
    }

    // Use same size for both processors (forces single processor mode)
    fftSize1 = closestSize;
    fftSize2 = closestSize;
    crossfade = 0.0f;
}

void FrequencyShifterProcessor::requestLatency(int latencySamples)
{
    // setLatencySamples() notifies the host synchronously; from the audio thread that can
    // block on a WaitableEvent (the pluginval Automation hang on mode changes). Apply it
    // directly only on the message thread; otherwise defer it to the message thread.
    if (juce::MessageManager::existsAndIsCurrentThread())
        setLatencySamples(latencySamples);
    else
    {
        pendingLatencySamples.store(latencySamples);
        triggerAsyncUpdate();
    }
}

void FrequencyShifterProcessor::handleAsyncUpdate()
{
    const int latency = pendingLatencySamples.exchange(-1);
    if (latency >= 0)
        setLatencySamples(latency);
}

void FrequencyShifterProcessor::reinitializeDsp()
{
    // Get FFT size based on SMEAR setting (always snaps to nearest valid size)
    float smear = smearMs.load();
    int fftSize1, fftSize2;
    float crossfade;
    getBlendParameters(smear, fftSize1, fftSize2, crossfade);

    currentFftSizes[0] = fftSize1;
    currentFftSizes[1] = fftSize2;  // Same as fftSize1 after optimization
    currentHopSizes[0] = fftSize1 / 4;  // Standard 75% overlap
    currentHopSizes[1] = fftSize2 / 4;
    currentCrossfade = crossfade;  // Always 0.0 after optimization

    // OPTIMIZATION: Always single processor mode now (getBlendParameters sets fftSize1==fftSize2)
    // This halves CPU usage compared to dual-processor crossfade approach
    useSingleProcessor = true;

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

    // Reset LFO phase and depth smoother
    lfoPhase = 0.0;
    dlyLfoPhase = 0.0;
    lastRandomValue = 0.0f;
    wasPlaying = false;  // force a retrigger on the first play after (re)prepare
    smoothedLfoDepth          = lfoDepth.load();
    smoothedShiftHz           = shiftHz.load();
    smoothedQuantizeStrength  = quantizeStrength.load();
    smoothedLfoRate           = lfoRate.load();

    // Prepare per-channel quantizers with primary FFT settings for phase continuity (Phase 2A.3)
    for (auto& q : quantizers)
        if (q)
            q->prepare(currentSampleRate, currentFftSizes[0], currentHopSizes[0]);

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
            spectralDelays[ch][proc].setGain(delayGain.load());
        }

        // Initialize time-domain feedback buffer for cascading pitch shifts
        feedbackBuffers[static_cast<size_t>(ch)].resize(MAX_FEEDBACK_DELAY_SAMPLES, 0.0f);
        feedbackWritePos[static_cast<size_t>(ch)] = 0;
        feedbackFilterState[static_cast<size_t>(ch)] = 0.0f;

        // Feedback-loop source overlap-add buffer — proc-0 sized so its positions track
        // outputBuffers[ch][0] (read with the same outputReadPos index).
        feedbackSourceBuffers[static_cast<size_t>(ch)].assign(static_cast<size_t>(currentFftSizes[0]) * 2, 0.0f);

        // Initialize Hilbert shifter for Classic mode
        hilbertShifters[static_cast<size_t>(ch)].prepare(currentSampleRate);
        hilbertShifters[static_cast<size_t>(ch)].reset();
    }

    // Calculate initial feedback filter coefficient (lowpass for damping)
    float dampNorm = delayDamping.load() / 100.0f;
    float cutoffHz = 12000.0f * std::pow(1000.0f / 12000.0f, dampNorm);
    feedbackFilterCoeff = std::exp(-2.0f * static_cast<float>(M_PI) * cutoffHz / static_cast<float>(currentSampleRate));

    // Calculate WARM filter coefficients (2-pole Butterworth lowpass at ~4.5kHz)
    // This emulates vintage hardware bandwidth limiting for a noticeably warmer sound
    {
        const float warmCutoff = 4500.0f;  // ~4.5kHz rolloff for audible warmth
        const float Q = 0.707f;  // Butterworth Q
        const float omega = 2.0f * static_cast<float>(M_PI) * warmCutoff / static_cast<float>(currentSampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * Q);

        const float a0 = 1.0f + alpha;
        warmFilterCoeffs[0] = ((1.0f - cosOmega) / 2.0f) / a0;  // b0
        warmFilterCoeffs[1] = (1.0f - cosOmega) / a0;            // b1
        warmFilterCoeffs[2] = ((1.0f - cosOmega) / 2.0f) / a0;  // b2
        warmFilterCoeffs[3] = -(-2.0f * cosOmega) / a0;          // -a1 (negated for direct form)
        warmFilterCoeffs[4] = -(1.0f - alpha) / a0;              // -a2 (negated for direct form)

        // Reset WARM filter state
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        {
            warmFilterState[static_cast<size_t>(ch)].fill(0.0f);
        }
    }

    // Calculate Classic mode output anti-aliasing LPF (~16kHz, 2nd order Butterworth)
    // Removes Nyquist-aliased content from large upward frequency shifts
    {
        const float aaCutoff = 16000.0f;
        const float Q = 0.707f;  // Butterworth Q
        const float omega = 2.0f * static_cast<float>(M_PI) * aaCutoff / static_cast<float>(currentSampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * Q);

        const float a0 = 1.0f + alpha;
        classicOutputLpfCoeffs[0] = ((1.0f - cosOmega) / 2.0f) / a0;  // b0
        classicOutputLpfCoeffs[1] = (1.0f - cosOmega) / a0;            // b1
        classicOutputLpfCoeffs[2] = ((1.0f - cosOmega) / 2.0f) / a0;  // b2
        classicOutputLpfCoeffs[3] = -(-2.0f * cosOmega) / a0;          // -a1
        classicOutputLpfCoeffs[4] = -(1.0f - alpha) / a0;              // -a2

        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
            classicOutputLpfState[static_cast<size_t>(ch)].fill(0.0f);
    }

    // Calculate highpass filter coefficients (150Hz, Q=0.707, Butterworth)
    // This prevents low frequency "thumping" buildup in the feedback loop
    {
        const float hpfCutoff = 150.0f;  // Raised from 80Hz for Orville-style behavior
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

    // Calculate 4-pole lowpass filter coefficients (~4kHz, 24dB/oct Linkwitz-Riley)
    // This aggressive filtering is essential for clean Hilbert feedback cascading
    // Lower cutoff (4kHz vs 7kHz) prevents aliased harmonics from building up
    // Two cascaded biquads give 24dB/oct slope for effective alias suppression
    {
        const float lpfCutoff = 4000.0f;  // 4kHz - darker but cleaner feedback
        const float Q = 0.707f;  // Butterworth Q for each stage (cascaded = Linkwitz-Riley)
        const float omega = 2.0f * static_cast<float>(M_PI) * lpfCutoff / static_cast<float>(currentSampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * Q);

        const float a0 = 1.0f + alpha;
        feedbackLpfCoeffs[0] = ((1.0f - cosOmega) / 2.0f) / a0;  // b0
        feedbackLpfCoeffs[1] = (1.0f - cosOmega) / a0;            // b1
        feedbackLpfCoeffs[2] = ((1.0f - cosOmega) / 2.0f) / a0;  // b2
        feedbackLpfCoeffs[3] = -(-2.0f * cosOmega) / a0;          // -a1 (negated for direct form)
        feedbackLpfCoeffs[4] = -(1.0f - alpha) / a0;              // -a2 (negated for direct form)

        // Reset both LPF stages
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        {
            feedbackLpf1State[static_cast<size_t>(ch)].fill(0.0f);
            feedbackLpf2State[static_cast<size_t>(ch)].fill(0.0f);
        }
    }

    // Reset cross-coupled feedback and drift LFO
    crossFeedbackSample.fill(0.0f);
    driftLfoPhase = 0.0;

    // Calculate Eventide-style 4th order Butterworth LPF coefficients for feedback filtering
    // 4th order = 2 cascaded 2nd order sections for 48 dB/oct slope
    // This provides steep anti-aliasing to clean up sideband leakage before feedback
    {
        const float fbLpfCutoff = 12000.0f;  // 12kHz cutoff for feedback path
        const float omega = 2.0f * static_cast<float>(M_PI) * fbLpfCutoff / static_cast<float>(currentSampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);

        // 4th order Butterworth has two stages with specific Q values
        // Q1 = 0.541 (first stage), Q2 = 1.307 (second stage)
        const float Q1 = 0.5412f;
        const float Q2 = 1.3065f;

        // First biquad section (Q1)
        float alpha1 = sinOmega / (2.0f * Q1);
        float a0_1 = 1.0f + alpha1;
        classicFbLpfCoeffs[0] = ((1.0f - cosOmega) / 2.0f) / a0_1;  // b0
        classicFbLpfCoeffs[1] = (1.0f - cosOmega) / a0_1;            // b1
        classicFbLpfCoeffs[2] = ((1.0f - cosOmega) / 2.0f) / a0_1;  // b2
        classicFbLpfCoeffs[3] = -(-2.0f * cosOmega) / a0_1;          // -a1
        classicFbLpfCoeffs[4] = -(1.0f - alpha1) / a0_1;             // -a2

        // Second biquad section (Q2)
        float alpha2 = sinOmega / (2.0f * Q2);
        float a0_2 = 1.0f + alpha2;
        classicFbLpfCoeffs[5] = ((1.0f - cosOmega) / 2.0f) / a0_2;  // b0
        classicFbLpfCoeffs[6] = (1.0f - cosOmega) / a0_2;            // b1
        classicFbLpfCoeffs[7] = ((1.0f - cosOmega) / 2.0f) / a0_2;  // b2
        classicFbLpfCoeffs[8] = -(-2.0f * cosOmega) / a0_2;          // -a1
        classicFbLpfCoeffs[9] = -(1.0f - alpha2) / a0_2;             // -a2

        // Reset classic mode feedback filter states
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        {
            classicDcBlockState[static_cast<size_t>(ch)] = 0.0f;
            classicFbHpfState[static_cast<size_t>(ch)].fill(0.0f);
            classicFbLpfState[static_cast<size_t>(ch)].fill(0.0f);
            classicFbDampState[static_cast<size_t>(ch)] = 0.0f;
            classicOutputLpfState[static_cast<size_t>(ch)].fill(0.0f);
        }
    }

    // Initialize stereo decorrelation buffer (0.06ms delay for left channel)
    // This reduces phase-locked resonance between L/R channels
    decorrelateDelaySamples = static_cast<int>(0.00006f * currentSampleRate + 0.5f);
    leftDecorrelateBuffer.resize(static_cast<size_t>(decorrelateDelaySamples + 4), 0.0f);
    decorrelateWritePos = 0;

    // Report latency based on current mode
    int currentMode = processingMode.load();
    requestLatency(currentMode == 0 ? CLASSIC_MODE_LATENCY : MAX_FFT_SIZE);
    needsReinit.store(false);
}

void FrequencyShifterProcessor::releaseResources()
{
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
    {
        for (int proc = 0; proc < NUM_PROCESSORS; ++proc)
        {
            stftProcessors[ch][proc].reset();
            inputBuffers[ch][proc].clear();
            outputBuffers[ch][proc].clear();
        }
        delayCompBuffers[ch].clear();
        dryDelayBuffers[ch].clear();
        feedbackSourceBuffers[ch].clear();
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
    // Apply stored atomic values here in audio thread for thread safety
    if (maskNeedsUpdate.load())
    {
        spectralMask.setMode(static_cast<fshift::SpectralMask::Mode>(maskMode.load()));
        spectralMask.setLowFreq(maskLowFreq.load());
        spectralMask.setHighFreq(maskHighFreq.load());
        spectralMask.setTransition(maskTransition.load());
        spectralMask.computeMaskCurve(currentSampleRate, currentFftSizes[0]);
        maskNeedsUpdate.store(false);
    }

    // Apply deferred spectral delay updates in audio thread for thread safety
    if (delayNeedsUpdate.load())
    {
        float currentDelayTime = delayTime.load();
        float currentDelaySlope = delaySlope.load();
        float currentDelayDamping = delayDamping.load();
        float currentDelayGainDb = delayGain.load();

        for (auto& chDelays : spectralDelays)
        {
            for (auto& delay : chDelays)
            {
                delay.setDelayTime(currentDelayTime);
                delay.setFrequencySlope(currentDelaySlope);
                delay.setFeedback(0.0f);  // R2: per-bin feedback off; time-domain loop is the sole feedback path
                delay.setDamping(currentDelayDamping);
                delay.setGain(currentDelayGainDb);
            }
        }

        // Update feedback filter coefficient for time-domain damping
        // 0% damping = 12kHz cutoff, 100% damping = 1kHz cutoff (logarithmic)
        float dampNorm = currentDelayDamping / 100.0f;
        float cutoffHz = 12000.0f * std::pow(1000.0f / 12000.0f, dampNorm);
        feedbackFilterCoeff = std::exp(-2.0f * static_cast<float>(M_PI) * cutoffHz / static_cast<float>(currentSampleRate));

        delayNeedsUpdate.store(false);
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Get current parameter values. Discontinuous-but-audible params are routed
    // through block-rate one-pole smoothers (see prepareToPlay) so slider slams
    // and host automation steps don't click.
    auto smoothBlock = [numSamples](float& state, float target, float perSampleCoeff) {
        float blockCoeff = std::pow(perSampleCoeff, static_cast<float>(numSamples));
        state = target + blockCoeff * (state - target);
        return state;
    };

    const float baseShiftHz = smoothBlock(smoothedShiftHz, shiftHz.load(), shiftHzSmoothCoeff);
    const float rawQuantizeStrength = smoothBlock(
        smoothedQuantizeStrength, quantizeStrength.load(), quantizeStrengthSmoothCoeff);
    // R1: remap the slider's 0->100 travel onto internal 0.2->1.0 (slider 0 == old 0.2).
    // The slider reads its position, not a literal %, and quant never drops below 0.2.
    const float currentQuantizeStrength = 0.2f + 0.8f * rawQuantizeStrength;
    const float currentDryWet = dryWetMix.load();
    // "Tones Only" (peak-snap) envelope-imposition controls: Texture = depth (how strongly the
    // source's amplitude envelope is imposed on the shifted wet); Density = max attack boost.
    const bool  currentPeakSnap = peakSnapEnabled.load();
    const float currentTexture  = noiseMix.load();          // 0..1, imposition depth
    const float currentDensity  = peakSensitivity.load();   // 0..1, attack-boost ceiling
    const bool currentMaskEnabled = maskEnabled.load();
    const bool currentWarmEnabled = warmEnabled.load();

    // LFO modulation parameters (smooth depth to prevent clicks on slider jumps).
    const float currentLfoDepth = smoothBlock(
        smoothedLfoDepth, lfoDepth.load(), lfoDepthSmoothCoeff);
    const int currentLfoDepthMode = lfoDepthMode.load();
    const float currentLfoRate = smoothBlock(
        smoothedLfoRate, lfoRate.load(), lfoRateSmoothCoeff);
    const bool currentLfoSync = lfoSync.load();
    const int currentLfoDivision = lfoDivision.load();
    const int currentLfoShape = lfoShape.load();
    const bool currentLfoEnabled = lfoEnabled.load();
    const bool currentDelayEnabled = delayEnabled.load();
    const bool currentDelaySync = delaySync.load();
    const int currentDelayDivision = delayDivision.load();
    const float currentFeedbackAmount = delayFeedback.load() / 100.0f;  // 0-0.95

    // Read host tempo for tempo sync
    double currentBpm = 120.0;  // Fallback
    bool   isPlaying = false;
    double ppqPosition = 0.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto tempo = position->getBpm())
            {
                currentBpm = *tempo;
            }
            isPlaying = position->getIsPlaying();
            if (auto ppq = position->getPpqPosition())
            {
                ppqPosition = *ppq;
            }
        }
    }
    hostBpm.store(currentBpm);

    // Transport-aware reset: when playback starts (stopped -> playing), retrigger the
    // free-running LFOs to phase 0 so they begin at the top each time you hit play.
    // Synced LFOs are additionally bar-locked to the host position below, so they line
    // up to the bar too. Degrades gracefully with no transport (e.g. standalone):
    // isPlaying stays false and the LFOs free-run exactly as before.
    if (isPlaying && !wasPlaying)
    {
        lfoPhase = 0.0;
        dlyLfoPhase = 0.0;
    }
    wasPlaying = isPlaying;

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

    // === LFO Calculation ===
    float lfoModulationHz = 0.0f;
    if (currentLfoEnabled && currentLfoDepth > 0.01f)
    {
        // Calculate LFO frequency
        double lfoFreqHz;
        if (currentLfoSync && currentLfoDivision >= 0 && currentLfoDivision < NUM_LFO_DIVISIONS)
        {
            // Tempo synced: calculate from BPM and division
            double beatsPerCycle = LFO_DIVISION_BEATS[currentLfoDivision];
            double secondsPerBeat = 60.0 / currentBpm;
            double secondsPerCycle = beatsPerCycle * secondsPerBeat;
            lfoFreqHz = 1.0 / secondsPerCycle;

            // PPQ phase-lock: while the transport is playing, anchor the synced LFO phase
            // to the host's musical position so it is bar-aligned and deterministic (the
            // same section always modulates identically; phase is 0 at the bar line). While
            // stopped it free-runs from the accumulator so it still moves for sound design.
            if (isPlaying)
            {
                double cyclePhase = ppqPosition / beatsPerCycle;
                lfoPhase = cyclePhase - std::floor(cyclePhase);
            }
        }
        else
        {
            // Free running: use RATE parameter directly
            lfoFreqHz = static_cast<double>(currentLfoRate);
        }

        // Advance LFO phase for this block
        double phaseIncrement = (lfoFreqHz * numSamples) / currentSampleRate;

        // Latency compensation for tempo sync
        // Advance phase by FFT latency to stay in sync with audible output
        double latencyCompensationPhase = 0.0;
        if (currentLfoSync)
        {
            int fftLatencySamples = currentFftSizes[0];
            latencyCompensationPhase = (static_cast<double>(fftLatencySamples) / currentSampleRate) * lfoFreqHz;
        }

        // Calculate current phase with latency compensation
        double currentPhase = lfoPhase + latencyCompensationPhase;
        currentPhase = currentPhase - std::floor(currentPhase);  // Wrap to 0-1

        // Generate LFO value based on shape (bipolar: -1 to +1)
        float lfoValue = 0.0f;
        switch (currentLfoShape)
        {
            case 0:  // Sine
                lfoValue = std::sin(currentPhase * 2.0 * juce::MathConstants<double>::pi);
                break;
            case 1:  // Triangle
                lfoValue = static_cast<float>(4.0 * std::abs(currentPhase - 0.5) - 1.0);
                break;
            case 2:  // Saw (rising)
                lfoValue = static_cast<float>(2.0 * currentPhase - 1.0);
                break;
            case 3:  // Inv Saw (falling)
                lfoValue = static_cast<float>(1.0 - 2.0 * currentPhase);
                break;
            case 4:  // Random (Sample & Hold)
                // Only update random value when phase wraps
                if (lfoPhase + phaseIncrement >= 1.0)
                {
                    lastRandomValue = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
                }
                lfoValue = lastRandomValue;
                break;
            case 5:  // Square
                lfoValue = (currentPhase < 0.5) ? 1.0f : -1.0f;
                break;
            default:
                lfoValue = 0.0f;
        }

        // Apply depth based on mode
        if (currentLfoDepthMode == 0)
        {
            // Hz mode: depth is in Hz
            lfoModulationHz = lfoValue * currentLfoDepth;
        }
        else
        {
            // Degrees mode: convert to Hz based on quantizer intervals
            // 1 degree = 1 scale step, approximate as semitone ratio
            // For now, use 100 cents per degree as approximation
            if (currentQuantizeStrength > 0.01f && quantizers[0])
            {
                // Use quantizer to get Hz per degree
                float baseFreq = 440.0f;  // Reference frequency
                float centsPerDegree = 100.0f;  // 1 semitone per degree
                float targetCents = lfoValue * currentLfoDepth * centsPerDegree;
                float ratio = std::pow(2.0f, targetCents / 1200.0f);
                lfoModulationHz = baseFreq * (ratio - 1.0f);
            }
            else
            {
                // Fall back to Hz mode when quantization is off
                lfoModulationHz = lfoValue * currentLfoDepth;
            }
        }

        // Advance phase for next block
        lfoPhase += phaseIncrement;
        lfoPhase = lfoPhase - std::floor(lfoPhase);  // Wrap to 0-1
    }

    // Final modulated shift value. Guard against a non-finite LFO modulation (Degrees-mode
    // pow(2, cents/1200) overflows to Inf at extreme LFO depth) poisoning the shifters.
    float currentShiftHz = baseShiftHz + lfoModulationHz;
    if (! std::isfinite(currentShiftHz))
        currentShiftHz = baseShiftHz;

    // === Delay Time LFO Calculation ===
    // Independent LFO that modulates delay time for dub/tape wobble effects
    float dlyLfoModulationMs = 0.0f;
    const float currentDlyLfoDepth = dlyLfoDepth.load();
    const float currentDlyLfoRate = dlyLfoRate.load();
    const bool currentDlyLfoSync = dlyLfoSync.load();
    const int currentDlyLfoDivision = dlyLfoDivision.load();
    const int currentDlyLfoShape = dlyLfoShape.load();
    const bool currentDlyLfoEnabled = dlyLfoEnabled.load();

    if (currentDlyLfoEnabled && currentDlyLfoDepth > 0.01f)
    {
        // Calculate LFO frequency
        double dlyLfoFreqHz;
        if (currentDlyLfoSync && currentDlyLfoDivision >= 0 && currentDlyLfoDivision < NUM_LFO_DIVISIONS)
        {
            // Tempo synced: calculate from BPM and division
            double beatsPerCycle = LFO_DIVISION_BEATS[currentDlyLfoDivision];
            double secondsPerBeat = 60.0 / currentBpm;
            double secondsPerCycle = beatsPerCycle * secondsPerBeat;
            dlyLfoFreqHz = 1.0 / secondsPerCycle;

            // PPQ phase-lock while playing (bar-aligned, deterministic) — see main LFO.
            if (isPlaying)
            {
                double cyclePhase = ppqPosition / beatsPerCycle;
                dlyLfoPhase = cyclePhase - std::floor(cyclePhase);
            }
        }
        else
        {
            // Free running: use RATE parameter directly
            dlyLfoFreqHz = static_cast<double>(currentDlyLfoRate);
        }

        // Advance delay LFO phase for this block
        double dlyPhaseIncrement = (dlyLfoFreqHz * numSamples) / currentSampleRate;

        // Latency compensation for tempo sync
        double dlyLatencyCompensationPhase = 0.0;
        if (currentDlyLfoSync)
        {
            int fftLatencySamples = currentFftSizes[0];
            dlyLatencyCompensationPhase = (static_cast<double>(fftLatencySamples) / currentSampleRate) * dlyLfoFreqHz;
        }

        // Calculate current phase with latency compensation
        double currentDlyPhase = dlyLfoPhase + dlyLatencyCompensationPhase;
        currentDlyPhase = currentDlyPhase - std::floor(currentDlyPhase);  // Wrap to 0-1

        // Generate LFO value based on shape (bipolar: -1 to +1)
        float dlyLfoValue = 0.0f;
        switch (currentDlyLfoShape)
        {
            case 0:  // Sine
                dlyLfoValue = std::sin(currentDlyPhase * 2.0 * juce::MathConstants<double>::pi);
                break;
            case 1:  // Triangle
                dlyLfoValue = static_cast<float>(4.0 * std::abs(currentDlyPhase - 0.5) - 1.0);
                break;
            case 2:  // Saw (rising)
                dlyLfoValue = static_cast<float>(2.0 * currentDlyPhase - 1.0);
                break;
            case 3:  // Inv Saw (falling)
                dlyLfoValue = static_cast<float>(1.0 - 2.0 * currentDlyPhase);
                break;
            case 4:  // Random (Sample & Hold)
                // Only update random value when phase wraps
                if (dlyLfoPhase + dlyPhaseIncrement >= 1.0)
                {
                    dlyLastRandomValue = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
                }
                dlyLfoValue = dlyLastRandomValue;
                break;
            case 5:  // Square
                dlyLfoValue = (currentDlyPhase < 0.5) ? 1.0f : -1.0f;
                break;
            default:
                dlyLfoValue = 0.0f;
        }

        // Apply depth (in ms)
        dlyLfoModulationMs = dlyLfoValue * currentDlyLfoDepth;

        // Advance phase for next block
        dlyLfoPhase += dlyPhaseIncrement;
        dlyLfoPhase = dlyLfoPhase - std::floor(dlyLfoPhase);  // Wrap to 0-1
    }

    // Apply delay time modulation and clamp to valid range (10ms - 2000ms)
    const float modulatedDelayTimeMs = std::clamp(currentDelayTimeMs + dlyLfoModulationMs, 10.0f, 2000.0f);

    // Cache crossfade value
    const float crossfade = currentCrossfade;
    const bool singleProc = useSingleProcessor;

    // If no processing needed, still apply delay compensation for timing
    const bool bypassProcessing = (std::abs(baseShiftHz) < 0.01f && currentLfoDepth < 0.01f && currentQuantizeStrength < 0.01f);

    // === Mode Switching Logic ===
    const int currentMode = processingMode.load();
    const bool switching = needsModeSwitch.load();

    // Calculate mode crossfade rate (samples to complete transition)
    const float modeCrossfadeRate = 1.0f / (MODE_CROSSFADE_MS * 0.001f * static_cast<float>(currentSampleRate));

    // If mode switch completed, update latency and finalize
    if (switching && modeCrossfadeProgress >= 1.0f)
    {
        processingMode.store(targetMode);
        needsModeSwitch.store(false);
        modeCrossfadeProgress = 1.0f;
        requestLatency(targetMode == 0 ? CLASSIC_MODE_LATENCY : MAX_FFT_SIZE);
    }

    // Determine active mode (use target mode once crossfade is complete)
    const bool useClassicMode = switching ? (targetMode == 0) : (currentMode == 0);
    const bool useSpectralMode = switching ? (previousMode == 1 || targetMode == 1) : (currentMode == 1);

    // Process each channel
    for (int channel = 0; channel < std::min(numChannels, MAX_CHANNELS); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        // Store dry signal for mixing
        std::vector<float> drySignal(channelData, channelData + numSamples);

        // Temp buffers for outputs
        std::vector<float> classicOutput(static_cast<size_t>(numSamples), 0.0f);
        std::vector<float> proc0Output(static_cast<size_t>(numSamples), 0.0f);
        std::vector<float> proc1Output(static_cast<size_t>(numSamples), 0.0f);

        // === CLASSIC MODE PROCESSING ===
        // Eventide-style Hilbert frequency shifter with precision-filtered feedback
        // Uses IIR allpass Hilbert transform with DC blocking + 4th order LPF for clean cascading
        if (useClassicMode || switching)
        {
            auto& hilbert = hilbertShifters[static_cast<size_t>(channel)];
            hilbert.setShiftHz(currentShiftHz);

            for (int i = 0; i < numSamples; ++i)
            {
                float inputSample = drySignal[static_cast<size_t>(i)];

                // Add feedback from delay buffer for cascading/cumulative pitch shifts (barber-pole effect)
                if (currentDelayEnabled && currentFeedbackAmount > 0.01f)
                {
                    auto& fbBuffer = feedbackBuffers[static_cast<size_t>(channel)];
                    int fbBufSize = static_cast<int>(fbBuffer.size());

                    // Per-sample delay time smoothing (one-pole filter for glitch-free modulation)
                    float targetDelaySamples = modulatedDelayTimeMs * static_cast<float>(currentSampleRate) / 1000.0f;
                    smoothedDelayTimeSamples[static_cast<size_t>(channel)] =
                        targetDelaySamples + delayTimeSmoothCoeff *
                        (smoothedDelayTimeSamples[static_cast<size_t>(channel)] - targetDelaySamples);

                    // Classic mode: NO FFT latency compensation - use smoothed delay time
                    float delaySamplesFloat = smoothedDelayTimeSamples[static_cast<size_t>(channel)];
                    float minDelaySamplesF = 10.0f * static_cast<float>(currentSampleRate) / 1000.0f;
                    delaySamplesFloat = std::clamp(delaySamplesFloat, minDelaySamplesF, static_cast<float>(fbBufSize - 2));

                    // Fractional delay read with linear interpolation for smooth modulation
                    float readPosFloat = static_cast<float>(feedbackWritePos[static_cast<size_t>(channel)]) - delaySamplesFloat;
                    if (readPosFloat < 0.0f)
                        readPosFloat += static_cast<float>(fbBufSize);

                    int readPos0 = static_cast<int>(readPosFloat) % fbBufSize;
                    int readPos1 = (readPos0 + 1) % fbBufSize;
                    float frac = readPosFloat - std::floor(readPosFloat);

                    float interpolatedSample = fbBuffer[static_cast<size_t>(readPos0)] * (1.0f - frac)
                                             + fbBuffer[static_cast<size_t>(readPos1)] * frac;
                    float feedbackSample = interpolatedSample * currentFeedbackAmount;

                    // Soft clip feedback on read for safety
                    if (std::abs(feedbackSample) > 0.95f)
                    {
                        feedbackSample = std::tanh(feedbackSample);
                    }

                    inputSample += feedbackSample;
                }

                // Apply Hilbert transform frequency shift (feedback goes through for cumulative shifts)
                float shiftedSample = hilbert.process(inputSample, channel);

                // === FEEDBACK FILTERING (matching spectral mode approach) ===
                // Signal chain: HPF (150Hz) → LPF (2nd order) → Write
                if (currentDelayEnabled && !switching)
                {
                    auto& fbBuffer = feedbackBuffers[static_cast<size_t>(channel)];
                    int fbBufSize = static_cast<int>(fbBuffer.size());

                    float toBuffer = shiftedSample;

                    // 1. Highpass filter (150Hz) to prevent low-frequency buildup
                    // Uses same coefficients as spectral mode HPF for consistency
                    auto& hpfState = classicFbHpfState[static_cast<size_t>(channel)];
                    {
                        float x0 = toBuffer;
                        float x1 = hpfState[0];
                        float x2 = hpfState[1];
                        float y1 = hpfState[2];
                        float y2 = hpfState[3];

                        float hpfOutput = feedbackHpfCoeffs[0] * x0
                                        + feedbackHpfCoeffs[1] * x1
                                        + feedbackHpfCoeffs[2] * x2
                                        + feedbackHpfCoeffs[3] * y1
                                        + feedbackHpfCoeffs[4] * y2;

                        hpfState[0] = x0;
                        hpfState[1] = x1;
                        hpfState[2] = hpfOutput;
                        hpfState[3] = y1;

                        toBuffer = hpfOutput;
                    }

                    // 2. Anti-aliasing LPF (2nd order Butterworth at 12kHz)
                    // Single biquad stage (Q=0.54) — gentler than previous 4th order,
                    // avoids resonance peak and ringing during delay modulation
                    auto& lpfState = classicFbLpfState[static_cast<size_t>(channel)];
                    {
                        float x0 = toBuffer;
                        float x1 = lpfState[0];
                        float x2 = lpfState[1];
                        float y1 = lpfState[2];
                        float y2 = lpfState[3];

                        float filtered = classicFbLpfCoeffs[0] * x0
                                       + classicFbLpfCoeffs[1] * x1
                                       + classicFbLpfCoeffs[2] * x2
                                       + classicFbLpfCoeffs[3] * y1
                                       + classicFbLpfCoeffs[4] * y2;

                        lpfState[0] = x0;
                        lpfState[1] = x1;
                        lpfState[2] = filtered;
                        lpfState[3] = y1;

                        toBuffer = filtered;
                    }

                    // 3. R5: Damping — one-pole LPF whose cutoff tracks the Damping knob
                    // (feedbackFilterCoeff: 0% -> ~12kHz, 100% -> ~1kHz). Gives the Classic
                    // feedback the same tonal control the Spectral path already has.
                    {
                        float& dampState = classicFbDampState[static_cast<size_t>(channel)];
                        dampState = toBuffer + feedbackFilterCoeff * (dampState - toBuffer);
                        toBuffer = dampState;
                    }

                    // Write to feedback buffer (no write-side soft clip — read-side clip is sufficient)
                    fbBuffer[static_cast<size_t>(feedbackWritePos[static_cast<size_t>(channel)])] = toBuffer;
                    feedbackWritePos[static_cast<size_t>(channel)] = (feedbackWritePos[static_cast<size_t>(channel)] + 1) % fbBufSize;
                }

                // Anti-aliasing LPF on output (16kHz, removes Nyquist aliasing from large shifts)
                {
                    auto& aaState = classicOutputLpfState[static_cast<size_t>(channel)];
                    float x0 = shiftedSample;
                    float x1 = aaState[0];
                    float x2 = aaState[1];
                    float y1 = aaState[2];
                    float y2 = aaState[3];

                    float aaOutput = classicOutputLpfCoeffs[0] * x0
                                   + classicOutputLpfCoeffs[1] * x1
                                   + classicOutputLpfCoeffs[2] * x2
                                   + classicOutputLpfCoeffs[3] * y1
                                   + classicOutputLpfCoeffs[4] * y2;

                    aaState[0] = x0;
                    aaState[1] = x1;
                    aaState[2] = aaOutput;
                    aaState[3] = y1;

                    shiftedSample = aaOutput;
                }

                // Output is the shifted signal (feedback creates cascading barber-pole effect)
                classicOutput[static_cast<size_t>(i)] = shiftedSample;
            }
        }

        // === SPECTRAL MODE PROCESSING ===
        // Process through both STFT pipelines (or just one if singleProc)
        if (useSpectralMode || switching)
        {
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

                // Feedback is recirculated only on proc 0 when Delay is enabled. Gates every
                // change below, so the no-delay path is byte-for-byte identical to before.
                const bool feedbackActive = (currentDelayEnabled && proc == 0);

                // Add feedback from time-domain buffer (only once per sample, on proc 0)
                // This routes feedback BEFORE the shifter for cascading pitch shifts
                if (currentDelayEnabled && proc == 0)
                {
                    auto& fbBuffer = feedbackBuffers[static_cast<size_t>(channel)];
                    int fbBufSize = static_cast<int>(fbBuffer.size());

                    // Per-sample delay time smoothing (one-pole filter for glitch-free modulation)
                    float targetDelaySamples = modulatedDelayTimeMs * static_cast<float>(currentSampleRate) / 1000.0f;
                    smoothedDelayTimeSamples[static_cast<size_t>(channel)] =
                        targetDelaySamples + delayTimeSmoothCoeff *
                        (smoothedDelayTimeSamples[static_cast<size_t>(channel)] - targetDelaySamples);

                    // Compensate for SMEAR-dependent FFT latency
                    // The feedback path goes through FFT processing, which adds latency
                    // that varies with SMEAR setting. We subtract this to keep delay
                    // timing consistent regardless of SMEAR.
                    int currentFftLatencySamples = currentFftSizes[0];
                    float delaySamplesFloat = smoothedDelayTimeSamples[static_cast<size_t>(channel)]
                                            - static_cast<float>(currentFftLatencySamples);

                    float minDelaySamplesF = 10.0f * static_cast<float>(currentSampleRate) / 1000.0f;
                    delaySamplesFloat = std::clamp(delaySamplesFloat, minDelaySamplesF, static_cast<float>(fbBufSize - 2));

                    // Fractional delay read with linear interpolation for smooth modulation
                    float readPosFloat = static_cast<float>(feedbackWritePos[static_cast<size_t>(channel)]) - delaySamplesFloat;
                    if (readPosFloat < 0.0f)
                        readPosFloat += static_cast<float>(fbBufSize);

                    int readPos0 = static_cast<int>(readPosFloat) % fbBufSize;
                    int readPos1 = (readPos0 + 1) % fbBufSize;
                    float frac = readPosFloat - std::floor(readPosFloat);

                    float delayedSample = fbBuffer[static_cast<size_t>(readPos0)] * (1.0f - frac)
                                        + fbBuffer[static_cast<size_t>(readPos1)] * frac;
                    float feedbackSample = delayedSample * currentFeedbackAmount;

                    // Soft clip feedback for safety (tanh-style)
                    if (std::abs(feedbackSample) > 0.95f)
                    {
                        feedbackSample = std::tanh(feedbackSample);
                    }

                    inputSample += feedbackSample;

                    // DEBUG: Log feedback activity (once per second per channel)
                    static int debugCounter = 0;
                    if (channel == 0 && ++debugCounter % static_cast<int>(currentSampleRate) == 0)
                    {
                        DBG("=== Delay Feedback Debug ===");
                        DBG("Delayed sample: " + juce::String(delayedSample, 6));
                        DBG("Input after feedback: " + juce::String(inputSample, 6));
                        DBG("Requested delay: " + juce::String(modulatedDelayTimeMs) + " ms (base: " + juce::String(currentDelayTimeMs) + " ms)");
                        DBG("FFT latency compensation: " + juce::String(currentFftLatencySamples) + " samples ("
                            + juce::String(currentFftLatencySamples * 1000.0f / currentSampleRate, 1) + " ms)");
                        DBG("Smoothed delay samples: " + juce::String(delaySamplesFloat, 1));
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

                    // Feedback-loop source spectrum (filled only when feedback is active):
                    // the shifted/quantized magnitude BEFORE spectral-envelope preservation,
                    // paired with the post-quantize pre-mask phase. Recirculated below in place
                    // of the envelope-boosted audible output.
                    std::vector<float> preEnvMagnitude;
                    std::vector<float> fbPhase;

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
                        if (currentPreserve > 0.01f && quantizers[channel] && currentQuantizeStrength > 0.01f)
                        {
                            inputEnvelope = quantizers[channel]->getSpectralEnvelope(magnitude, currentSampleRate, fftSize);
                            envelopePtr = &inputEnvelope;
                        }

                        // Spectral mode = "in-key frequency shifter": shift and scale-snap happen
                        // together inside the quantizer. shiftHz stays continuous (no bin rounding)
                        // and the two-nearest-scale-tone crossfade glides smoothly as the knob
                        // moves. Phase coherence comes from the quantizer's per-MIDI-note phase
                        // accumulators.
                        if (quantizers[channel] &&
                            (currentQuantizeStrength > 0.01f || std::abs(currentShiftHz) > 0.01f))
                        {
                            std::tie(magnitude, phase) = quantizers[channel]->quantizeSpectrum(
                                magnitude, phase, currentSampleRate, fftSize,
                                currentShiftHz, currentQuantizeStrength,
                                nullptr, envelopePtr,
                                feedbackActive ? &preEnvMagnitude : nullptr);

                            // Snapshot the post-quantize, pre-mask phase to pair with the
                            // pre-envelope magnitude for the feedback-loop IFFT below.
                            if (feedbackActive)
                                fbPhase = phase;
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
                            spectralDelays[channel][proc].setDelayTime(modulatedDelayTimeMs);
                            spectralDelays[channel][proc].process(magnitude, phase);
                        }
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

                    // Feedback-loop source: IFFT the pre-envelope-preservation spectrum into its
                    // own overlap-add buffer (same size/positions as outputBuf). When the quantizer
                    // didn't run this frame (transient bypass, or no shift/quantize), preEnvMagnitude
                    // is empty and the audible frame — which carries no envelope gain in that case —
                    // is reused, exactly preserving the original feedback behaviour.
                    if (feedbackActive)
                    {
                        auto& fbSrcBuf = feedbackSourceBuffers[static_cast<size_t>(channel)];
                        auto fbFrame = preEnvMagnitude.empty()
                            ? outputFrame
                            : stftProcessors[channel][proc]->inverse(preEnvMagnitude, fbPhase);
                        for (int j = 0; j < fftSize; ++j)
                        {
                            int pos = (writePos + j) % static_cast<int>(fbSrcBuf.size());
                            fbSrcBuf[static_cast<size_t>(pos)] += fbFrame[static_cast<size_t>(j)];
                        }
                    }
                }

                // Read from output buffer
                float outputSample = outputBuf[static_cast<size_t>(outReadPos)];
                outputBuf[static_cast<size_t>(outReadPos)] = 0.0f;  // Clear for next overlap-add

                // Read the matching feedback-loop source sample (same position) before advancing
                float feedbackSourceSample = 0.0f;
                if (feedbackActive)
                {
                    auto& fbSrcBuf = feedbackSourceBuffers[static_cast<size_t>(channel)];
                    feedbackSourceSample = fbSrcBuf[static_cast<size_t>(outReadPos)];
                    fbSrcBuf[static_cast<size_t>(outReadPos)] = 0.0f;  // Clear for next overlap-add
                }

                outReadPos = (outReadPos + 1) % static_cast<int>(outputBuf.size());

                // Write processed output to time-domain feedback buffer (only once, on proc 0)
                // This gets added to input on the next delay cycle, creating cascading pitch shifts
                if (currentDelayEnabled && proc == 0)
                {
                    auto& fbBuffer = feedbackBuffers[static_cast<size_t>(channel)];
                    int fbBufSize = static_cast<int>(fbBuffer.size());

                    // === Feedback signal chain: HPF (150Hz) → LPF (DAMP) → Write ===
                    //
                    // The recirculated signal is feedbackSourceSample — the shifted/quantized
                    // output BEFORE in-loop spectral-envelope preservation (applySpectralEnvelopeFast
                    // in the quantizer). Recirculating the envelope-boosted audible output instead
                    // made the loop regenerative at high shift + Envelope (the v0.1.7 runaway):
                    // each pass re-applied the per-band envelope make-up gain (up to +36 dB/band).
                    // Tapping pre-envelope keeps the loop gain governed only by the Feedback knob
                    // and the HPF/damping filters — the stable Envelope-off regime — while the
                    // audible path still gets full envelope preservation. (Replaces the v0.1.7
                    // 1/lastPreserveGain compensation, which used the post-loop amplitude gain —
                    // the wrong quantity — and could not cancel the per-band spectral loop gain.)

                    // Step 1: Apply highpass filter (150Hz) to prevent low frequency buildup
                    // Biquad Direct Form I: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
                    auto& hpfState = feedbackHpfState[static_cast<size_t>(channel)];
                    float x0 = feedbackSourceSample;
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

                    // Write to feedback buffer
                    fbBuffer[static_cast<size_t>(feedbackWritePos[static_cast<size_t>(channel)])] = lpfState;
                    feedbackWritePos[static_cast<size_t>(channel)] = (feedbackWritePos[static_cast<size_t>(channel)] + 1) % fbBufSize;

                    // DEBUG: Log output being written to feedback buffer (once per second)
                    static int fbWriteDebugCounter = 0;
                    if (channel == 0 && ++fbWriteDebugCounter % static_cast<int>(currentSampleRate) == 0)
                    {
                        DBG("--- Feedback Write ---");
                        DBG("Feedback source (pre-envelope): " + juce::String(feedbackSourceSample, 6));
                        DBG("After HPF: " + juce::String(hpfOutput, 6));
                        DBG("After LPF (written to buffer): " + juce::String(lpfState, 6));
                    }
                }

                procOutput[static_cast<size_t>(i)] = outputSample;
            }
        }
        } // End of Spectral mode processing

        // === MIXING AND OUTPUT ===
        // Handle both Classic and Spectral modes, with crossfade during mode switching

        // FFT size crossfade gains (for Spectral mode dual-processor blending)
        const float fftAngle = crossfade * static_cast<float>(M_PI) * 0.5f;
        const float fftGain0 = std::cos(fftAngle);
        const float fftGain1 = std::sin(fftAngle);

        for (int i = 0; i < numSamples; ++i)
        {
            float wetSample = 0.0f;
            float drySample = drySignal[static_cast<size_t>(i)];

            if (currentMode == 0 && !switching)
            {
                // === CLASSIC MODE (not switching) ===
                // Near-zero latency: no dry delay needed
                wetSample = classicOutput[static_cast<size_t>(i)];

                // Apply WARM filter (vintage bandwidth limiting) to wet signal
                if (currentWarmEnabled)
                {
                    auto& warmState = warmFilterState[static_cast<size_t>(channel)];
                    float wx0 = wetSample;
                    float wx1 = warmState[0];
                    float wx2 = warmState[1];
                    float wy1 = warmState[2];
                    float wy2 = warmState[3];

                    float warmOutput = warmFilterCoeffs[0] * wx0
                                     + warmFilterCoeffs[1] * wx1
                                     + warmFilterCoeffs[2] * wx2
                                     + warmFilterCoeffs[3] * wy1
                                     + warmFilterCoeffs[4] * wy2;

                    warmState[1] = wx1;
                    warmState[0] = wx0;
                    warmState[3] = wy1;
                    warmState[2] = warmOutput;

                    wetSample = warmOutput;
                }

                // Still write to dry delay buffer to keep it updated for potential mode switch
                auto& dryBuf = dryDelayBuffers[channel];
                int bufSize = static_cast<int>(dryBuf.size());
                dryBuf[static_cast<size_t>(dryDelayWritePos[channel])] = drySample;
                dryDelayWritePos[channel] = (dryDelayWritePos[channel] + 1) % bufSize;

                // Mix dry/wet (no delay on dry for Classic mode)
                channelData[i] = drySample * (1.0f - currentDryWet) + wetSample * currentDryWet;
            }
            else if (currentMode == 1 && !switching)
            {
                // === SPECTRAL MODE (not switching) ===
                // Full FFT latency: apply delay compensation and dry delay

                // Blend dual FFT processor outputs
                float spectralProcessed;
                if (singleProc)
                {
                    spectralProcessed = proc0Output[static_cast<size_t>(i)];
                }
                else
                {
                    spectralProcessed = proc0Output[static_cast<size_t>(i)] * fftGain0 +
                                        proc1Output[static_cast<size_t>(i)] * fftGain1;
                }

                // Apply delay compensation to maintain fixed latency
                int effectiveFftSize = singleProc ? currentFftSizes[0] :
                    static_cast<int>(static_cast<float>(currentFftSizes[0]) * fftGain0 * fftGain0 +
                                     static_cast<float>(currentFftSizes[1]) * fftGain1 * fftGain1);
                int delayNeeded = MAX_FFT_SIZE - effectiveFftSize;

                // Write to delay compensation buffer
                delayCompBuffers[channel][static_cast<size_t>(delayCompWritePos[channel])] = spectralProcessed;
                delayCompWritePos[channel] = (delayCompWritePos[channel] + 1) %
                    static_cast<int>(delayCompBuffers[channel].size());

                // Read from delay compensation buffer
                int readIdx = (delayCompWritePos[channel] - delayNeeded - 1 +
                    static_cast<int>(delayCompBuffers[channel].size())) %
                    static_cast<int>(delayCompBuffers[channel].size());
                wetSample = delayCompBuffers[channel][static_cast<size_t>(readIdx)];

                // Delay dry signal by MAX_FFT_SIZE to align with wet
                auto& dryBuf = dryDelayBuffers[channel];
                int bufSize = static_cast<int>(dryBuf.size());
                dryBuf[static_cast<size_t>(dryDelayWritePos[channel])] = drySample;
                int dryReadIdx = (dryDelayWritePos[channel] - MAX_FFT_SIZE + bufSize) % bufSize;
                float delayedDrySample = dryBuf[static_cast<size_t>(dryReadIdx)];
                dryDelayWritePos[channel] = (dryDelayWritePos[channel] + 1) % bufSize;

                // Phase 2B+ Amplitude envelope tracking (Spectral only)
                float currentPreserve = preserveAmount.load();
                if (currentPreserve > 0.01f && !bypassProcessing)
                {
                    float inputAbs = std::abs(delayedDrySample);
                    if (inputAbs > inputEnvelope[static_cast<size_t>(channel)])
                        inputEnvelope[static_cast<size_t>(channel)] =
                            inputAbs + envAttackCoeff * (inputEnvelope[static_cast<size_t>(channel)] - inputAbs);
                    else
                        inputEnvelope[static_cast<size_t>(channel)] =
                            inputAbs + envReleaseCoeff * (inputEnvelope[static_cast<size_t>(channel)] - inputAbs);

                    float outputAbs = std::abs(wetSample);
                    if (outputAbs > outputEnvelope[static_cast<size_t>(channel)])
                        outputEnvelope[static_cast<size_t>(channel)] =
                            outputAbs + envAttackCoeff * (outputEnvelope[static_cast<size_t>(channel)] - outputAbs);
                    else
                        outputEnvelope[static_cast<size_t>(channel)] =
                            outputAbs + envReleaseCoeff * (outputEnvelope[static_cast<size_t>(channel)] - outputAbs);

                    float effectiveStrength = std::pow(currentPreserve, 0.7f);
                    constexpr float epsilon = 1e-6f;
                    float gainCorrection = inputEnvelope[static_cast<size_t>(channel)] /
                                           (outputEnvelope[static_cast<size_t>(channel)] + epsilon);
                    gainCorrection = std::clamp(gainCorrection, 0.25f, 4.0f);
                    float blendedCorrection = 1.0f + effectiveStrength * (gainCorrection - 1.0f);
                    wetSample *= blendedCorrection;
                }

                // Apply WARM filter (vintage bandwidth limiting) to wet signal
                if (currentWarmEnabled)
                {
                    auto& warmState = warmFilterState[static_cast<size_t>(channel)];
                    float wx0 = wetSample;
                    float wx1 = warmState[0];
                    float wx2 = warmState[1];
                    float wy1 = warmState[2];
                    float wy2 = warmState[3];

                    float warmOutput = warmFilterCoeffs[0] * wx0
                                     + warmFilterCoeffs[1] * wx1
                                     + warmFilterCoeffs[2] * wx2
                                     + warmFilterCoeffs[3] * wy1
                                     + warmFilterCoeffs[4] * wy2;

                    warmState[1] = wx1;
                    warmState[0] = wx0;
                    warmState[3] = wy1;
                    warmState[2] = warmOutput;

                    wetSample = warmOutput;
                }

                // "Tones Only": impose the SOURCE's amplitude envelope on the fully-shifted wet.
                // The long STFT window smears the attack and the phase-lock rings out; here we
                // reshape the wet's LOUDNESS contour to follow the source — sharp onset, pre-echo
                // gated (src env ~0 before the hit), tail ducked when the source has decayed but
                // the wet still rings — WITHOUT mixing any dry signal into the output. The audio
                // stays 100% the effect; delayedDrySample is only a control reference.
                // Texture = depth of the effect; Density = how much attack boost is allowed.
                if (currentPeakSnap && currentTexture > 0.0001f)
                {
                    const float srcAbs = std::abs(delayedDrySample);
                    const float wetAbs = std::abs(wetSample);
                    float& se = srcEnv[static_cast<size_t>(channel)];
                    float& we = wetEnv[static_cast<size_t>(channel)];
                    se = srcAbs + ((srcAbs > se) ? punchEnvAtkCoeff : punchEnvRelCoeff) * (se - srcAbs);
                    we = wetAbs + ((wetAbs > we) ? punchEnvAtkCoeff : punchEnvRelCoeff) * (we - wetAbs);

                    const float maxGain = 2.0f + currentDensity * 10.0f;          // up to ~+21 dB
                    const float ratio   = juce::jlimit(0.1f, maxGain, se / (we + 1.0e-6f));
                    wetSample *= 1.0f + currentTexture * (ratio - 1.0f);          // Texture = depth
                }

                // Mix delayed dry with wet
                channelData[i] = delayedDrySample * (1.0f - currentDryWet) + wetSample * currentDryWet;
            }
            else
            {
                // === MODE SWITCHING - Crossfade between modes ===
                float classicWet = classicOutput[static_cast<size_t>(i)];

                // Process Spectral output with delay compensation
                float spectralProcessed;
                if (singleProc)
                {
                    spectralProcessed = proc0Output[static_cast<size_t>(i)];
                }
                else
                {
                    spectralProcessed = proc0Output[static_cast<size_t>(i)] * fftGain0 +
                                        proc1Output[static_cast<size_t>(i)] * fftGain1;
                }

                int effectiveFftSize = singleProc ? currentFftSizes[0] :
                    static_cast<int>(static_cast<float>(currentFftSizes[0]) * fftGain0 * fftGain0 +
                                     static_cast<float>(currentFftSizes[1]) * fftGain1 * fftGain1);
                int delayNeeded = MAX_FFT_SIZE - effectiveFftSize;

                delayCompBuffers[channel][static_cast<size_t>(delayCompWritePos[channel])] = spectralProcessed;
                delayCompWritePos[channel] = (delayCompWritePos[channel] + 1) %
                    static_cast<int>(delayCompBuffers[channel].size());

                int readIdx = (delayCompWritePos[channel] - delayNeeded - 1 +
                    static_cast<int>(delayCompBuffers[channel].size())) %
                    static_cast<int>(delayCompBuffers[channel].size());
                float spectralWet = delayCompBuffers[channel][static_cast<size_t>(readIdx)];

                // Handle dry signal delay buffer
                auto& dryBuf = dryDelayBuffers[channel];
                int bufSize = static_cast<int>(dryBuf.size());
                dryBuf[static_cast<size_t>(dryDelayWritePos[channel])] = drySample;
                int dryReadIdx = (dryDelayWritePos[channel] - MAX_FFT_SIZE + bufSize) % bufSize;
                float delayedDrySample = dryBuf[static_cast<size_t>(dryReadIdx)];
                dryDelayWritePos[channel] = (dryDelayWritePos[channel] + 1) % bufSize;

                // Mode crossfade (equal-power)
                float progress = modeCrossfadeProgress + modeCrossfadeRate * static_cast<float>(i);
                progress = std::min(1.0f, progress);
                float modeAngle = progress * static_cast<float>(M_PI) * 0.5f;
                float fromGain = std::cos(modeAngle);
                float toGain = std::sin(modeAngle);

                float finalWet;
                float finalDry;
                if (targetMode == 0)
                {
                    // Switching TO Classic (FROM Spectral)
                    finalWet = spectralWet * fromGain + classicWet * toGain;
                    // Crossfade dry signal: delayed dry (Spectral) -> immediate dry (Classic)
                    finalDry = delayedDrySample * fromGain + drySample * toGain;
                }
                else
                {
                    // Switching TO Spectral (FROM Classic)
                    finalWet = classicWet * fromGain + spectralWet * toGain;
                    // Crossfade dry signal: immediate dry (Classic) -> delayed dry (Spectral)
                    finalDry = drySample * fromGain + delayedDrySample * toGain;
                }

                // Apply WARM filter (vintage bandwidth limiting) to wet signal
                if (currentWarmEnabled)
                {
                    auto& warmState = warmFilterState[static_cast<size_t>(channel)];
                    float wx0 = finalWet;
                    float wx1 = warmState[0];
                    float wx2 = warmState[1];
                    float wy1 = warmState[2];
                    float wy2 = warmState[3];

                    float warmOutput = warmFilterCoeffs[0] * wx0
                                     + warmFilterCoeffs[1] * wx1
                                     + warmFilterCoeffs[2] * wx2
                                     + warmFilterCoeffs[3] * wy1
                                     + warmFilterCoeffs[4] * wy2;

                    warmState[1] = wx1;
                    warmState[0] = wx0;
                    warmState[3] = wy1;
                    warmState[2] = warmOutput;

                    finalWet = warmOutput;
                }

                channelData[i] = finalDry * (1.0f - currentDryWet) + finalWet * currentDryWet;
            }
        }

        // Update mode crossfade progress at end of block
        if (switching)
        {
            modeCrossfadeProgress += modeCrossfadeRate * static_cast<float>(numSamples);
        }
    }

    // Apply stereo decorrelation if enabled (0.06ms delay on left channel only)
    // This reduces phase-locked resonance artifacts between L/R channels
    if (stereoDecorrelateEnabled.load() && numChannels >= 2 && decorrelateDelaySamples > 0)
    {
        auto* leftChannel = buffer.getWritePointer(0);
        int bufSize = static_cast<int>(leftDecorrelateBuffer.size());

        for (int i = 0; i < numSamples; ++i)
        {
            // Read delayed sample from buffer
            int readPos = (decorrelateWritePos - decorrelateDelaySamples + bufSize) % bufSize;
            float delayedSample = leftDecorrelateBuffer[static_cast<size_t>(readPos)];

            // Write current sample to buffer
            leftDecorrelateBuffer[static_cast<size_t>(decorrelateWritePos)] = leftChannel[i];
            decorrelateWritePos = (decorrelateWritePos + 1) % bufSize;

            // Output delayed sample
            leftChannel[i] = delayedSample;
        }
    }

    // Advance drift LFO phase for next block (Orville-style organic movement)
    // This creates slow ~0.2Hz modulation on Classic mode shift frequency
    driftLfoPhase += (DRIFT_LFO_RATE / currentSampleRate) * static_cast<double>(numSamples);
    if (driftLfoPhase >= 1.0)
        driftLfoPhase -= 1.0;
}

int FrequencyShifterProcessor::getLatencySamples() const
{
    // Report latency based on processing mode
    // Classic mode: near-zero latency (~12 samples for allpass group delay)
    // Spectral mode: full FFT latency (4096 samples)
    return (processingMode.load() == 0) ? CLASSIC_MODE_LATENCY : MAX_FFT_SIZE;
}

double FrequencyShifterProcessor::getTailLengthSeconds() const
{
    // Latency from FFT processing (use max for consistency)
    return static_cast<double>(MAX_FFT_SIZE + MAX_FFT_SIZE / 4) / currentSampleRate;
}

juce::AudioProcessorEditor* FrequencyShifterProcessor::createEditor()
{
#if HOLY_SHIFTER_USE_WEBVIEW
    return new WebViewEditor(*this);
#else
    return new VisageHostEditor(*this);
#endif
}

void FrequencyShifterProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    PresetManager::stampVersion(state);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FrequencyShifterProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml(*xmlState);
        PresetManager::migrateState(tree, PresetManager::readVersion(*xmlState));
        parameters.replaceState(tree);
    }
}

// Plugin instantiation
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyShifterProcessor();
}
