#pragma once

#include <cmath>
#include <random>
#include <algorithm>

namespace fshift {

/**
 * LfoModulator - LFO for modulating frequency shift amount.
 *
 * Features:
 * - Multiple waveform shapes (Sine, Triangle, Saw, Inv Saw, Random)
 * - Free-running or tempo-synced operation
 * - Optional quantization to scale-degree intervals
 * - Random mode jumps instantly to new values (no smoothing)
 */
class LfoModulator
{
public:
    enum class Shape
    {
        Sine = 0,
        Triangle,
        Saw,
        InvSaw,
        Random
    };

    // Tempo sync divisions (relative to quarter note)
    enum class SyncDivision
    {
        Off = 0,      // Use Hz rate
        Div_4_1,      // 4 bars (16 quarter notes)
        Div_2_1,      // 2 bars (8 quarter notes)
        Div_1_1,      // 1 bar (4 quarter notes)
        Div_1_2,      // Half note
        Div_1_4,      // Quarter note
        Div_1_8,      // Eighth note
        Div_1_16,     // Sixteenth note
        Div_1_32,     // 32nd note
        NumDivisions
    };

    LfoModulator() = default;

    void prepare(double sr)
    {
        sampleRate = sr;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        currentRawValue = 0.0f;
        currentSHValue = 0.0f;
        lastPhaseQuadrant = 0;
    }

    // Set LFO rate in Hz (used when sync is off)
    void setRateHz(float hz)
    {
        rateHz = std::max(0.01f, hz);
    }

    // Set tempo sync division
    void setSyncDivision(SyncDivision div)
    {
        syncDivision = div;
    }

    // Set sync division from integer (0 = Off, 1-8 = divisions)
    void setSyncDivision(int divIndex)
    {
        divIndex = std::clamp(divIndex, 0, static_cast<int>(SyncDivision::NumDivisions) - 1);
        syncDivision = static_cast<SyncDivision>(divIndex);
    }

    // Set host tempo in BPM
    void setTempo(double bpm)
    {
        hostTempo = std::max(20.0, bpm);
    }

    // Set waveform shape
    void setShape(Shape s)
    {
        shape = s;
    }

    // Set shape from integer
    void setShape(int shapeIndex)
    {
        shapeIndex = std::clamp(shapeIndex, 0, 4);
        shape = static_cast<Shape>(shapeIndex);
    }

    // Set modulation amount in Hz (bipolar: -amount to +amount)
    void setAmount(float hz)
    {
        amount = std::max(0.0f, hz);
    }

    // Enable/disable quantization to scale degrees
    void setQuantizeEnabled(bool enabled)
    {
        quantizeEnabled = enabled;
    }

    // Set scale degree intervals for quantization (in Hz per scale degree)
    // This should be set based on root note frequency and scale type
    void setScaleDegreeInterval(float intervalHz)
    {
        scaleDegreeInterval = std::max(1.0f, intervalHz);
    }

    // Process one sample and return modulation value in Hz
    float process()
    {
        // Calculate effective rate
        float effectiveRateHz = getEffectiveRate();

        // Update phase
        double phaseIncrement = effectiveRateHz / sampleRate;
        phase += phaseIncrement;

        // Wrap phase
        while (phase >= 1.0)
            phase -= 1.0;

        // Generate raw LFO value (-1 to +1) and store it
        currentRawValue = generateWaveform(static_cast<float>(phase));

        // Scale by amount
        float modulationHz = currentRawValue * amount;

        // Apply quantization if enabled
        if (quantizeEnabled && scaleDegreeInterval > 0.0f)
        {
            modulationHz = quantizeToScaleDegrees(modulationHz);
        }

        return modulationHz;
    }

    // Get current LFO phase (0-1)
    float getPhase() const { return static_cast<float>(phase); }

    // Get current raw waveform value (-1 to +1) without scaling
    float getCurrentValue() const { return currentRawValue; }

    // Get effective rate in Hz (accounting for tempo sync)
    float getEffectiveRate() const
    {
        if (syncDivision == SyncDivision::Off)
            return rateHz;

        // Calculate rate from tempo and sync division
        double quarterNoteHz = hostTempo / 60.0;  // Quarter notes per second
        double multiplier = getSyncMultiplier();

        return static_cast<float>(quarterNoteHz / multiplier);
    }

private:
    double sampleRate = 44100.0;
    double phase = 0.0;

    float rateHz = 1.0f;
    float amount = 0.0f;
    Shape shape = Shape::Sine;
    SyncDivision syncDivision = SyncDivision::Off;
    double hostTempo = 120.0;

    bool quantizeEnabled = false;
    float scaleDegreeInterval = 100.0f;  // Hz per scale degree

    // Current raw waveform value (-1 to +1)
    float currentRawValue = 0.0f;

    // Random state
    float currentSHValue = 0.0f;
    int lastPhaseQuadrant = 0;

    // Random generator for S&H
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> dist{ -1.0f, 1.0f };

    // Get sync multiplier (in quarter notes)
    double getSyncMultiplier() const
    {
        switch (syncDivision)
        {
            case SyncDivision::Off:       return 1.0;    // Won't be used
            case SyncDivision::Div_4_1:   return 16.0;   // 4 bars
            case SyncDivision::Div_2_1:   return 8.0;    // 2 bars
            case SyncDivision::Div_1_1:   return 4.0;    // 1 bar
            case SyncDivision::Div_1_2:   return 2.0;    // Half note
            case SyncDivision::Div_1_4:   return 1.0;    // Quarter note
            case SyncDivision::Div_1_8:   return 0.5;    // Eighth
            case SyncDivision::Div_1_16:  return 0.25;   // Sixteenth
            case SyncDivision::Div_1_32:  return 0.125;  // 32nd
            default:                      return 1.0;
        }
    }

    // Generate waveform value from phase (0-1) -> (-1 to +1)
    float generateWaveform(float p)
    {
        switch (shape)
        {
            case Shape::Sine:
                // Sine wave
                return std::sin(p * 2.0f * 3.14159265359f);

            case Shape::Triangle:
                // Triangle: 0->0.25 = 0->1, 0.25->0.75 = 1->-1, 0.75->1 = -1->0
                if (p < 0.25f)
                    return p * 4.0f;
                else if (p < 0.75f)
                    return 1.0f - (p - 0.25f) * 4.0f;
                else
                    return -1.0f + (p - 0.75f) * 4.0f;

            case Shape::Saw:
                // Saw down: 1 at phase 0, -1 at phase 1
                return 1.0f - 2.0f * p;

            case Shape::InvSaw:
                // Saw up: -1 at phase 0, 1 at phase 1
                return -1.0f + 2.0f * p;

            case Shape::Random:
                return generateRandom(p);

            default:
                return 0.0f;
        }
    }

    // Random with instant jumps (no smoothing)
    float generateRandom(float p)
    {
        // Determine which quadrant we're in
        int quadrant = static_cast<int>(p * 4.0f);

        // Trigger new random value at start of each cycle (instant jump)
        if (quadrant == 0 && lastPhaseQuadrant == 3)
        {
            currentSHValue = dist(rng);
        }
        lastPhaseQuadrant = quadrant;

        return currentSHValue;
    }

    // Quantize modulation to scale degree intervals
    float quantizeToScaleDegrees(float hz)
    {
        if (scaleDegreeInterval <= 0.0f)
            return hz;

        // Round to nearest scale degree interval
        float degrees = hz / scaleDegreeInterval;
        float quantizedDegrees = std::round(degrees);

        return quantizedDegrees * scaleDegreeInterval;
    }
};

} // namespace fshift
