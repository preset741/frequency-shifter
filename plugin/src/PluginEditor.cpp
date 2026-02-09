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
        // Find peak in current frame (data is normalized 0-1, representing -100dB to 0dB)
        float framePeakNorm = 0.0f;
        for (size_t i = 0; i < SPECTRUM_SIZE; ++i)
        {
            framePeakNorm = std::max(framePeakNorm, spectrumData[i]);
        }

        // Convert normalized value back to dB for auto-scaling
        float framePeakDb = framePeakNorm * 100.0f - 100.0f;

        // Update peak detector (fast attack, slow decay)
        if (framePeakDb > currentPeakDb)
        {
            currentPeakDb = framePeakDb;
        }
        else
        {
            currentPeakDb *= peakDecayRate;
            if (currentPeakDb < floorDb)
                currentPeakDb = floorDb;
        }

        // Update display ceiling (adapts to signal level)
        float targetCeiling = std::min(0.0f, std::max(-60.0f, currentPeakDb + 10.0f));

        if (targetCeiling > displayCeilingDb)
        {
            displayCeilingDb = displayCeilingDb + (targetCeiling - displayCeilingDb) * ceilingAttackRate;
        }
        else
        {
            displayCeilingDb = displayCeilingDb * ceilingDecayRate + targetCeiling * (1.0f - ceilingDecayRate);
        }

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

        currentPeakDb *= peakDecayRate;
        if (currentPeakDb < floorDb)
            currentPeakDb = floorDb;

        displayCeilingDb = displayCeilingDb * 0.999f + (-10.0f) * 0.001f;

        if (needsRepaint)
            repaint();
    }
}

void SpectrumAnalyzer::paint(juce::Graphics& g)
{
    using Colors = FrequencyShifterEditor::Colors;

    const auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    const float rangeDb = displayCeilingDb - floorDb;

    // Background
    g.setColour(juce::Colour(Colors::strip));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Draw grid lines with dynamic dB labels
    g.setColour(juce::Colour(Colors::stripBorder));

    int gridSpacing = 20;
    if (rangeDb < 50.0f) gridSpacing = 10;
    if (rangeDb < 30.0f) gridSpacing = 5;

    int ceilingRounded = static_cast<int>(std::ceil(displayCeilingDb / gridSpacing) * gridSpacing);

    for (int db = ceilingRounded; db >= static_cast<int>(floorDb); db -= gridSpacing)
    {
        float normalized = (static_cast<float>(db) - floorDb) / rangeDb;
        float y = height * (1.0f - normalized);

        if (y >= 0 && y <= height)
        {
            g.setColour(juce::Colour(Colors::stripBorder));
            g.drawHorizontalLine(static_cast<int>(y), 0.0f, width);

            g.setColour(juce::Colour(Colors::textMuted));
            g.setFont(juce::FontOptions(8.0f));
            g.drawText(juce::String(db), 2, static_cast<int>(y) - 6, 25, 12,
                       juce::Justification::left, false);
        }
    }

    // Get frequency info
    const double sampleRate = audioProcessor.getSampleRate();
    const int fftSize = audioProcessor.getCurrentFFTSize();
    const int numBins = fftSize / 2;

    // Frequency labels (logarithmic scale)
    g.setColour(juce::Colour(Colors::textMuted));
    g.setFont(juce::FontOptions(9.0f));

    const std::array<float, 6> freqLabels = { 100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    for (float freq : freqLabels)
    {
        if (freq < sampleRate / 2.0)
        {
            const float fMin = 20.0f;
            const float fMax = static_cast<float>(sampleRate / 2.0);
            float x = std::log(freq / fMin) / std::log(fMax / fMin) * width;

            g.setColour(juce::Colour(Colors::stripBorder));
            g.drawVerticalLine(static_cast<int>(x), 0.0f, height);

            g.setColour(juce::Colour(Colors::textMuted));
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

            if (binFreq < fMin)
                continue;

            float x = std::log(binFreq / fMin) / std::log(fMax / fMin) * width;

            float magnitudeNorm = smoothedData[static_cast<size_t>(bin)];
            float magnitudeDb = magnitudeNorm * 100.0f - 100.0f;

            float normalized = (magnitudeDb - floorDb) / rangeDb;
            normalized = std::max(0.0f, std::min(1.0f, normalized));
            float y = height * (1.0f - normalized);

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

        if (pathStarted)
        {
            fillPath.lineTo(width, height);
            fillPath.closeSubPath();

            // Draw filled area with accent glow
            g.setColour(juce::Colour(Colors::accentGlow));
            g.fillPath(fillPath);

            // Draw spectrum line with accent color
            g.setColour(juce::Colour(Colors::accent));
            g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
        }
    }

    // Border
    g.setColour(juce::Colour(Colors::stripBorder));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void SpectrumAnalyzer::resized()
{
    // Nothing special needed
}

//==============================================================================
// HolyShifterLookAndFeel Implementation
//==============================================================================

FrequencyShifterEditor::HolyShifterLookAndFeel::HolyShifterLookAndFeel()
{
    // Set default colors for Holy Shifter theme
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(Colors::accent));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(Colors::track));
    setColour(juce::Slider::thumbColourId, juce::Colour(Colors::accent));
    setColour(juce::Slider::trackColourId, juce::Colour(Colors::track));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(Colors::text));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ComboBox::backgroundColourId, juce::Colour(Colors::raised));
    setColour(juce::ComboBox::textColourId, juce::Colour(Colors::text));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(Colors::border));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(Colors::textMuted));

    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(Colors::raised));
    setColour(juce::PopupMenu::textColourId, juce::Colour(Colors::text));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(Colors::accentDim));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(Colors::text));

    setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    setColour(juce::ToggleButton::tickColourId, juce::Colour(Colors::accent));
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(Colors::textMuted));

    setColour(juce::Label::textColourId, juce::Colour(Colors::text));
}

