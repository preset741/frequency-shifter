#include "PluginEditor.h"
#include "dsp/Scales.h"
#include <cmath>

//==============================================================================
// SpectrumAnalyzer Implementation
//==============================================================================

SpectrumAnalyzer::SpectrumAnalyzer(FrequencyShifterProcessor& processor)
    : audioProcessor(processor)
{
    startTimerHz(30);  // 30 FPS refresh rate
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stopTimer();
}

void SpectrumAnalyzer::timerCallback()
{
    if (audioProcessor.getSpectrumData(spectrumData))
    {
        // Apply smoothing for visual appeal
        for (size_t i = 0; i < SPECTRUM_SIZE; ++i)
        {
            smoothedData[i] = smoothedData[i] * smoothingFactor + spectrumData[i] * (1.0f - smoothingFactor);
        }
        repaint();
    }
    else
    {
        // Decay when no new data
        bool needsRepaint = false;
        for (size_t i = 0; i < SPECTRUM_SIZE; ++i)
        {
            if (smoothedData[i] > 0.001f)
            {
                smoothedData[i] *= 0.95f;
                needsRepaint = true;
            }
        }
        if (needsRepaint)
            repaint();
    }
}

void SpectrumAnalyzer::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    // Background
    g.setColour(juce::Colour(backgroundColor));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Draw grid lines
    g.setColour(juce::Colour(gridColor));

    // Horizontal grid lines (dB levels)
    for (int db = -80; db <= 0; db += 20)
    {
        float y = height * (1.0f - (db + 100.0f) / 100.0f);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, width);
    }

    // Get frequency info
    const double sampleRate = audioProcessor.getSampleRate();
    const int fftSize = audioProcessor.getCurrentFFTSize();
    const int numBins = fftSize / 2;

    // Frequency labels (logarithmic scale)
    g.setColour(juce::Colour(textColor));
    g.setFont(juce::FontOptions(9.0f));

    const std::array<float, 6> freqLabels = { 100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    for (float freq : freqLabels)
    {
        if (freq < sampleRate / 2.0)
        {
            // Log scale mapping: x = log(f/fMin) / log(fMax/fMin)
            const float fMin = 20.0f;
            const float fMax = static_cast<float>(sampleRate / 2.0);
            float x = std::log(freq / fMin) / std::log(fMax / fMin) * width;

            g.setColour(juce::Colour(gridColor));
            g.drawVerticalLine(static_cast<int>(x), 0.0f, height);

            g.setColour(juce::Colour(textColor));
            juce::String label = freq >= 1000.0f
                ? juce::String(static_cast<int>(freq / 1000)) + "k"
                : juce::String(static_cast<int>(freq));
            g.drawText(label, static_cast<int>(x) - 15, static_cast<int>(height) - 12, 30, 12,
                       juce::Justification::centred, false);
        }
    }

    // Draw spectrum
    if (numBins > 0)
    {
        juce::Path spectrumPath;
        juce::Path fillPath;

        const float fMin = 20.0f;
        const float fMax = static_cast<float>(sampleRate / 2.0);
        const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftSize);

        bool pathStarted = false;

        for (int bin = 1; bin < std::min(numBins, SPECTRUM_SIZE); ++bin)
        {
            float binFreq = static_cast<float>(bin) * binWidth;

            // Skip bins below minimum frequency
            if (binFreq < fMin)
                continue;

            // Log scale x position
            float x = std::log(binFreq / fMin) / std::log(fMax / fMin) * width;

            // Get magnitude (clamped)
            float magnitude = smoothedData[static_cast<size_t>(bin)];
            float y = height * (1.0f - magnitude);

            if (!pathStarted)
            {
                spectrumPath.startNewSubPath(x, y);
                fillPath.startNewSubPath(x, height);
                fillPath.lineTo(x, y);
                pathStarted = true;
            }
            else
            {
                spectrumPath.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }

        // Complete fill path
        if (pathStarted)
        {
            fillPath.lineTo(width, height);
            fillPath.closeSubPath();

            // Draw filled area
            g.setColour(juce::Colour(spectrumFillColor));
            g.fillPath(fillPath);

            // Draw spectrum line
            g.setColour(juce::Colour(spectrumColor));
            g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
        }
    }

    // Border
    g.setColour(juce::Colour(gridColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void SpectrumAnalyzer::resized()
{
    // Nothing special needed
}

//==============================================================================
// ModernLookAndFeel Implementation
//==============================================================================

FrequencyShifterEditor::ModernLookAndFeel::ModernLookAndFeel()
{
    // Set default colors
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(Colors::accent));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(Colors::knobBackground));
    setColour(juce::Slider::thumbColourId, juce::Colour(Colors::accent));
    setColour(juce::Slider::trackColourId, juce::Colour(Colors::knobBackground));

    setColour(juce::ComboBox::backgroundColourId, juce::Colour(Colors::knobBackground));
    setColour(juce::ComboBox::textColourId, juce::Colour(Colors::text));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(Colors::knobForeground));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(Colors::accent));

    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(Colors::panelBackground));
    setColour(juce::PopupMenu::textColourId, juce::Colour(Colors::text));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(Colors::accent));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(Colors::background));

    setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    setColour(juce::ToggleButton::tickColourId, juce::Colour(Colors::accent));
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(Colors::textDim));

    setColour(juce::Label::textColourId, juce::Colour(Colors::text));
}

