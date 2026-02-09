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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

/**
 * FrequencyShifterEditor - GUI for the Frequency Shifter plugin.
 *
 * "Holy Shifter" - Frequency Shifter with Harmonic Quantisation
 * Dark theme with gold/amber accent color.
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
    // Custom look and feel for Holy Shifter aesthetic
    class HolyShifterLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HolyShifterLookAndFeel();

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;

        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              juce::Slider::SliderStyle style, juce::Slider& slider) override;

        void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

        void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                          int buttonX, int buttonY, int buttonW, int buttonH,
                          juce::ComboBox& box) override;

        void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                               bool isSeparator, bool isActive, bool isHighlighted,
                               bool isTicked, bool hasSubMenu,
                               const juce::String& text, const juce::String& shortcutKeyText,
                               const juce::Drawable* icon, const juce::Colour* textColour) override;
    };

    // Helper functions
    void setupLabel(juce::Label& label, const juce::String& text, bool isSection = false);
    void setupSlider(juce::Slider& slider, juce::Slider::SliderStyle style);
    void setupHorizontalSlider(juce::Slider& slider);

    // Strip section drawing helper
    void drawStrip(juce::Graphics& g, int y, int height, const juce::String& label = "",
                   bool hasBorder = true, bool dimmed = false);

    // Reference to processor
    FrequencyShifterProcessor& audioProcessor;

    // Custom look and feel
    HolyShifterLookAndFeel holyLookAndFeel;

    // Processing Mode toggle (Classic vs Spectral)
    juce::ComboBox processingModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> processingModeAttachment;
    void updateControlsForMode();  // Enable/disable Spectral-only controls

    // WARM toggle (vintage bandwidth limiting)
    juce::ToggleButton warmButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> warmAttachment;

    // Main frequency shift control
    juce::Slider shiftSlider;
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

    // LFO Modulation controls
    juce::Slider lfoDepthSlider;
    juce::Label lfoDepthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfoDepthAttachment;

    juce::ComboBox lfoDepthModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoDepthModeAttachment;

    juce::Slider lfoRateSlider;
    juce::Label lfoRateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfoRateAttachment;

    juce::ToggleButton lfoSyncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lfoSyncAttachment;

    juce::ComboBox lfoDivisionCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoDivisionAttachment;

    juce::ComboBox lfoShapeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoShapeAttachment;

    // Helper to toggle between RATE slider and DIV dropdown based on SYNC state
    void updateLfoSyncUI();

    // Delay Time LFO controls
    juce::Slider dlyLfoDepthSlider;
    juce::Label dlyLfoDepthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dlyLfoDepthAttachment;

    juce::Slider dlyLfoRateSlider;
    juce::Label dlyLfoRateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dlyLfoRateAttachment;

    juce::ToggleButton dlyLfoSyncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dlyLfoSyncAttachment;

    juce::ComboBox dlyLfoDivisionCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dlyLfoDivisionAttachment;

    juce::ComboBox dlyLfoShapeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dlyLfoShapeAttachment;

    // Helper to toggle between RATE slider and DIV dropdown based on DLY SYNC state
    void updateDlyLfoSyncUI();

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

    juce::ToggleButton delaySyncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> delaySyncAttachment;

    juce::ComboBox delayDivisionCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> delayDivisionAttachment;

    // Timer to update UI state based on sync toggle
    void updateDelaySyncUI();

    juce::Slider delaySlopeSlider;
    juce::Label delaySlopeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delaySlopeAttachment;

    juce::Slider delayFeedbackSlider;
    juce::Label delayFeedbackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayFeedbackAttachment;

    juce::Slider delayDampingSlider;
    juce::Label delayDampingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayDampingAttachment;

    juce::Slider delayDiffuseSlider;
    juce::Label delayDiffuseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayDiffuseAttachment;

    // Stereo decorrelation toggle (testing feature)
    juce::ToggleButton stereoDecorrelateToggle;

public:
    // UI colors - Holy Shifter color scheme (public for SpectrumAnalyzer access)
    struct Colors
    {
        // Background colors
        static constexpr juce::uint32 background = 0xFF0A0A0C;
        static constexpr juce::uint32 surface = 0xFF111113;
        static constexpr juce::uint32 strip = 0xFF0E0E10;
        static constexpr juce::uint32 stripBorder = 0xFF1A1A1D;
        static constexpr juce::uint32 raised = 0xFF161618;
        static constexpr juce::uint32 border = 0xFF1E1E22;
        static constexpr juce::uint32 borderDim = 0xFF151517;
        static constexpr juce::uint32 panelBg = 0xFF0D0D0F;
        static constexpr juce::uint32 panelBorder = 0xFF1C1C20;

        // Text colors
        static constexpr juce::uint32 text = 0xFFE8E4DB;
        static constexpr juce::uint32 textSec = 0xFF8A857D;
        static constexpr juce::uint32 textMuted = 0xFF3E3A34;

        // Accent colors (gold/amber)
        static constexpr juce::uint32 accent = 0xFFC9A96E;
        static constexpr juce::uint32 accentDim = 0xFF6B5D3D;
        static constexpr juce::uint32 accentGlow = 0x26C9A96E;

        // Track color
        static constexpr juce::uint32 track = 0xFF252320;
    };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyShifterEditor)
};