void FrequencyShifterEditor::HolyShifterLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    const float radius = static_cast<float>(juce::jmin(width / 2, height / 2)) - 18.0f;
    const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Check if bipolar (shift knob has min < 0 and max > 0)
    double minVal = slider.getMinimum();
    double maxVal = slider.getMaximum();
    bool isBipolar = (minVal < 0 && maxVal > 0);
    float centerAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);

    // Subtle outer ring
    g.setColour(juce::Colour(Colors::borderDim));
    g.drawEllipse(centreX - radius - 8.0f, centreY - radius - 8.0f,
                  (radius + 8.0f) * 2.0f, (radius + 8.0f) * 2.0f, 0.5f);

    // Background arc (track)
    g.setColour(juce::Colour(Colors::track));
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.strokePath(backgroundArc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

    // Value arc with glow effect
    if (isBipolar)
    {
        // Bipolar: draw from center to current position
        float startAngle = (sliderPosProportional >= 0.5f) ? centerAngle : angle;
        float endAngle = (sliderPosProportional >= 0.5f) ? angle : centerAngle;

        if (std::abs(endAngle - startAngle) > 0.01f)
        {
            g.setColour(juce::Colour(Colors::accent));
            juce::Path valueArc;
            valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                    startAngle, endAngle, true);
            g.strokePath(valueArc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }
    }
    else
    {
        // Unipolar: draw from start to current position
        if (sliderPosProportional > 0.0f)
        {
            g.setColour(juce::Colour(Colors::accent));
            juce::Path valueArc;
            valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                    rotaryStartAngle, angle, true);
            g.strokePath(valueArc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }
    }

    // Tick marks at 0%, 25%, 50%, 75%, 100%
    for (int i = 0; i <= 4; ++i)
    {
        float tickNorm = static_cast<float>(i) / 4.0f;
        float tickAngle = rotaryStartAngle + tickNorm * (rotaryEndAngle - rotaryStartAngle);
        float tickAngleRad = tickAngle - juce::MathConstants<float>::halfPi;

        float innerR = radius + 6.0f;
        float outerR = radius + 10.0f;

        float x1 = centreX + innerR * std::cos(tickAngleRad);
        float y1 = centreY + innerR * std::sin(tickAngleRad);
        float x2 = centreX + outerR * std::cos(tickAngleRad);
        float y2 = centreY + outerR * std::sin(tickAngleRad);

        // Center tick (50%) is brighter for bipolar knobs
        bool isCenterTick = (i == 2) && isBipolar;
        g.setColour(juce::Colour(isCenterTick ? Colors::textSec : Colors::textMuted));
        g.drawLine(x1, y1, x2, y2, 0.6f);
    }

    // Indicator dot
    float indicatorAngleRad = angle - juce::MathConstants<float>::halfPi;
    float dotX = centreX + radius * std::cos(indicatorAngleRad);
    float dotY = centreY + radius * std::sin(indicatorAngleRad);
    g.setColour(juce::Colour(Colors::accent));
    g.fillEllipse(dotX - 3.0f, dotY - 3.0f, 6.0f, 6.0f);

    // Value text in center
    g.setColour(juce::Colour(Colors::text));
    g.setFont(juce::FontOptions(26.0f).withStyle("Light"));

    double value = slider.getValue();
    juce::String valueText;
    if (std::abs(value) >= 100.0)
        valueText = juce::String(static_cast<int>(value));
    else
        valueText = juce::String(value, 1);

    g.drawText(valueText, static_cast<int>(centreX - 50), static_cast<int>(centreY - 14),
               100, 28, juce::Justification::centred, false);

    // Unit text below value
    g.setColour(juce::Colour(Colors::textMuted));
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("HZ", static_cast<int>(centreX - 20), static_cast<int>(centreY + 14),
               40, 12, juce::Justification::centred, false);
}

void FrequencyShifterEditor::HolyShifterLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        const float trackY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float trackHeight = 1.5f;

        // Background track
        g.setColour(juce::Colour(Colors::track));
        g.fillRoundedRectangle(static_cast<float>(x), trackY - trackHeight * 0.5f,
                                static_cast<float>(width), trackHeight, 1.0f);

        // Value track with accent color
        float valueWidth = sliderPos - static_cast<float>(x);
        if (valueWidth > 0)
        {
            g.setColour(juce::Colour(Colors::accent));
            g.fillRoundedRectangle(static_cast<float>(x), trackY - trackHeight * 0.5f,
                                    valueWidth, trackHeight, 1.0f);
        }

        // Thumb (small circle)
        const float thumbRadius = 3.5f;
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

void FrequencyShifterEditor::HolyShifterLookAndFeel::drawToggleButton(
    juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    (void)shouldDrawButtonAsHighlighted;
    (void)shouldDrawButtonAsDown;

    auto bounds = button.getLocalBounds().toFloat();
    bool isOn = button.getToggleState();

    // Pill-style toggle dimensions
    const float toggleWidth = 26.0f;
    const float toggleHeight = 13.0f;
    const float dotSize = 9.0f;

    // Draw toggle pill
    float toggleX = 0.0f;
    float toggleY = (bounds.getHeight() - toggleHeight) * 0.5f;

    g.setColour(juce::Colour(isOn ? Colors::accentDim : Colors::track));
    g.fillRoundedRectangle(toggleX, toggleY, toggleWidth, toggleHeight, toggleHeight * 0.5f);

    // Draw toggle dot
    float dotX = isOn ? (toggleX + toggleWidth - dotSize - 2.0f) : (toggleX + 2.0f);
    float dotY = toggleY + (toggleHeight - dotSize) * 0.5f;
    g.setColour(juce::Colour(isOn ? Colors::accent : Colors::textMuted));
    g.fillEllipse(dotX, dotY, dotSize, dotSize);

    // Draw label text
    g.setColour(juce::Colour(isOn ? Colors::text : Colors::textSec));
    g.setFont(juce::FontOptions(9.0f));

    auto textBounds = bounds.withLeft(toggleWidth + 6.0f);
    g.drawText(button.getButtonText(), textBounds, juce::Justification::centredLeft, false);
}

