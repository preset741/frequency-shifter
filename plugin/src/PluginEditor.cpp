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
        float framePeakDb = framePeakNorm * 100.0f - 100.0f;  // Reverse the 0-1 to -100..0 dB mapping

        // Update peak detector (fast attack, slow decay)
        if (framePeakDb > currentPeakDb)
        {
            currentPeakDb = framePeakDb;
        }
        else
        {
            currentPeakDb *= peakDecayRate;
            // Clamp to floor
            if (currentPeakDb < floorDb)
                currentPeakDb = floorDb;
        }

        // Update display ceiling (adapts to signal level)
        // Target ceiling is peak + 10dB headroom, clamped to reasonable range
        float targetCeiling = std::min(0.0f, std::max(-60.0f, currentPeakDb + 10.0f));

        if (targetCeiling > displayCeilingDb)
        {
            // Fast attack when signal gets louder
            displayCeilingDb = displayCeilingDb + (targetCeiling - displayCeilingDb) * ceilingAttackRate;
        }
        else
        {
            // Slow decay when signal gets quieter
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

        // Also decay peak detector
        currentPeakDb *= peakDecayRate;
        if (currentPeakDb < floorDb)
            currentPeakDb = floorDb;

        // Slowly decay ceiling back to default
        displayCeilingDb = displayCeilingDb * 0.999f + (-10.0f) * 0.001f;

        if (needsRepaint)
            repaint();
    }
}