void FrequencyShifterEditor::ModernLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    const float radius = static_cast<float>(juce::jmin(width / 2, height / 2)) - 4.0f;
    const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float rx = centreX - radius;
    const float ry = centreY - radius;
    const float rw = radius * 2.0f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Background arc
    g.setColour(juce::Colour(Colors::knobBackground));
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.strokePath(backgroundArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

    // Value arc
    if (sliderPosProportional > 0.0f)
    {
        g.setColour(juce::Colour(Colors::accent));
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                rotaryStartAngle, angle, true);
        g.strokePath(valueArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // Center dot
    g.setColour(juce::Colour(Colors::knobForeground));
    g.fillEllipse(centreX - radius * 0.6f, centreY - radius * 0.6f,
                  radius * 1.2f, radius * 1.2f);

    // Pointer
    juce::Path pointer;
    const float pointerLength = radius * 0.5f;
    const float pointerThickness = 3.0f;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 8.0f,
                                 pointerThickness, pointerLength, 1.0f);
    g.setColour(juce::Colour(Colors::accent));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    // Value text in center
    g.setColour(juce::Colour(Colors::text));
    g.setFont(juce::FontOptions(14.0f));

    juce::String valueText;
    double value = slider.getValue();
    if (std::abs(value) >= 100.0)
        valueText = juce::String(static_cast<int>(value));
    else
        valueText = juce::String(value, 1);

    g.drawText(valueText, static_cast<int>(centreX - 30), static_cast<int>(centreY - 8),
               60, 16, juce::Justification::centred, false);
}

void FrequencyShifterEditor::ModernLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        const float trackY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float trackHeight = 4.0f;

        // Background track
        g.setColour(juce::Colour(Colors::knobBackground));
        g.fillRoundedRectangle(static_cast<float>(x), trackY - trackHeight * 0.5f,
                                static_cast<float>(width), trackHeight, 2.0f);

        // Value track
        g.setColour(juce::Colour(Colors::accent));
        g.fillRoundedRectangle(static_cast<float>(x), trackY - trackHeight * 0.5f,
                                sliderPos - static_cast<float>(x), trackHeight, 2.0f);

        // Thumb
        const float thumbRadius = 8.0f;
        g.setColour(juce::Colour(Colors::accent));
        g.fillEllipse(sliderPos - thumbRadius, trackY - thumbRadius,
                      thumbRadius * 2.0f, thumbRadius * 2.0f);
    }
    else
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                minSliderPos, maxSliderPos, style, slider);
    }
}