void FrequencyShifterEditor::HolyShifterLookAndFeel::drawComboBox(
    juce::Graphics& g, int width, int height, bool isButtonDown,
    int buttonX, int buttonY, int buttonW, int buttonH,
    juce::ComboBox& box)
{
    (void)isButtonDown;
    (void)buttonX;
    (void)buttonY;
    (void)buttonW;
    (void)buttonH;

    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));

    // Background
    g.setColour(juce::Colour(Colors::raised));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border
    g.setColour(juce::Colour(Colors::border));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    // Arrow
    g.setColour(juce::Colour(Colors::textMuted));
    float arrowX = static_cast<float>(width) - 12.0f;
    float arrowY = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(arrowX - 3.0f, arrowY - 2.0f,
                      arrowX + 3.0f, arrowY - 2.0f,
                      arrowX, arrowY + 2.0f);
    g.fillPath(arrow);
}

void FrequencyShifterEditor::HolyShifterLookAndFeel::drawPopupMenuItem(
    juce::Graphics& g, const juce::Rectangle<int>& area,
    bool isSeparator, bool isActive, bool isHighlighted,
    bool isTicked, bool hasSubMenu,
    const juce::String& text, const juce::String& shortcutKeyText,
    const juce::Drawable* icon, const juce::Colour* textColour)
{
    (void)isSeparator;
    (void)isActive;
    (void)isTicked;
    (void)hasSubMenu;
    (void)shortcutKeyText;
    (void)icon;
    (void)textColour;

    if (isHighlighted)
    {
        g.setColour(juce::Colour(Colors::accentDim));
        g.fillRect(area);
    }

    g.setColour(juce::Colour(Colors::text));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(text, area.reduced(8, 0), juce::Justification::centredLeft, true);
}

//==============================================================================
// FrequencyShifterEditor Implementation
//==============================================================================