void SpectrumAnalyzer::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    // Calculate current display range
    const float rangeDb = displayCeilingDb - floorDb;

    // Background
    g.setColour(juce::Colour(backgroundColor));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Draw grid lines with dynamic dB labels
    g.setColour(juce::Colour(gridColor));

    // Calculate nice grid spacing based on current range
    int gridSpacing = 20;  // Default 20dB spacing
    if (rangeDb < 50.0f) gridSpacing = 10;
    if (rangeDb < 30.0f) gridSpacing = 5;

    // Round ceiling to nearest grid line for cleaner labels
    int ceilingRounded = static_cast<int>(std::ceil(displayCeilingDb / gridSpacing) * gridSpacing);

    // Horizontal grid lines (dB levels) - draw from ceiling down to floor
    for (int db = ceilingRounded; db >= static_cast<int>(floorDb); db -= gridSpacing)
    {
        // Map dB to y position using current dynamic range
        float normalized = (static_cast<float>(db) - floorDb) / rangeDb;
        float y = height * (1.0f - normalized);

        if (y >= 0 && y <= height)
        {
            g.setColour(juce::Colour(gridColor));
            g.drawHorizontalLine(static_cast<int>(y), 0.0f, width);

            // Draw dB label on the left
            g.setColour(juce::Colour(textColor));
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

            // Get magnitude and convert to dB for auto-scaled display
            float magnitudeNorm = smoothedData[static_cast<size_t>(bin)];
            float magnitudeDb = magnitudeNorm * 100.0f - 100.0f;  // Convert 0-1 back to -100..0 dB

            // Map dB to y position using dynamic range
            float normalized = (magnitudeDb - floorDb) / rangeDb;
            normalized = std::max(0.0f, std::min(1.0f, normalized));  // Clamp to valid range
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

    // Setup main shift slider with logarithmic scale (always on)
    setupSlider(shiftSlider, juce::Slider::RotaryHorizontalVerticalDrag);
    shiftSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    // Set up symmetric log scale for the shift knob
    auto rangeToValue = [](double /*rangeStart*/, double /*rangeEnd*/, double normalised) -> double
    {
        // Map from 0..1 to -1..1
        double symNorm = normalised * 2.0 - 1.0;
        double sign = symNorm >= 0.0 ? 1.0 : -1.0;
        double absNorm = std::abs(symNorm);
        constexpr double logScale = 10.0;
        constexpr double maxShift = 20000.0;
        double logMax = std::log(1.0 + maxShift / logScale);
        double absVal = logScale * (std::exp(absNorm * logMax) - 1.0);
        return sign * absVal;
    };

    auto valueToRange = [](double /*rangeStart*/, double /*rangeEnd*/, double value) -> double
    {
        double sign = value >= 0.0 ? 1.0 : -1.0;
        double absVal = std::abs(value);
        constexpr double logScale = 10.0;
        constexpr double maxShift = 20000.0;
        double logMax = std::log(1.0 + maxShift / logScale);
        double normalized = sign * std::log(1.0 + absVal / logScale) / logMax;
        // Map from -1..1 to 0..1
        return (normalized + 1.0) * 0.5;
    };

    auto snapToLegalValue = [](double /*rangeStart*/, double /*rangeEnd*/, double value) -> double
    {
        // Snap to 0.1 Hz resolution for small values, 1 Hz for larger
        if (std::abs(value) < 100.0)
            return std::round(value * 10.0) / 10.0;
        return std::round(value);
    };

    shiftSlider.setNormalisableRange(
        juce::NormalisableRange<double>(-20000.0, 20000.0, rangeToValue, valueToRange, snapToLegalValue));

    addAndMakeVisible(shiftSlider);

    // Add slider listener for manual sync (no attachment - we sync manually due to custom range)
    shiftSlider.addListener(this);

    setupLabel(shiftLabel, "SHIFT (Hz)");
    addAndMakeVisible(shiftLabel);

    // Setup quantize slider
    setupSlider(quantizeSlider, juce::Slider::LinearHorizontal);
    quantizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    quantizeSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(quantizeSlider);
    quantizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_QUANTIZE_STRENGTH, quantizeSlider);

    setupLabel(quantizeLabel, "QUANTIZE");
    addAndMakeVisible(quantizeLabel);

    // Setup preserve slider (Phase 2B: Envelope preservation)
    setupSlider(preserveSlider, juce::Slider::LinearHorizontal);
    preserveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    preserveSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(preserveSlider);
    preserveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_PRESERVE, preserveSlider);

    setupLabel(preserveLabel, "PRESERVE");
    addAndMakeVisible(preserveLabel);

    // Setup transients slider (Phase 2B: Transient bypass)
    setupSlider(transientsSlider, juce::Slider::LinearHorizontal);
    transientsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    transientsSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(transientsSlider);
    transientsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_TRANSIENTS, transientsSlider);

    setupLabel(transientsLabel, "TRANSIENT");
    addAndMakeVisible(transientsLabel);

    // Setup sensitivity slider (Phase 2B: Transient detection threshold)
    setupSlider(sensitivitySlider, juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    sensitivitySlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(sensitivitySlider);
    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SENSITIVITY, sensitivitySlider);

    setupLabel(sensitivityLabel, "SENS");
    addAndMakeVisible(sensitivityLabel);

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
    dryWetSlider.setNumDecimalPlacesToDisplay(1);
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

    // Setup SMEAR slider (5-123ms continuous control)
    setupSlider(smearSlider, juce::Slider::LinearHorizontal);
    smearSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    smearSlider.setNumDecimalPlacesToDisplay(1);
    smearSlider.setTextValueSuffix(" ms");
    addAndMakeVisible(smearSlider);
    smearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_SMEAR, smearSlider);

    setupLabel(smearLabel, "SMEAR");
    addAndMakeVisible(smearLabel);

    // === LFO Modulation Controls ===

    // LFO Depth slider
    setupSlider(lfoDepthSlider, juce::Slider::LinearHorizontal);
    lfoDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    lfoDepthSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(lfoDepthSlider);
    lfoDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_DEPTH, lfoDepthSlider);

    setupLabel(lfoDepthLabel, "DEPTH");
    addAndMakeVisible(lfoDepthLabel);

    // LFO Depth Mode combo (Hz/Deg)
    lfoDepthModeCombo.addItem("Hz", 1);
    lfoDepthModeCombo.addItem("Deg", 2);
    addAndMakeVisible(lfoDepthModeCombo);
    lfoDepthModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_DEPTH_MODE, lfoDepthModeCombo);

    // LFO Rate slider
    setupSlider(lfoRateSlider, juce::Slider::LinearHorizontal);
    lfoRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    lfoRateSlider.setNumDecimalPlacesToDisplay(2);
    addAndMakeVisible(lfoRateSlider);
    lfoRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_RATE, lfoRateSlider);

    setupLabel(lfoRateLabel, "RATE");
    addAndMakeVisible(lfoRateLabel);

    // LFO Sync toggle
    lfoSyncButton.setButtonText("SYNC");
    lfoSyncButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    addAndMakeVisible(lfoSyncButton);
    lfoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_SYNC, lfoSyncButton);

    // LFO Division combo (when synced)
    lfoDivisionCombo.addItem("4/1", 1);
    lfoDivisionCombo.addItem("2/1", 2);
    lfoDivisionCombo.addItem("1/1", 3);
    lfoDivisionCombo.addItem("1/2", 4);
    lfoDivisionCombo.addItem("1/4", 5);
    lfoDivisionCombo.addItem("1/8", 6);
    lfoDivisionCombo.addItem("1/16", 7);
    lfoDivisionCombo.addItem("1/32", 8);
    lfoDivisionCombo.addItem("1/4T", 9);
    lfoDivisionCombo.addItem("1/8T", 10);
    lfoDivisionCombo.addItem("1/16T", 11);
    lfoDivisionCombo.addItem("1/4.", 12);
    lfoDivisionCombo.addItem("1/8.", 13);
    lfoDivisionCombo.addItem("1/16.", 14);
    addAndMakeVisible(lfoDivisionCombo);
    lfoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_DIVISION, lfoDivisionCombo);

    // LFO Shape combo
    lfoShapeCombo.addItem("Sine", 1);
    lfoShapeCombo.addItem("Triangle", 2);
    lfoShapeCombo.addItem("Saw", 3);
    lfoShapeCombo.addItem("Inv Saw", 4);
    lfoShapeCombo.addItem("Random", 5);
    addAndMakeVisible(lfoShapeCombo);
    lfoShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_LFO_SHAPE, lfoShapeCombo);

    setupLabel(lfoShapeLabel, "SHAPE");
    addAndMakeVisible(lfoShapeLabel);

    // Set up sync button callback to toggle between RATE slider and DIV dropdown
    lfoSyncButton.onClick = [this]() { updateLfoSyncUI(); };
    updateLfoSyncUI();  // Initialize visibility

    // === Delay Time LFO Controls ===

    // DLY LFO Depth slider
    setupSlider(dlyLfoDepthSlider, juce::Slider::LinearHorizontal);
    dlyLfoDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    dlyLfoDepthSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(dlyLfoDepthSlider);
    dlyLfoDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_DEPTH, dlyLfoDepthSlider);

    setupLabel(dlyLfoDepthLabel, "DLY DEPTH");
    addAndMakeVisible(dlyLfoDepthLabel);

    // DLY LFO Rate slider
    setupSlider(dlyLfoRateSlider, juce::Slider::LinearHorizontal);
    dlyLfoRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    dlyLfoRateSlider.setNumDecimalPlacesToDisplay(2);
    addAndMakeVisible(dlyLfoRateSlider);
    dlyLfoRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_RATE, dlyLfoRateSlider);

    setupLabel(dlyLfoRateLabel, "DLY RATE");
    addAndMakeVisible(dlyLfoRateLabel);

    // DLY LFO Sync toggle
    dlyLfoSyncButton.setButtonText("SYNC");
    dlyLfoSyncButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    addAndMakeVisible(dlyLfoSyncButton);
    dlyLfoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_SYNC, dlyLfoSyncButton);

    // DLY LFO Division combo (when synced)
    dlyLfoDivisionCombo.addItem("4/1", 1);
    dlyLfoDivisionCombo.addItem("2/1", 2);
    dlyLfoDivisionCombo.addItem("1/1", 3);
    dlyLfoDivisionCombo.addItem("1/2", 4);
    dlyLfoDivisionCombo.addItem("1/4", 5);
    dlyLfoDivisionCombo.addItem("1/8", 6);
    dlyLfoDivisionCombo.addItem("1/16", 7);
    dlyLfoDivisionCombo.addItem("1/32", 8);
    dlyLfoDivisionCombo.addItem("1/4T", 9);
    dlyLfoDivisionCombo.addItem("1/8T", 10);
    dlyLfoDivisionCombo.addItem("1/16T", 11);
    dlyLfoDivisionCombo.addItem("1/4.", 12);
    dlyLfoDivisionCombo.addItem("1/8.", 13);
    dlyLfoDivisionCombo.addItem("1/16.", 14);
    addAndMakeVisible(dlyLfoDivisionCombo);
    dlyLfoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_DIVISION, dlyLfoDivisionCombo);

    // DLY LFO Shape combo
    dlyLfoShapeCombo.addItem("Sine", 1);
    dlyLfoShapeCombo.addItem("Triangle", 2);
    dlyLfoShapeCombo.addItem("Saw", 3);
    dlyLfoShapeCombo.addItem("Inv Saw", 4);
    dlyLfoShapeCombo.addItem("Random", 5);
    addAndMakeVisible(dlyLfoShapeCombo);
    dlyLfoShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DLY_LFO_SHAPE, dlyLfoShapeCombo);

    setupLabel(dlyLfoShapeLabel, "DLY SHAPE");
    addAndMakeVisible(dlyLfoShapeLabel);

    // Set up sync button callback to toggle between RATE slider and DIV dropdown
    dlyLfoSyncButton.onClick = [this]() { updateDlyLfoSyncUI(); };
    updateDlyLfoSyncUI();  // Initialize visibility

    // === Spectral Mask Controls ===

    // Mask enabled toggle
    maskEnabledButton.setButtonText("Mask");
    maskEnabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    addAndMakeVisible(maskEnabledButton);
    maskEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_ENABLED, maskEnabledButton);

    // Mask mode combo
    maskModeCombo.addItem("Low Pass", 1);
    maskModeCombo.addItem("High Pass", 2);
    maskModeCombo.addItem("Band Pass", 3);
    addAndMakeVisible(maskModeCombo);
    maskModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_MODE, maskModeCombo);

    setupLabel(maskModeLabel, "MODE");
    addAndMakeVisible(maskModeLabel);

    // Mask low frequency slider
    setupSlider(maskLowFreqSlider, juce::Slider::LinearHorizontal);
    maskLowFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    maskLowFreqSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(maskLowFreqSlider);
    maskLowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_LOW_FREQ, maskLowFreqSlider);

    setupLabel(maskLowFreqLabel, "LOW");
    addAndMakeVisible(maskLowFreqLabel);

    // Mask high frequency slider
    setupSlider(maskHighFreqSlider, juce::Slider::LinearHorizontal);
    maskHighFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    maskHighFreqSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(maskHighFreqSlider);
    maskHighFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_HIGH_FREQ, maskHighFreqSlider);

    setupLabel(maskHighFreqLabel, "HIGH");
    addAndMakeVisible(maskHighFreqLabel);

    // Mask transition slider
    setupSlider(maskTransitionSlider, juce::Slider::LinearHorizontal);
    maskTransitionSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    maskTransitionSlider.setNumDecimalPlacesToDisplay(1);
    addAndMakeVisible(maskTransitionSlider);
    maskTransitionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_MASK_TRANSITION, maskTransitionSlider);

    setupLabel(maskTransitionLabel, "TRANS");
    addAndMakeVisible(maskTransitionLabel);

    // === Spectral Delay Controls ===

    // Delay enabled toggle
    delayEnabledButton.setButtonText("Delay");
    delayEnabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    addAndMakeVisible(delayEnabledButton);
    delayEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_ENABLED, delayEnabledButton);

    // Delay time slider
    setupSlider(delayTimeSlider, juce::Slider::LinearHorizontal);
    delayTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    delayTimeSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(delayTimeSlider);
    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_TIME, delayTimeSlider);

    setupLabel(delayTimeLabel, "TIME");
    addAndMakeVisible(delayTimeLabel);

    // Delay sync toggle
    delaySyncButton.setButtonText("Sync");
    delaySyncButton.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::text));
    delaySyncButton.onClick = [this]() { updateDelaySyncUI(); };
    addAndMakeVisible(delaySyncButton);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_SYNC, delaySyncButton);

    // Delay division combo (tempo sync divisions)
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

    setupLabel(delayDivisionLabel, "DIV");
    addAndMakeVisible(delayDivisionLabel);

    // Delay slope slider
    setupSlider(delaySlopeSlider, juce::Slider::LinearHorizontal);
    delaySlopeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    delaySlopeSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(delaySlopeSlider);
    delaySlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_SLOPE, delaySlopeSlider);

    setupLabel(delaySlopeLabel, "SLOPE");
    addAndMakeVisible(delaySlopeLabel);

    // Delay feedback slider
    setupSlider(delayFeedbackSlider, juce::Slider::LinearHorizontal);
    delayFeedbackSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    delayFeedbackSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(delayFeedbackSlider);
    delayFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_FEEDBACK, delayFeedbackSlider);

    setupLabel(delayFeedbackLabel, "FDBK");
    addAndMakeVisible(delayFeedbackLabel);

    // Delay damping slider
    setupSlider(delayDampingSlider, juce::Slider::LinearHorizontal);
    delayDampingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    delayDampingSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(delayDampingSlider);
    delayDampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_DAMPING, delayDampingSlider);

    setupLabel(delayDampingLabel, "DAMP");
    addAndMakeVisible(delayDampingLabel);

    // Delay diffuse slider (spectral delay wet/dry - smear effect)
    setupSlider(delayDiffuseSlider, juce::Slider::LinearHorizontal);
    delayDiffuseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    delayDiffuseSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(delayDiffuseSlider);
    delayDiffuseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_DIFFUSE, delayDiffuseSlider);

    setupLabel(delayDiffuseLabel, "DIFFUSE");
    addAndMakeVisible(delayDiffuseLabel);

    // Delay mix slider (time-domain delay echo level)
    setupSlider(delayMixSlider, juce::Slider::LinearHorizontal);
    delayMixSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    delayMixSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(delayMixSlider);
    delayMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), FrequencyShifterProcessor::PARAM_DELAY_MIX, delayMixSlider);

    setupLabel(delayMixLabel, "MIX");
    addAndMakeVisible(delayMixLabel);

    // Stereo decorrelation toggle (testing feature)
    // Applies 0.06ms delay to left channel to reduce phase-locked resonance
    stereoDecorrelateToggle.setButtonText("L/R Decorr");
    stereoDecorrelateToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(Colors::textDim));
    stereoDecorrelateToggle.onClick = [this]() {
        audioProcessor.setStereoDecorrelate(stereoDecorrelateToggle.getToggleState());
    };
    addAndMakeVisible(stereoDecorrelateToggle);

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
            setSize(640, 900);
        else
            setSize(640, 740);
    };
    addAndMakeVisible(spectrumButton);

    // Set editor size (increased for delay time LFO controls)
    setSize(640, 740);

    // Initialize delay sync UI state
    updateDelaySyncUI();
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
    g.fillRoundedRectangle(240.0f, 65.0f, 380.0f, 200.0f, 10.0f);

    // Mix & Drift panel (now taller to fit stochastic row)
    g.fillRoundedRectangle(20.0f, 280.0f, 600.0f, 150.0f, 10.0f);

    // Mask panel (two rows)
    g.fillRoundedRectangle(20.0f, 440.0f, 600.0f, 80.0f, 10.0f);

    // Delay panel (two rows)
    g.fillRoundedRectangle(20.0f, 530.0f, 600.0f, 70.0f, 10.0f);

    // Spectrum panel (when visible)
    if (spectrumVisible)
    {
        g.fillRoundedRectangle(20.0f, 610.0f, 600.0f, 150.0f, 10.0f);
    }
}

