#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
 * SpectrumAnalyzer - Real-time spectrum visualization component.
 *
 * Displays the frequency spectrum of the processed audio signal
 * using data from the FFT processing pipeline.
 */
class SpectrumAnalyzer : public juce::Component,
                          private juce::Timer
{
public:
    SpectrumAnalyzer(FrequencyShifterProcessor& processor);
    ~SpectrumAnalyzer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    FrequencyShifterProcessor& audioProcessor;

    // Spectrum data buffer
    std::array<float, SPECTRUM_SIZE> spectrumData{};

    // Smoothed display data
    std::array<float, SPECTRUM_SIZE> smoothedData{};

    // Smoothing factor (0-1, higher = slower decay)
    static constexpr float smoothingFactor = 0.8f;

    // Auto-scaling state
    float currentPeakDb = -60.0f;      // Current detected peak level in dB
    float displayCeilingDb = -10.0f;   // Top of display range (adapts to signal)
    static constexpr float floorDb = -100.0f;  // Bottom of display range (fixed)
    static constexpr float peakDecayRate = 0.995f;  // How fast peak detector decays
    static constexpr float ceilingAttackRate = 0.3f;  // How fast ceiling rises to meet peaks
    static constexpr float ceilingDecayRate = 0.998f;  // How slowly ceiling falls

    // Colors
    static constexpr juce::uint32 backgroundColor = 0xFF1A1A2E;
    static constexpr juce::uint32 gridColor = 0xFF2A2A3E;
    static constexpr juce::uint32 spectrumColor = 0xFF7AA2F7;
    static constexpr juce::uint32 spectrumFillColor = 0x407AA2F7;
    static constexpr juce::uint32 textColor = 0xFF6C7086;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

/**
 * FrequencyShifterEditor - GUI for the Frequency Shifter plugin.
 *
 * Provides a clean, modern interface with:
 * - Large frequency shift knob with Hz display
 * - Scale quantization controls
 * - Root note and scale type selection
 * - Dry/wet mix control
 * - Enhanced phase vocoder toggle
 *
 * UI Design Notes:
 * - The interface is designed for easy customization later
 * - Uses JUCE's LookAndFeel for consistent styling
 * - All controls are attached to AudioProcessorValueTreeState parameters
 */
class FrequencyShifterEditor : public juce::AudioProcessorEditor,
                               private juce::Slider::Listener
{
public:
    explicit FrequencyShifterEditor(FrequencyShifterProcessor& processor);
    ~FrequencyShifterEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Custom look and feel for modern appearance
    class ModernLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        ModernLookAndFeel();

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;

        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              juce::Slider::SliderStyle style, juce::Slider& slider) override;
    };

    // Helper to create a styled label
    void setupLabel(juce::Label& label, const juce::String& text);

    // Helper to create a styled slider
    void setupSlider(juce::Slider& slider, juce::Slider::SliderStyle style);

    // Reference to processor
    FrequencyShifterProcessor& audioProcessor;

    // Custom look and feel
    ModernLookAndFeel modernLookAndFeel;

    // Main frequency shift control
    juce::Slider shiftSlider;
    juce::Label shiftLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shiftAttachment;

    // Quantization control
    juce::Slider quantizeSlider;
    juce::Label quantizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> quantizeAttachment;

    // Phase 2B: Envelope preservation and transient controls
    juce::Slider preserveSlider;
    juce::Label preserveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preserveAttachment;

    juce::Slider transientsSlider;
    juce::Label transientsLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transientsAttachment;

    juce::Slider sensitivitySlider;
    juce::Label sensitivityLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttachment;

    // Root note selector
    juce::ComboBox rootNoteCombo;
    juce::Label rootNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootNoteAttachment;

    // Scale type selector
    juce::ComboBox scaleTypeCombo;
    juce::Label scaleTypeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleTypeAttachment;

    // Dry/wet mix
    juce::Slider dryWetSlider;
    juce::Label dryWetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;

    // Phase vocoder toggle
    juce::ToggleButton phaseVocoderButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> phaseVocoderAttachment;

    // SMEAR control (replaces quality mode dropdown)
    juce::Slider smearSlider;
    juce::Label smearLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearAttachment;

    // Drift controls (for organic/imperfect quantization)
    juce::Slider driftAmountSlider;
    juce::Label driftAmountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driftAmountAttachment;

    juce::Slider driftRateSlider;
    juce::Label driftRateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driftRateAttachment;

    juce::ComboBox driftModeCombo;
    juce::Label driftModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> driftModeAttachment;

    // Stochastic controls (only active when drift mode is Stochastic)
    juce::ComboBox stochasticTypeCombo;
    juce::Label stochasticTypeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stochasticTypeAttachment;

    juce::Slider stochasticDensitySlider;
    juce::Label stochasticDensityLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stochasticDensityAttachment;

    juce::Slider stochasticSmoothnessSlider;
    juce::Label stochasticSmoothnessLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stochasticSmoothnessAttachment;

    // Manual sync (SliderAttachment doesn't support custom ranges for log scale)
    void sliderValueChanged(juce::Slider* slider) override;

    // Spectrum analyzer
    std::unique_ptr<SpectrumAnalyzer> spectrumAnalyzer;
    juce::ToggleButton spectrumButton;
    bool spectrumVisible = false;

    // Spectral mask controls
    juce::ToggleButton maskEnabledButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> maskEnabledAttachment;

    juce::ComboBox maskModeCombo;
    juce::Label maskModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> maskModeAttachment;

    juce::Slider maskLowFreqSlider;
    juce::Label maskLowFreqLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maskLowFreqAttachment;

    juce::Slider maskHighFreqSlider;
    juce::Label maskHighFreqLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maskHighFreqAttachment;

    juce::Slider maskTransitionSlider;
    juce::Label maskTransitionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maskTransitionAttachment;

    // Spectral delay controls
    juce::ToggleButton delayEnabledButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> delayEnabledAttachment;

    juce::Slider delayTimeSlider;
    juce::Label delayTimeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayTimeAttachment;

    juce::Slider delaySlopeSlider;
    juce::Label delaySlopeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delaySlopeAttachment;

    juce::Slider delayFeedbackSlider;
    juce::Label delayFeedbackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayFeedbackAttachment;

    juce::Slider delayDampingSlider;
    juce::Label delayDampingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayDampingAttachment;

    juce::Slider delayMixSlider;
    juce::Label delayMixLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayMixAttachment;

    juce::Slider delayGainSlider;
    juce::Label delayGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayGainAttachment;

    // UI colors (for easy customization)
    struct Colors
    {
        static constexpr juce::uint32 background = 0xFF1E1E2E;
        static constexpr juce::uint32 panelBackground = 0xFF2A2A3E;
        static constexpr juce::uint32 accent = 0xFF7AA2F7;
        static constexpr juce::uint32 accentSecondary = 0xFF9ECE6A;
        static constexpr juce::uint32 text = 0xFFCDD6F4;
        static constexpr juce::uint32 textDim = 0xFF6C7086;
        static constexpr juce::uint32 knobBackground = 0xFF313244;
        static constexpr juce::uint32 knobForeground = 0xFF45475A;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyShifterEditor)
};