FrequencyShifterEditor::FrequencyShifterEditor(FrequencyShifterProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&holyLookAndFeel);

    // Processing mode combo (Classic vs Spectral)
    // Parameter order: 0=Classic, 1=Spectral (must match processor's StringArray)
    processingModeCombo.addItem("Classic", 1);   // ID 1 -> Index 0
    processingModeCombo.addItem("Spectral", 2);  // ID 2 -> Index 1
    processingModeCombo.onChange = [this]() { updateControlsForMode(); };
    addAndMakeVisible(processingModeCombo);
    processingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_PROCESSING_MODE, processingModeCombo);

    // WARM toggle
    warmButton.setButtonText("Warm");
    addAndMakeVisible(warmButton);
    warmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_WARM, warmButton);

    // Main shift knob with logarithmic scale
    shiftSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    shiftSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    auto rangeToValue = [](double, double, double normalised) -> double
    {
        double symNorm = normalised * 2.0 - 1.0;
        double sign = symNorm >= 0.0 ? 1.0 : -1.0;
        double absNorm = std::abs(symNorm);
        constexpr double logScale = 10.0;
        constexpr double maxShift = 5000.0;
        double logMax = std::log(1.0 + maxShift / logScale);
        double absVal = logScale * (std::exp(absNorm * logMax) - 1.0);
        return sign * absVal;
    };

    auto valueToRange = [](double, double, double value) -> double
    {
        double sign = value >= 0.0 ? 1.0 : -1.0;
        double absVal = std::abs(value);
        constexpr double logScale = 10.0;
        constexpr double maxShift = 5000.0;
        double logMax = std::log(1.0 + maxShift / logScale);
        double normalized = sign * std::log(1.0 + absVal / logScale) / logMax;
        return (normalized + 1.0) * 0.5;
    };

    auto snapToLegalValue = [](double, double, double value) -> double
    {
        if (std::abs(value) < 100.0)
            return std::round(value * 10.0) / 10.0;
        return std::round(value);
    };

    shiftSlider.setNormalisableRange(
        juce::NormalisableRange<double>(-5000.0, 5000.0, rangeToValue, valueToRange, snapToLegalValue));

    addAndMakeVisible(shiftSlider);
    shiftSlider.addListener(this);

    // Root note selector
    for (const auto& note : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" })
    {
        rootNoteCombo.addItem(note, rootNoteCombo.getNumItems() + 1);
    }
    addAndMakeVisible(rootNoteCombo);
    rootNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_ROOT_NOTE, rootNoteCombo);

    setupLabel(rootNoteLabel, "Root");
    addAndMakeVisible(rootNoteLabel);

    // Scale type selector
    for (const auto& name : fshift::getScaleNames())
    {
        scaleTypeCombo.addItem(name, scaleTypeCombo.getNumItems() + 1);
    }
    addAndMakeVisible(scaleTypeCombo);
    scaleTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SCALE_TYPE, scaleTypeCombo);

    // Quantize slider
    setupHorizontalSlider(quantizeSlider);
    quantizeSlider.setTextValueSuffix("");
    addAndMakeVisible(quantizeSlider);
    quantizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_QUANTIZE_STRENGTH, quantizeSlider);

    setupLabel(quantizeLabel, "Quantize");
    addAndMakeVisible(quantizeLabel);

    // Preserve slider
    setupHorizontalSlider(preserveSlider);
    addAndMakeVisible(preserveSlider);
    preserveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_PRESERVE, preserveSlider);

    setupLabel(preserveLabel, "Preserve");
    addAndMakeVisible(preserveLabel);

    // Transients slider
    setupHorizontalSlider(transientsSlider);
    addAndMakeVisible(transientsSlider);
    transientsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_TRANSIENTS, transientsSlider);

    setupLabel(transientsLabel, "Transient");
    addAndMakeVisible(transientsLabel);

    // Sensitivity slider
    setupHorizontalSlider(sensitivitySlider);
    sensitivitySlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(sensitivitySlider);
    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SENSITIVITY, sensitivitySlider);

    setupLabel(sensitivityLabel, "Sens");
    addAndMakeVisible(sensitivityLabel);

    // Enhanced mode toggle
    phaseVocoderButton.setButtonText("Enhanced");
    addAndMakeVisible(phaseVocoderButton);
    phaseVocoderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_PHASE_VOCODER, phaseVocoderButton);

    // SMEAR slider
    setupHorizontalSlider(smearSlider);
    smearSlider.setTextValueSuffix(" ms");
    addAndMakeVisible(smearSlider);
    smearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SMEAR, smearSlider);

    setupLabel(smearLabel, "Smear");
    addAndMakeVisible(smearLabel);

    // === LFO Modulation Controls ===

    setupHorizontalSlider(lfoDepthSlider);
    lfoDepthSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(lfoDepthSlider);
    lfoDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_DEPTH, lfoDepthSlider);

    setupLabel(lfoDepthLabel, "Depth");
    addAndMakeVisible(lfoDepthLabel);

    lfoDepthModeCombo.addItem("Hz", 1);
    lfoDepthModeCombo.addItem("Degrees", 2);
    addAndMakeVisible(lfoDepthModeCombo);
    lfoDepthModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_DEPTH_MODE, lfoDepthModeCombo);

    setupHorizontalSlider(lfoRateSlider);
    lfoRateSlider.setTextValueSuffix(" Hz");
    lfoRateSlider.setNumDecimalPlacesToDisplay(2);
    addAndMakeVisible(lfoRateSlider);
    lfoRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_RATE, lfoRateSlider);

    setupLabel(lfoRateLabel, "Rate");
    addAndMakeVisible(lfoRateLabel);

    lfoSyncButton.setButtonText("Sync");
    addAndMakeVisible(lfoSyncButton);
    lfoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_SYNC, lfoSyncButton);

    lfoDivisionCombo.addItem("4/1", 1);
    lfoDivisionCombo.addItem("2/1", 2);
    lfoDivisionCombo.addItem("1/1", 3);
    lfoDivisionCombo.addItem("1/2", 4);
    lfoDivisionCombo.addItem("1/4", 5);
    lfoDivisionCombo.addItem("1/8", 6);
    lfoDivisionCombo.addItem("1/16", 7);
    lfoDivisionCombo.addItem("1/32", 8);
    addAndMakeVisible(lfoDivisionCombo);
    lfoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_DIVISION, lfoDivisionCombo);

    lfoShapeCombo.addItem("Sine", 1);
    lfoShapeCombo.addItem("Triangle", 2);
    lfoShapeCombo.addItem("Saw", 3);
    lfoShapeCombo.addItem("Inv Saw", 4);
    lfoShapeCombo.addItem("Random", 5);
    addAndMakeVisible(lfoShapeCombo);
    lfoShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_SHAPE, lfoShapeCombo);

    lfoSyncButton.onClick = [this]() { updateLfoSyncUI(); };
    updateLfoSyncUI();

    // === Delay Controls ===

    delayEnabledButton.setButtonText("Delay");
    addAndMakeVisible(delayEnabledButton);
    delayEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_ENABLED, delayEnabledButton);

    setupHorizontalSlider(delayTimeSlider);
    delayTimeSlider.setTextValueSuffix(" ms");
    delayTimeSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(delayTimeSlider);
    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_TIME, delayTimeSlider);

    setupLabel(delayTimeLabel, "Time");
    addAndMakeVisible(delayTimeLabel);

    delaySyncButton.setButtonText("Sync");
    delaySyncButton.onClick = [this]() { updateDelaySyncUI(); };
    addAndMakeVisible(delaySyncButton);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_SYNC, delaySyncButton);

    delayDivisionCombo.addItem("1/32", 1);
    delayDivisionCombo.addItem("1/16T", 2);
    delayDivisionCombo.addItem("1/16", 3);
    delayDivisionCombo.addItem("1/16D", 4);
    delayDivisionCombo.addItem("1/8T", 5);
    delayDivisionCombo.addItem("1/8", 6);
    delayDivisionCombo.addItem("1/8D", 7);
    delayDivisionCombo.addItem("1/4T", 8);
    delayDivisionCombo.addItem("1/4", 9);
    delayDivisionCombo.addItem("1/4D", 10);
    delayDivisionCombo.addItem("1/2T", 11);
    delayDivisionCombo.addItem("1/2", 12);
    delayDivisionCombo.addItem("1/2D", 13);
    delayDivisionCombo.addItem("1/1", 14);
    delayDivisionCombo.addItem("2/1", 15);
    delayDivisionCombo.addItem("4/1", 16);
    addAndMakeVisible(delayDivisionCombo);
    delayDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_DIVISION, delayDivisionCombo);

    setupHorizontalSlider(delayFeedbackSlider);
    delayFeedbackSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(delayFeedbackSlider);
    delayFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_FEEDBACK, delayFeedbackSlider);

    setupLabel(delayFeedbackLabel, "Fdbk");
    addAndMakeVisible(delayFeedbackLabel);

    setupHorizontalSlider(delayDampingSlider);
    delayDampingSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(delayDampingSlider);
    delayDampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_DAMPING, delayDampingSlider);

    setupLabel(delayDampingLabel, "Damp");
    addAndMakeVisible(delayDampingLabel);

    setupHorizontalSlider(delaySlopeSlider);
    delaySlopeSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(delaySlopeSlider);
    delaySlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_SLOPE, delaySlopeSlider);

    setupLabel(delaySlopeLabel, "Slope");
    addAndMakeVisible(delaySlopeLabel);

    setupHorizontalSlider(delayDiffuseSlider);
    delayDiffuseSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(delayDiffuseSlider);
    delayDiffuseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_DIFFUSE, delayDiffuseSlider);

    setupLabel(delayDiffuseLabel, "Diffuse");
    addAndMakeVisible(delayDiffuseLabel);

    stereoDecorrelateToggle.setButtonText("L/R Decorr");
    stereoDecorrelateToggle.onClick = [this]() {
        audioProcessor.setStereoDecorrelate(stereoDecorrelateToggle.getToggleState());
    };
    addAndMakeVisible(stereoDecorrelateToggle);

    // === Delay Time LFO Controls ===

    setupHorizontalSlider(dlyLfoDepthSlider);
    dlyLfoDepthSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(dlyLfoDepthSlider);
    dlyLfoDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_DEPTH, dlyLfoDepthSlider);

    setupLabel(dlyLfoDepthLabel, "Depth");
    addAndMakeVisible(dlyLfoDepthLabel);

    setupHorizontalSlider(dlyLfoRateSlider);
    dlyLfoRateSlider.setTextValueSuffix(" Hz");
    dlyLfoRateSlider.setNumDecimalPlacesToDisplay(2);
    addAndMakeVisible(dlyLfoRateSlider);
    dlyLfoRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_RATE, dlyLfoRateSlider);

    setupLabel(dlyLfoRateLabel, "Rate");
    addAndMakeVisible(dlyLfoRateLabel);

    dlyLfoSyncButton.setButtonText("Sync");
    addAndMakeVisible(dlyLfoSyncButton);
    dlyLfoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_SYNC, dlyLfoSyncButton);

    dlyLfoDivisionCombo.addItem("4/1", 1);
    dlyLfoDivisionCombo.addItem("2/1", 2);
    dlyLfoDivisionCombo.addItem("1/1", 3);
    dlyLfoDivisionCombo.addItem("1/2", 4);
    dlyLfoDivisionCombo.addItem("1/4", 5);
    dlyLfoDivisionCombo.addItem("1/8", 6);
    dlyLfoDivisionCombo.addItem("1/16", 7);
    dlyLfoDivisionCombo.addItem("1/32", 8);
    addAndMakeVisible(dlyLfoDivisionCombo);
    dlyLfoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_DIVISION, dlyLfoDivisionCombo);

    dlyLfoShapeCombo.addItem("Sine", 1);
    dlyLfoShapeCombo.addItem("Triangle", 2);
    dlyLfoShapeCombo.addItem("Saw", 3);
    dlyLfoShapeCombo.addItem("Inv Saw", 4);
    dlyLfoShapeCombo.addItem("Random", 5);
    addAndMakeVisible(dlyLfoShapeCombo);
    dlyLfoShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_SHAPE, dlyLfoShapeCombo);

    dlyLfoSyncButton.onClick = [this]() { updateDlyLfoSyncUI(); };
    updateDlyLfoSyncUI();

    // === Spectral Mask Controls ===

    maskEnabledButton.setButtonText("Mask");
    addAndMakeVisible(maskEnabledButton);
    maskEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_ENABLED, maskEnabledButton);

    maskModeCombo.addItem("Low Pass", 1);
    maskModeCombo.addItem("High Pass", 2);
    maskModeCombo.addItem("Band Pass", 3);
    addAndMakeVisible(maskModeCombo);
    maskModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_MODE, maskModeCombo);

    setupHorizontalSlider(maskLowFreqSlider);
    maskLowFreqSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(maskLowFreqSlider);
    maskLowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_LOW_FREQ, maskLowFreqSlider);

    setupLabel(maskLowFreqLabel, "Low");
    addAndMakeVisible(maskLowFreqLabel);

    setupHorizontalSlider(maskHighFreqSlider);
    maskHighFreqSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(maskHighFreqSlider);
    maskHighFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_HIGH_FREQ, maskHighFreqSlider);

    setupLabel(maskHighFreqLabel, "High");
    addAndMakeVisible(maskHighFreqLabel);

    setupHorizontalSlider(maskTransitionSlider);
    maskTransitionSlider.setNumDecimalPlacesToDisplay(2);
    maskTransitionSlider.setTextValueSuffix(" oct");
    addAndMakeVisible(maskTransitionSlider);
    maskTransitionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_TRANSITION, maskTransitionSlider);

    setupLabel(maskTransitionLabel, "Trans");
    addAndMakeVisible(maskTransitionLabel);

    // === Dry/Wet Mix ===

    setupHorizontalSlider(dryWetSlider);
    addAndMakeVisible(dryWetSlider);
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DRY_WET, dryWetSlider);

    setupLabel(dryWetLabel, "Dry / Wet");
    addAndMakeVisible(dryWetLabel);

    // Spectrum toggle
    spectrumButton.setButtonText("Spectrum");
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

        if (spectrumVisible)
            setSize(600, 950);
        else
            setSize(600, 800);
    };
    addAndMakeVisible(spectrumButton);

    // Set editor size (600px width as per mockup)
    setSize(600, 800);

    // Initialize UI states
    updateDelaySyncUI();
    updateControlsForMode();
}