//==============================================================================
// FrequencyShifterEditor Implementation
//==============================================================================

FrequencyShifterEditor::FrequencyShifterEditor(FrequencyShifterProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&modernLookAndFeel);

    // Setup main shift slider
    setupSlider(shiftSlider, juce::Slider::RotaryHorizontalVerticalDrag);
    shiftSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(shiftSlider);
    shiftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SHIFT_HZ, shiftSlider);

    setupLabel(shiftLabel, "SHIFT (Hz)");
    addAndMakeVisible(shiftLabel);

    // Setup quantize slider
    setupSlider(quantizeSlider, juce::Slider::LinearHorizontal);
    quantizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(quantizeSlider);
    quantizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_QUANTIZE_STRENGTH, quantizeSlider);

    setupLabel(quantizeLabel, "QUANTIZE");
    addAndMakeVisible(quantizeLabel);

    // Setup root note combo (just 12 pitch classes - octave is irrelevant for scale quantization)
    for (const auto& note : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" })
    {
        rootNoteCombo.addItem(note, rootNoteCombo.getNumItems() + 1);
    }
    addAndMakeVisible(rootNoteCombo);
    rootNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_ROOT_NOTE, rootNoteCombo);

    setupLabel(rootNoteLabel, "ROOT");
    addAndMakeVisible(rootNoteLabel);

    // Setup scale type combo
    for (const auto& name : fshift::getScaleNames())
    {
        scaleTypeCombo.addItem(name, scaleTypeCombo.getNumItems() + 1);
    }
    addAndMakeVisible(scaleTypeCombo);
    scaleTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SCALE_TYPE, scaleTypeCombo);

    setupLabel(scaleTypeLabel, "SCALE");
    addAndMakeVisible(scaleTypeLabel);

    // Setup dry/wet slider
    setupSlider(dryWetSlider, juce::Slider::LinearHorizontal);
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(dryWetSlider);
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DRY_WET, dryWetSlider);

    setupLabel(dryWetLabel, "DRY/WET");
    addAndMakeVisible(dryWetLabel);

    // Setup phase vocoder toggle
    phaseVocoderButton.setButtonText("Enhanced Mode");
    phaseVocoderButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    addAndMakeVisible(phaseVocoderButton);
    phaseVocoderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_PHASE_VOCODER, phaseVocoderButton);

    // Setup quality mode combo
    qualityModeCombo.addItem("Low Latency (~23ms)", 1);
    qualityModeCombo.addItem("Balanced (~46ms)", 2);
    qualityModeCombo.addItem("Quality (~93ms)", 3);
    addAndMakeVisible(qualityModeCombo);
    qualityModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_QUALITY_MODE, qualityModeCombo);

    setupLabel(qualityModeLabel, "LATENCY");
    addAndMakeVisible(qualityModeLabel);

    // Setup log scale toggle (placed below shift knob)
    logScaleButton.setButtonText("Log");
    logScaleButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    logScaleButton.onClick = [this]() { updateShiftSliderRange(); };
    addAndMakeVisible(logScaleButton);
    logScaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LOG_SCALE, logScaleButton);

    // Add slider listener for manual sync in log mode
    shiftSlider.addListener(this);

    // Setup spectrum analyzer toggle
    spectrumButton.setButtonText("Spectrum");
    spectrumButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    spectrumButton.onClick = [this]()
    {
        spectrumVisible = spectrumButton.getToggleState();
        if (spectrumVisible && !spectrumAnalyzer)
        {
            spectrumAnalyzer = std::make_unique<SpectrumAnalyzer>(audioProcessor);
            addAndMakeVisible(*spectrumAnalyzer);
        }
        if (spectrumAnalyzer)
            spectrumAnalyzer->setVisible(spectrumVisible);

        // Resize window when spectrum is toggled
        if (spectrumVisible)
            setSize(500, 520);
        else
            setSize(500, 400);
    };
    addAndMakeVisible(spectrumButton);

    // Set editor size
    setSize(500, 400);
}