void FrequencyShifterEditor::resized()
{
    // Main shift knob - large, centered in left panel
    shiftSlider.setBounds(45, 85, 150, 150);
    shiftLabel.setBounds(45, 235, 150, 20);

    // Scale controls - right panel
    const int rightPanelX = 260;
    const int labelWidth = 80;
    const int controlWidth = 250;

    rootNoteLabel.setBounds(rightPanelX, 80, labelWidth, 20);
    rootNoteCombo.setBounds(rightPanelX + labelWidth, 78, controlWidth, 24);

    scaleTypeLabel.setBounds(rightPanelX, 115, labelWidth, 20);
    scaleTypeCombo.setBounds(rightPanelX + labelWidth, 113, controlWidth, 24);

    quantizeLabel.setBounds(rightPanelX, 150, labelWidth, 20);
    quantizeSlider.setBounds(rightPanelX + labelWidth, 148, controlWidth, 24);

    // Phase 2B: Envelope preservation (near QUANTIZE)
    preserveLabel.setBounds(rightPanelX, 180, labelWidth, 20);
    preserveSlider.setBounds(rightPanelX + labelWidth, 178, controlWidth, 24);

    // Phase 2B: Transient controls (on same row, split width)
    const int transientControlWidth = 115;
    transientsLabel.setBounds(rightPanelX, 210, 65, 20);
    transientsSlider.setBounds(rightPanelX + 65, 208, transientControlWidth, 24);

    sensitivityLabel.setBounds(rightPanelX + 190, 210, 40, 20);
    sensitivitySlider.setBounds(rightPanelX + 230, 208, transientControlWidth, 24);

    phaseVocoderButton.setBounds(rightPanelX, 245, 200, 24);

    smearLabel.setBounds(rightPanelX, 280, labelWidth, 20);
    smearSlider.setBounds(rightPanelX + labelWidth, 278, controlWidth, 24);

    // Mix controls - bottom panel row 1 (shifted down by 60 to accommodate new controls)
    dryWetLabel.setBounds(40, 355, 70, 20);
    dryWetSlider.setBounds(110, 353, 380, 24);
    spectrumButton.setBounds(510, 353, 100, 24);

    // LFO controls - bottom panel row 2
    lfoDepthLabel.setBounds(40, 390, 50, 20);
    lfoDepthSlider.setBounds(90, 388, 130, 24);
    lfoDepthModeCombo.setBounds(225, 388, 55, 24);

    lfoRateLabel.setBounds(295, 390, 40, 20);
    lfoRateSlider.setBounds(335, 388, 100, 24);

    lfoSyncButton.setBounds(445, 388, 60, 24);
    lfoDivisionCombo.setBounds(505, 388, 70, 24);

    // LFO controls - bottom panel row 3
    lfoShapeLabel.setBounds(40, 425, 50, 20);
    lfoShapeCombo.setBounds(90, 423, 100, 24);

    // Delay LFO controls - bottom panel row 4
    dlyLfoDepthLabel.setBounds(40, 460, 70, 20);
    dlyLfoDepthSlider.setBounds(110, 458, 130, 24);

    dlyLfoRateLabel.setBounds(255, 460, 60, 20);
    dlyLfoRateSlider.setBounds(315, 458, 100, 24);

    dlyLfoSyncButton.setBounds(425, 458, 60, 24);
    dlyLfoDivisionCombo.setBounds(485, 458, 70, 24);

    // Delay LFO controls - bottom panel row 5
    dlyLfoShapeLabel.setBounds(40, 495, 70, 20);
    dlyLfoShapeCombo.setBounds(110, 493, 100, 24);

    // Mask controls - row 1: toggle, mode, transition
    maskEnabledButton.setBounds(30, 545, 60, 24);

    maskModeLabel.setBounds(100, 547, 45, 20);
    maskModeCombo.setBounds(145, 545, 100, 24);

    maskTransitionLabel.setBounds(260, 547, 45, 20);
    maskTransitionSlider.setBounds(305, 545, 120, 24);

    // Mask controls - row 2: low and high frequency (wider sliders)
    maskLowFreqLabel.setBounds(30, 582, 35, 20);
    maskLowFreqSlider.setBounds(65, 580, 250, 24);

    maskHighFreqLabel.setBounds(330, 582, 40, 20);
    maskHighFreqSlider.setBounds(370, 580, 240, 24);

    // Delay controls - row 1: toggle, time, sync, division, slope
    delayEnabledButton.setBounds(30, 635, 60, 24);

    delayTimeLabel.setBounds(95, 637, 35, 20);
    delayTimeSlider.setBounds(130, 635, 120, 24);

    delaySyncButton.setBounds(255, 635, 55, 24);

    delayDivisionLabel.setBounds(310, 637, 25, 20);
    delayDivisionCombo.setBounds(335, 635, 70, 24);

    delaySlopeLabel.setBounds(415, 637, 45, 20);
    delaySlopeSlider.setBounds(460, 635, 150, 24);

    // Delay controls - row 2: feedback, damping, diffuse, mix
    delayFeedbackLabel.setBounds(30, 667, 35, 20);
    delayFeedbackSlider.setBounds(65, 665, 100, 24);

    delayDampingLabel.setBounds(175, 667, 40, 20);
    delayDampingSlider.setBounds(215, 665, 90, 24);

    delayDiffuseLabel.setBounds(315, 667, 55, 20);
    delayDiffuseSlider.setBounds(370, 665, 90, 24);

    delayMixLabel.setBounds(470, 667, 30, 20);
    delayMixSlider.setBounds(500, 665, 110, 24);

    // Stereo decorrelation toggle (bottom right corner - testing feature)
    stereoDecorrelateToggle.setBounds(520, 690, 110, 20);

    // Spectrum analyzer (below main controls when visible)
    if (spectrumAnalyzer && spectrumVisible)
    {
        spectrumAnalyzer->setBounds(20, 715, 600, 145);
    }
}