FrequencyShifterEditor::~FrequencyShifterEditor()
{
    shiftSlider.removeListener(this);
    setLookAndFeel(nullptr);
}

void FrequencyShifterEditor::setupLabel(juce::Label& label, const juce::String& text, bool isSection)
{
    label.setText(text, juce::dontSendNotification);
    if (isSection)
    {
        label.setFont(juce::FontOptions(8.0f));
        label.setColour(juce::Label::textColourId, juce::Colour(Colors::textMuted));
    }
    else
    {
        label.setFont(juce::FontOptions(9.0f));
        label.setColour(juce::Label::textColourId, juce::Colour(Colors::textSec));
    }
    label.setJustificationType(juce::Justification::centredRight);
}

void FrequencyShifterEditor::setupSlider(juce::Slider& slider, juce::Slider::SliderStyle style)
{
    slider.setSliderStyle(style);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(Colors::text));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void FrequencyShifterEditor::setupHorizontalSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    slider.setNumDecimalPlacesToDisplay(1);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(Colors::text));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void FrequencyShifterEditor::drawStrip(juce::Graphics& g, int y, int height,
                                        const juce::String& label, bool hasBorder, bool dimmed)
{
    auto stripBounds = juce::Rectangle<float>(0, static_cast<float>(y),
                                               static_cast<float>(getWidth()), static_cast<float>(height));

    // Strip background
    g.setColour(juce::Colour(Colors::strip).withAlpha(dimmed ? 0.3f : 1.0f));
    g.fillRect(stripBounds);

    // Top border
    if (hasBorder)
    {
        g.setColour(juce::Colour(Colors::stripBorder));
        g.drawHorizontalLine(y, 0, static_cast<float>(getWidth()));
    }
}