FrequencyShifterEditor::~FrequencyShifterEditor()
{
    shiftSlider.removeListener(this);
    setLookAndFeel(nullptr);
}

void FrequencyShifterEditor::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    label.setColour(juce::Label::textColourId, juce::Colour(Colors::textDim));
    label.setJustificationType(juce::Justification::centred);
}

void FrequencyShifterEditor::setupSlider(juce::Slider& slider, juce::Slider::SliderStyle style)
{
    slider.setSliderStyle(style);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(Colors::accent));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(Colors::knobBackground));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(Colors::text));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(Colors::knobBackground));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void FrequencyShifterEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient gradient(juce::Colour(Colors::background), 0, 0,
                                   juce::Colour(Colors::panelBackground), 0, static_cast<float>(getHeight()),
                                   false);
    g.setGradientFill(gradient);
    g.fillAll();

    // Title
    g.setColour(juce::Colour(Colors::text));
    g.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
    g.drawText("FREQUENCY SHIFTER", 0, 10, getWidth(), 30, juce::Justification::centred);

    // Subtitle
    g.setColour(juce::Colour(Colors::textDim));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("Harmonic-preserving pitch shift", 0, 35, getWidth(), 20, juce::Justification::centred);

    // Panel backgrounds
    g.setColour(juce::Colour(Colors::panelBackground).withAlpha(0.5f));

    // Main shift panel
    g.fillRoundedRectangle(20.0f, 65.0f, 200.0f, 200.0f, 10.0f);

    // Controls panel
    g.fillRoundedRectangle(240.0f, 65.0f, 240.0f, 200.0f, 10.0f);

    // Mix panel
    g.fillRoundedRectangle(20.0f, 280.0f, 460.0f, 100.0f, 10.0f);

    // Spectrum panel (when visible)
    if (spectrumVisible)
    {
        g.fillRoundedRectangle(20.0f, 390.0f, 460.0f, 120.0f, 10.0f);
    }
}

void FrequencyShifterEditor::resized()
{
    // Main shift knob - large, centered in left panel
    shiftSlider.setBounds(45, 85, 150, 150);
    shiftLabel.setBounds(45, 235, 100, 20);
    logScaleButton.setBounds(145, 235, 50, 20);

    // Scale controls - right panel
    const int rightPanelX = 260;
    const int labelWidth = 60;
    const int controlWidth = 150;

    rootNoteLabel.setBounds(rightPanelX, 80, labelWidth, 20);
    rootNoteCombo.setBounds(rightPanelX + labelWidth, 78, controlWidth, 24);

    scaleTypeLabel.setBounds(rightPanelX, 115, labelWidth, 20);
    scaleTypeCombo.setBounds(rightPanelX + labelWidth, 113, controlWidth, 24);

    quantizeLabel.setBounds(rightPanelX, 150, labelWidth, 20);
    quantizeSlider.setBounds(rightPanelX + labelWidth, 148, controlWidth, 24);

    phaseVocoderButton.setBounds(rightPanelX, 185, 200, 24);

    qualityModeLabel.setBounds(rightPanelX, 220, labelWidth, 20);
    qualityModeCombo.setBounds(rightPanelX + labelWidth, 218, controlWidth, 24);

    // Mix controls - bottom panel
    dryWetLabel.setBounds(40, 305, 60, 20);
    dryWetSlider.setBounds(100, 303, 260, 24);
    spectrumButton.setBounds(370, 303, 100, 24);

    // Spectrum analyzer (below main controls when visible)
    if (spectrumAnalyzer && spectrumVisible)
    {
        spectrumAnalyzer->setBounds(20, 395, 460, 110);
    }
}

// Symmetric log transform: sign(x) * log(1 + |x|/scale) / log(1 + max/scale)
// This gives fine control near 0 and coarser control at extremes
// Scale of 10 means: 0-10 Hz uses about 1/6 of the knob travel from center
static constexpr double LOG_SCALE = 10.0;
static constexpr double MAX_SHIFT = 20000.0;