void FrequencyShifterEditor::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &shiftSlider)
    {
        // Manual sync: Get the slider value and update the parameter directly
        // (SliderAttachment doesn't work with custom log scale ranges)
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

    // When SYNC is ON: disable TIME slider, enable DIV dropdown
    // When SYNC is OFF: enable TIME slider, disable DIV dropdown
    delayTimeSlider.setEnabled(!syncEnabled);
    delayTimeSlider.setAlpha(syncEnabled ? 0.4f : 1.0f);
    delayTimeLabel.setAlpha(syncEnabled ? 0.4f : 1.0f);

    delayDivisionCombo.setEnabled(syncEnabled);
    delayDivisionCombo.setAlpha(syncEnabled ? 1.0f : 0.4f);
    delayDivisionLabel.setAlpha(syncEnabled ? 1.0f : 0.4f);
}

void FrequencyShifterEditor::updateLfoSyncUI()
{
    bool syncEnabled = lfoSyncButton.getToggleState();

    // When SYNC is ON: disable RATE slider, enable DIV dropdown
    // When SYNC is OFF: enable RATE slider, disable DIV dropdown
    lfoRateSlider.setEnabled(!syncEnabled);
    lfoRateSlider.setAlpha(syncEnabled ? 0.4f : 1.0f);
    lfoRateLabel.setAlpha(syncEnabled ? 0.4f : 1.0f);

    lfoDivisionCombo.setEnabled(syncEnabled);
    lfoDivisionCombo.setAlpha(syncEnabled ? 1.0f : 0.4f);
}

void FrequencyShifterEditor::updateDlyLfoSyncUI()
{
    bool syncEnabled = dlyLfoSyncButton.getToggleState();

    // When SYNC is ON: disable RATE slider, enable DIV dropdown
    // When SYNC is OFF: enable RATE slider, disable DIV dropdown
    dlyLfoRateSlider.setEnabled(!syncEnabled);
    dlyLfoRateSlider.setAlpha(syncEnabled ? 0.4f : 1.0f);
    dlyLfoRateLabel.setAlpha(syncEnabled ? 0.4f : 1.0f);

    dlyLfoDivisionCombo.setEnabled(syncEnabled);
    dlyLfoDivisionCombo.setAlpha(syncEnabled ? 1.0f : 0.4f);
}