void FrequencyShifterEditor::paint(juce::Graphics& g)
{
    // Main background
    g.fillAll(juce::Colour(Colors::background));

    // Top accent line (gold gradient)
    {
        juce::ColourGradient gradient(
            juce::Colours::transparentBlack, 0, 0,
            juce::Colours::transparentBlack, static_cast<float>(getWidth()), 0,
            false);
        gradient.addColour(0.08, juce::Colours::transparentBlack);
        gradient.addColour(0.3, juce::Colour(Colors::accentDim));
        gradient.addColour(0.5, juce::Colour(Colors::accent));
        gradient.addColour(0.7, juce::Colour(Colors::accentDim));
        gradient.addColour(0.92, juce::Colours::transparentBlack);
        g.setGradientFill(gradient);
        g.fillRect(0, 0, getWidth(), 1);
    }

    // Title - "Holy Shifter" with serif font
    g.setColour(juce::Colour(Colors::text));
    g.setFont(juce::FontOptions(22.0f).withStyle("Light"));
    g.drawText("H O L Y   S H I F T E R", 24, 16, 300, 28, juce::Justification::centredLeft, false);

    // Subtitle
    g.setColour(juce::Colour(Colors::textMuted));
    g.setFont(juce::FontOptions(10.0f).withStyle("Italic"));
    g.drawText("Frequency Shifter with Harmonic Quantisation", 24, 42, 350, 14,
               juce::Justification::centredLeft, false);

    // Mode indicator (right side of title bar)
    bool isSpectral = (processingModeCombo.getSelectedId() == 2);
    g.setColour(juce::Colour(isSpectral ? Colors::accent : Colors::textMuted));
    g.setFont(juce::FontOptions(8.0f));
    g.drawText(isSpectral ? "SPECTRAL" : "CLASSIC", getWidth() - 100, 20, 80, 12,
               juce::Justification::centredRight, false);

    // Spectral panel background
    g.setColour(juce::Colour(Colors::panelBg));
    g.fillRoundedRectangle(208.0f, 70.0f, 368.0f, 150.0f, 6.0f);
    g.setColour(juce::Colour(Colors::panelBorder));
    g.drawRoundedRectangle(208.0f, 70.0f, 368.0f, 150.0f, 6.0f, 1.0f);

    // Strip sections
    int stripY = 230;

    // Smear & Enhance strip
    drawStrip(g, stripY, 50, "Smear & Enhance", true, !isSpectral);
    g.setColour(juce::Colour(Colors::textMuted));
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("SMEAR & ENHANCE", 24, stripY + 4, 150, 12, juce::Justification::centredLeft, false);
    stripY += 50;

    // Freq Modulation strip
    drawStrip(g, stripY, 70, "Freq Modulation", true, false);
    g.setColour(juce::Colour(Colors::textMuted));
    g.drawText("FREQ MODULATION", 24, stripY + 4, 150, 12, juce::Justification::centredLeft, false);
    stripY += 70;

    // Delay strip
    drawStrip(g, stripY, 130, "Delay", true, false);
    g.setColour(juce::Colour(Colors::textMuted));
    g.drawText("DELAY", 24, stripY + 4, 100, 12, juce::Justification::centredLeft, false);
    stripY += 130;

    // Delay Modulation strip
    drawStrip(g, stripY, 70, "Delay Modulation", true, false);
    g.setColour(juce::Colour(Colors::textMuted));
    g.drawText("DELAY MODULATION", 24, stripY + 4, 150, 12, juce::Justification::centredLeft, false);
    stripY += 70;

    // Mask strip
    drawStrip(g, stripY, 80, "Mask", true, !isSpectral);
    g.setColour(juce::Colour(Colors::textMuted));
    g.drawText("MASK", 24, stripY + 4, 100, 12, juce::Justification::centredLeft, false);
    stripY += 80;

    // Mix strip
    drawStrip(g, stripY, 50, "Mix", true, false);
    stripY += 50;

    // Bottom accent line
    {
        juce::ColourGradient gradient(
            juce::Colours::transparentBlack, 0, static_cast<float>(getHeight() - 1),
            juce::Colours::transparentBlack, static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1),
            false);
        gradient.addColour(0.15, juce::Colours::transparentBlack);
        gradient.addColour(0.5, juce::Colour(Colors::borderDim));
        gradient.addColour(0.85, juce::Colours::transparentBlack);
        g.setGradientFill(gradient);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
}