static double symLogTransform(double value)
{
    double sign = value >= 0.0 ? 1.0 : -1.0;
    double absVal = std::abs(value);
    double logMax = std::log(1.0 + MAX_SHIFT / LOG_SCALE);
    double normalized = sign * std::log(1.0 + absVal / LOG_SCALE) / logMax;
    // Map from -1..1 to 0..1
    return (normalized + 1.0) * 0.5;
}

static double symLogInverse(double normalized)
{
    // Map from 0..1 to -1..1
    double symNorm = normalized * 2.0 - 1.0;
    double sign = symNorm >= 0.0 ? 1.0 : -1.0;
    double absNorm = std::abs(symNorm);
    double logMax = std::log(1.0 + MAX_SHIFT / LOG_SCALE);
    double absVal = LOG_SCALE * (std::exp(absNorm * logMax) - 1.0);
    return sign * absVal;
}

void FrequencyShifterEditor::sliderValueChanged(juce::Slider* slider)
{
    // Only handle manual sync when in log mode (attachment is disconnected)
    if (slider == &shiftSlider && isLogModeActive)
    {
        // Get the slider value and update the parameter directly
        float value = static_cast<float>(shiftSlider.getValue());
        auto* param = audioProcessor.getValueTreeState().getParameter(FrequencyShifterProcessor::PARAM_SHIFT_HZ);
        if (param != nullptr)
        {
            // Convert to normalized 0-1 range for the parameter (-20000 to +20000)
            float normalized = (value + 20000.0f) / 40000.0f;
            param->setValueNotifyingHost(normalized);
        }
    }
}

void FrequencyShifterEditor::updateShiftSliderRange()
{
    // This is called when log/linear mode changes
    const bool newLogMode = logScaleButton.getToggleState();

    // If mode didn't change, nothing to do
    if (newLogMode == isLogModeActive)
        return;

    // Get current parameter value
    auto* param = audioProcessor.getValueTreeState().getParameter(FrequencyShifterProcessor::PARAM_SHIFT_HZ);
    float currentValue = 0.0f;
    if (param != nullptr)
    {
        // Denormalize from 0-1 to -20000..20000
        currentValue = param->getValue() * 40000.0f - 20000.0f;
    }

    if (newLogMode)
    {
        // Switching TO log mode
        // Detach the SliderAttachment (it doesn't support custom ranges)
        shiftAttachment.reset();

        // Set up custom symmetric log range
        auto rangeToValue = [](double /*rangeStart*/, double /*rangeEnd*/, double normalised) -> double
        {
            return symLogInverse(normalised);
        };

        auto valueToRange = [](double /*rangeStart*/, double /*rangeEnd*/, double value) -> double
        {
            return symLogTransform(value);
        };

        auto snapToLegalValue = [](double /*rangeStart*/, double /*rangeEnd*/, double value) -> double
        {
            // Snap to 0.1 Hz resolution for small values, 1 Hz for larger
            if (std::abs(value) < 100.0)
                return std::round(value * 10.0) / 10.0;
            return std::round(value);
        };

        shiftSlider.setNormalisableRange(
            juce::NormalisableRange<double>(-MAX_SHIFT, MAX_SHIFT, rangeToValue, valueToRange, snapToLegalValue));

        // Set the current value
        shiftSlider.setValue(currentValue, juce::dontSendNotification);

        isLogModeActive = true;
    }
    else
    {
        // Switching TO linear mode
        // First set linear range
        shiftSlider.setNormalisableRange(
            juce::NormalisableRange<double>(-20000.0, 20000.0, 1.0));

        // Restore value before reattaching
        shiftSlider.setValue(currentValue, juce::dontSendNotification);

        // Reattach - this will sync properly with linear range
        shiftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SHIFT_HZ, shiftSlider);

        isLogModeActive = false;
    }
}
