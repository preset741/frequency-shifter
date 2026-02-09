#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace fshift
{

/**
 * FeedbackDelay - Simple time-domain delay for frequency shift processing.
 *
 * Phase 3 Step 2: Basic delay only implementation.
 * Just a circular buffer with TIME and MIX controls.
 * No feedback, no damping, no tempo sync yet.
 *
 * Signal flow:
 *   shifter output -> write to buffer -> read delayed -> mix with output
 */
class FeedbackDelay
{
public:
    FeedbackDelay() = default;
    ~FeedbackDelay() = default;

    /**
     * Prepare the delay for processing.
     * @param sr Sample rate in Hz
     * @param maxDelayMs Maximum delay time in milliseconds (default 1000ms = 1 second)
     */
    void prepare(double sr, float maxDelayMs = 1000.0f)
    {
        sampleRate = sr;

        // Allocate delay buffer for max delay time (1 second)
        int maxDelaySamples = static_cast<int>(std::ceil(maxDelayMs * sampleRate / 1000.0));
        delayBuffer.resize(static_cast<size_t>(maxDelaySamples), 0.0f);

        writePos = 0;
    }

    /**
     * Reset the delay buffer.
     */
    void reset()
    {
        std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
        writePos = 0;
    }

    /**
     * Set delay time in milliseconds (10-1000 ms).
     */
    void setDelayTimeMs(float ms)
    {
        delayTimeMs = std::clamp(ms, 10.0f, 1000.0f);
    }

    /**
     * Set wet/dry mix (0-1). 0 = no delay audible, 1 = full delay level.
     * Note: This is an additive mix, not a crossfade.
     */
    void setMix(float m)
    {
        mix = std::clamp(m, 0.0f, 1.0f);
    }

    // Getters
    float getDelayTimeMs() const { return delayTimeMs; }
    float getMix() const { return mix; }

    /**
     * Write a sample to the delay buffer.
     * Call this AFTER the shifter has processed, passing its output.
     */
    void writeSample(float shifterOutput)
    {
        if (delayBuffer.empty())
            return;

        int bufferSize = static_cast<int>(delayBuffer.size());

        // Write to buffer
        delayBuffer[static_cast<size_t>(writePos)] = shifterOutput;

        // Advance write position
        writePos = (writePos + 1) % bufferSize;
    }

    /**
     * Read delayed sample from the buffer.
     * Call this AFTER writeSample to get the delayed signal.
     * Returns the delayed signal scaled by mix amount.
     */
    float readDelayedSample()
    {
        if (delayBuffer.empty())
            return 0.0f;

        int bufferSize = static_cast<int>(delayBuffer.size());

        // Calculate delay in samples
        int delaySamples = static_cast<int>(delayTimeMs * sampleRate / 1000.0f);
        delaySamples = std::clamp(delaySamples, 1, bufferSize - 1);

        // Calculate read position (writePos was already advanced, so we subtract from writePos)
        int readPos = (writePos - delaySamples + bufferSize) % bufferSize;

        // Read delayed signal
        float delayedSignal = delayBuffer[static_cast<size_t>(readPos)];

        // Apply mix
        return delayedSignal * mix;
    }

    // ======= STUB METHODS FOR COMPATIBILITY =======
    // These do nothing but allow existing code to compile
    // Will be implemented in later phases

    enum class SyncMode
    {
        Off = 0,
        ThirtySecond, Sixteenth, SixteenthDot,
        Eighth, EighthDot, Quarter, QuarterDot,
        Half, HalfDot, Whole,
        NumModes
    };

    static const char* getSyncModeName(SyncMode mode)
    {
        (void)mode;
        return "MS";
    }

    void setSyncMode(SyncMode mode) { (void)mode; }  // Stub - not implemented yet
    void setTempo(double bpm) { (void)bpm; }         // Stub - not implemented yet
    void setFeedback(float fb) { (void)fb; }         // Stub - not implemented yet
    void setDamping(float damp) { (void)damp; }      // Stub - not implemented yet

    // For compatibility with existing signal flow in processBlock
    float peekFeedbackSample() { return 0.0f; }  // No feedback yet - return 0
    float getDelayedOutput() { return readDelayedSample(); }

    static float softClip(float x) { return x; }  // Stub - pass through

private:
    double sampleRate = 44100.0;

    std::vector<float> delayBuffer;
    int writePos = 0;

    float delayTimeMs = 250.0f;  // Default 250ms
    float mix = 0.5f;            // Default 50% mix
};

} // namespace fshift