void FrequencyShifterEditor::resized()
{
    const int margin = 24;
    const int rowHeight = 24;
    const int labelWidth = 55;
    const int sliderWidth = 200;

    // Title bar controls
    processingModeCombo.setBounds(208, 78, 96, 22);
    warmButton.setBounds(getWidth() - margin - 80, 36, 80, 22);

    // Main shift knob (left side)
    shiftSlider.setBounds(24, 70, 180, 180);

    // Spectral panel controls (right side)
    int panelX = 220;
    int panelY = 108;
    int panelRowGap = 26;

    rootNoteLabel.setBounds(panelX, panelY, 35, 20);
    rootNoteCombo.setBounds(panelX + 40, panelY, 58, 22);
    scaleTypeCombo.setBounds(panelX + 105, panelY, 128, 22);
    panelY += panelRowGap;

    quantizeLabel.setBounds(panelX, panelY, 52, 20);
    quantizeSlider.setBounds(panelX + 55, panelY, 180, 20);
    panelY += panelRowGap;

    preserveLabel.setBounds(panelX, panelY, 52, 20);
    preserveSlider.setBounds(panelX + 55, panelY, 180, 20);
    panelY += panelRowGap;

    transientsLabel.setBounds(panelX, panelY, 52, 20);
    transientsSlider.setBounds(panelX + 55, panelY, 100, 20);
    sensitivityLabel.setBounds(panelX + 160, panelY, 30, 20);
    sensitivitySlider.setBounds(panelX + 190, panelY, 80, 20);

    // Strip sections
    int stripY = 230;
    int stripPadding = 20;

    // Smear & Enhance strip
    phaseVocoderButton.setBounds(margin, stripY + stripPadding, 90, 22);
    smearLabel.setBounds(margin + 100, stripY + stripPadding, 38, 20);
    smearSlider.setBounds(margin + 145, stripY + stripPadding, getWidth() - margin * 2 - 155, 20);
    stripY += 50;

    // Freq Modulation strip
    int lfoY = stripY + stripPadding;
    lfoDepthLabel.setBounds(margin, lfoY, 38, 20);
    lfoDepthSlider.setBounds(margin + 45, lfoY, 140, 20);
    lfoDepthModeCombo.setBounds(margin + 195, lfoY, 72, 22);

    lfoY += 26;
    lfoRateLabel.setBounds(margin, lfoY, 38, 20);
    lfoRateSlider.setBounds(margin + 45, lfoY, 140, 20);
    lfoSyncButton.setBounds(margin + 200, lfoY, 70, 22);
    lfoDivisionCombo.setBounds(margin + 280, lfoY, 58, 22);
    lfoShapeCombo.setBounds(getWidth() - margin - 78, lfoY, 78, 22);
    stripY += 70;

    // Delay strip
    int delY = stripY + stripPadding;
    delayEnabledButton.setBounds(margin, delY, 70, 22);
    delayTimeLabel.setBounds(margin + 80, delY, 38, 20);
    delayTimeSlider.setBounds(margin + 125, delY, 140, 20);
    delaySyncButton.setBounds(margin + 280, delY, 70, 22);
    delayDivisionCombo.setBounds(margin + 360, delY, 58, 22);

    delY += 26;
    delayFeedbackLabel.setBounds(margin, delY, 38, 20);
    delayFeedbackSlider.setBounds(margin + 45, delY, 120, 20);
    delayDampingLabel.setBounds(margin + 175, delY, 38, 20);
    delayDampingSlider.setBounds(margin + 220, delY, 120, 20);

    delY += 26;
    delaySlopeLabel.setBounds(margin, delY, 38, 20);
    delaySlopeSlider.setBounds(margin + 45, delY, 120, 20);
    delayDiffuseLabel.setBounds(margin + 175, delY, 48, 20);
    delayDiffuseSlider.setBounds(margin + 228, delY, 120, 20);

    delY += 26;
    stereoDecorrelateToggle.setBounds(getWidth() - margin - 100, delY, 100, 20);
    stripY += 130;

    // Delay Modulation strip
    int dlyLfoY = stripY + stripPadding;
    dlyLfoDepthLabel.setBounds(margin, dlyLfoY, 38, 20);
    dlyLfoDepthSlider.setBounds(margin + 45, dlyLfoY, 140, 20);

    dlyLfoY += 26;
    dlyLfoRateLabel.setBounds(margin, dlyLfoY, 38, 20);
    dlyLfoRateSlider.setBounds(margin + 45, dlyLfoY, 140, 20);
    dlyLfoSyncButton.setBounds(margin + 200, dlyLfoY, 70, 22);
    dlyLfoDivisionCombo.setBounds(margin + 280, dlyLfoY, 58, 22);
    dlyLfoShapeCombo.setBounds(getWidth() - margin - 78, dlyLfoY, 78, 22);
    stripY += 70;

    // Mask strip
    int maskY = stripY + stripPadding;
    maskEnabledButton.setBounds(margin, maskY, 70, 22);
    maskModeCombo.setBounds(margin + 80, maskY, 88, 22);
    maskTransitionLabel.setBounds(margin + 180, maskY, 34, 20);
    maskTransitionSlider.setBounds(margin + 220, maskY, 120, 20);

    maskY += 26;
    maskLowFreqLabel.setBounds(margin, maskY, 24, 20);
    maskLowFreqSlider.setBounds(margin + 30, maskY, 200, 20);
    maskHighFreqLabel.setBounds(margin + 245, maskY, 28, 20);
    maskHighFreqSlider.setBounds(margin + 280, maskY, 200, 20);
    stripY += 80;

    // Mix strip
    int mixY = stripY + 12;
    dryWetLabel.setBounds(margin, mixY, 55, 20);
    dryWetSlider.setBounds(margin + 65, mixY, getWidth() - margin * 2 - 180, 20);
    spectrumButton.setBounds(getWidth() - margin - 100, mixY, 100, 22);
    stripY += 50;

    // Spectrum analyzer (when visible)
    if (spectrumAnalyzer && spectrumVisible)
    {
        spectrumAnalyzer->setBounds(margin, stripY + 10, getWidth() - margin * 2, 130);
    }
}

void FrequencyShifterEditor::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &shiftSlider)
    {
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

void FrequencyShifterEditor::updateDelaySyncUI()
{
    bool syncEnabled = delaySyncButton.getToggleState();

    delayTimeSlider.setEnabled(!syncEnabled);
    delayTimeSlider.setAlpha(syncEnabled ? 0.35f : 1.0f);
    delayTimeLabel.setAlpha(syncEnabled ? 0.35f : 1.0f);

    delayDivisionCombo.setEnabled(syncEnabled);
    delayDivisionCombo.setAlpha(syncEnabled ? 1.0f : 0.35f);
}

void FrequencyShifterEditor::updateLfoSyncUI()
{
    bool syncEnabled = lfoSyncButton.getToggleState();

    lfoRateSlider.setEnabled(!syncEnabled);
    lfoRateSlider.setAlpha(syncEnabled ? 0.35f : 1.0f);
    lfoRateLabel.setAlpha(syncEnabled ? 0.35f : 1.0f);

    lfoDivisionCombo.setEnabled(syncEnabled);
    lfoDivisionCombo.setAlpha(syncEnabled ? 1.0f : 0.35f);
}

void FrequencyShifterEditor::updateDlyLfoSyncUI()
{
    bool syncEnabled = dlyLfoSyncButton.getToggleState();

    dlyLfoRateSlider.setEnabled(!syncEnabled);
    dlyLfoRateSlider.setAlpha(syncEnabled ? 0.35f : 1.0f);
    dlyLfoRateLabel.setAlpha(syncEnabled ? 0.35f : 1.0f);

    dlyLfoDivisionCombo.setEnabled(syncEnabled);
    dlyLfoDivisionCombo.setAlpha(syncEnabled ? 1.0f : 0.35f);
}

void FrequencyShifterEditor::updateControlsForMode()
{
    // Classic is ID=1, Spectral is ID=2 (matching processor's 0=Classic, 1=Spectral)
    bool isClassic = (processingModeCombo.getSelectedId() == 1);
    float disabledAlpha = 0.25f;
    float enabledAlpha = 1.0f;

    // SMEAR - Spectral only
    smearSlider.setEnabled(!isClassic);
    smearSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    smearLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // Quantize, Root, Scale - Spectral only
    quantizeSlider.setEnabled(!isClassic);
    quantizeSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    quantizeLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    rootNoteCombo.setEnabled(!isClassic);
    rootNoteCombo.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    rootNoteLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    scaleTypeCombo.setEnabled(!isClassic);
    scaleTypeCombo.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // PRESERVE, TRANSIENTS, SENSITIVITY - Spectral only
    preserveSlider.setEnabled(!isClassic);
    preserveSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    preserveLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    transientsSlider.setEnabled(!isClassic);
    transientsSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    transientsLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    sensitivitySlider.setEnabled(!isClassic);
    sensitivitySlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    sensitivityLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // LFO Depth Mode - Spectral only
    lfoDepthModeCombo.setEnabled(!isClassic);
    lfoDepthModeCombo.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // Enhanced Mode (Phase Vocoder) - Spectral only
    phaseVocoderButton.setEnabled(!isClassic);
    phaseVocoderButton.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // Mask controls - all Spectral only
    maskEnabledButton.setEnabled(!isClassic);
    maskEnabledButton.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    maskModeCombo.setEnabled(!isClassic);
    maskModeCombo.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    maskLowFreqSlider.setEnabled(!isClassic);
    maskLowFreqSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    maskLowFreqLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    maskHighFreqSlider.setEnabled(!isClassic);
    maskHighFreqSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    maskHighFreqLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    maskTransitionSlider.setEnabled(!isClassic);
    maskTransitionSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    maskTransitionLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // SLOPE, DIFFUSE - Spectral delay features
    delaySlopeSlider.setEnabled(!isClassic);
    delaySlopeSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    delaySlopeLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    delayDiffuseSlider.setEnabled(!isClassic);
    delayDiffuseSlider.setAlpha(isClassic ? disabledAlpha : enabledAlpha);
    delayDiffuseLabel.setAlpha(isClassic ? disabledAlpha : enabledAlpha);

    // Trigger repaint to update strip dimming
    repaint();
}
