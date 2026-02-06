#pragma once

#include <cmath>
#include <array>

namespace fshift {

/**
 * HilbertShifter - Classic SSB/Hilbert frequency shifter with double-precision processing.
 *
 * Uses allpass filter networks to create quadrature signals (I/Q with 90° phase difference),
 * then applies single sideband modulation for frequency shifting.
 *
 * v95: Upgraded to double precision for allpass filter states and internal processing.
 * This prevents quantization error accumulation in deep feedback loops, which was causing
 * the "digital distortion" artifacts. Double precision maintains phase accuracy through
 * hundreds of feedback iterations.
 *
 * Advantages over spectral methods:
 * - Zero latency (aside from filter group delay ~10-20 samples)
 * - No smearing/time domain artifacts
 * - Simple and CPU efficient
 *
 * Limitations:
 * - Cannot preserve harmonic relationships (inharmonic shifting)
 * - Best for subtle shifts or creative effects
 *
 * Implementation uses two parallel 6th-order allpass chains with coefficients
 * designed to maintain ~90° phase difference across the audio spectrum (20Hz-20kHz).
 */
class HilbertShifter
{
public:
    HilbertShifter() = default;

    void prepare(double sr)
    {
        sampleRate = sr;
        reset();
    }

    void reset()
    {
        // Reset allpass filter states for all channels (double precision)
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        {
            for (auto& state : allpassStatesI[ch])
                state = 0.0;
            for (auto& state : allpassStatesQ[ch])
                state = 0.0;
        }

        // Reset oscillator
        oscPhase = 0.0;
    }

    /**
     * Set the frequency shift amount in Hz.
     * Positive values shift up, negative values shift down.
     */
    void setShiftHz(float hz)
    {
        shiftHz = hz;
    }

    /**
     * Process a single sample and return the frequency-shifted output.
     * Uses default channel 0 for backwards compatibility.
     */
    float process(float input)
    {
        return process(input, 0);
    }

    /**
     * Process a single sample for a specific channel.
     * @param input The input sample
     * @param channel The channel index (0 = left, 1 = right)
     * @return The frequency-shifted output
     */
    float process(float input, int channel)
    {
        // Note: The channel parameter is ignored since each HilbertShifter instance
        // is dedicated to a single audio channel. We always use internal index 0.
        (void)channel;  // Suppress unused parameter warning

        // Generate quadrature signals using Hilbert transform (allpass networks)
        // Uses double precision internally for accurate feedback loops
        double I = processAllpassChainI(static_cast<double>(input), 0);
        double Q = processAllpassChainQ(static_cast<double>(input), 0);

        // Generate quadrature oscillator signals (already double precision)
        double cosOsc = std::cos(oscPhase);
        double sinOsc = std::sin(oscPhase);

        // Single sideband modulation (double precision)
        // Upper sideband (shift up): I * cos - Q * sin
        // Lower sideband (shift down): I * cos + Q * sin
        double output;
        if (shiftHz >= 0.0f)
            output = I * cosOsc - Q * sinOsc;  // Upper sideband
        else
            output = I * cosOsc + Q * sinOsc;  // Lower sideband

        // Advance oscillator phase using absolute frequency
        // Note: Each HilbertShifter instance handles one audio channel,
        // so we always advance the oscillator regardless of channel parameter
        double phaseIncrement = 2.0 * M_PI * std::abs(shiftHz) / sampleRate;
        oscPhase += phaseIncrement;

        // Wrap phase to prevent numerical issues
        while (oscPhase >= 2.0 * M_PI)
            oscPhase -= 2.0 * M_PI;
        while (oscPhase < 0.0)
            oscPhase += 2.0 * M_PI;

        return static_cast<float>(output);
    }

    /**
     * Get the current oscillator phase (0 to 2π).
     * Useful for visualization.
     */
    double getOscillatorPhase() const { return oscPhase; }

private:
    double sampleRate = 44100.0;
    float shiftHz = 0.0f;
    double oscPhase = 0.0;

    // Allpass filter coefficients for Hilbert transform (stored as double for precision)
    // These coefficients are designed to create ~90° phase difference
    // between the I and Q outputs across 20Hz-20kHz at 44.1kHz sample rate.
    // Based on Olli Niemitalo's Hilbert transformer design.

    // Chain I coefficients (phase reference)
    static constexpr std::array<double, 6> coeffsI = {
        0.4021921162426,
        0.8561710882420,
        0.9722909545651,
        0.9952884791278,
        0.9990657381831,
        0.9998766533010
    };

    // Chain Q coefficients (90° shifted)
    static constexpr std::array<double, 6> coeffsQ = {
        0.1684919243525,
        0.7024051466406,
        0.9351665954634,
        0.9862259517082,
        0.9979710606470,
        0.9997089053332
    };

    // Allpass filter states - DOUBLE PRECISION for accurate feedback loops
    // This prevents quantization error accumulation that causes distortion
    static constexpr int MAX_CHANNELS = 2;
    std::array<std::array<double, 6>, MAX_CHANNELS> allpassStatesI = {};
    std::array<std::array<double, 6>, MAX_CHANNELS> allpassStatesQ = {};

    /**
     * Process through the I-channel allpass chain for a specific channel.
     * First-order allpass transfer function: H(z) = (a + z^-1) / (1 + a*z^-1)
     * Direct form: y[n] = a * x[n] + state; state = x[n] - a * y[n]
     *
     * Uses double precision throughout to prevent quantization errors
     * from accumulating in deep feedback loops.
     */
    double processAllpassChainI(double input, int channel)
    {
        double x = input;
        for (size_t i = 0; i < coeffsI.size(); ++i)
        {
            double a = coeffsI[i];
            double output = a * x + allpassStatesI[channel][i];
            allpassStatesI[channel][i] = x - a * output;
            x = output;
        }
        return x;
    }

    /**
     * Process through the Q-channel allpass chain for a specific channel.
     * Uses double precision throughout.
     */
    double processAllpassChainQ(double input, int channel)
    {
        double x = input;
        for (size_t i = 0; i < coeffsQ.size(); ++i)
        {
            double a = coeffsQ[i];
            double output = a * x + allpassStatesQ[channel][i];
            allpassStatesQ[channel][i] = x - a * output;
            x = output;
        }
        return x;
    }
};

} // namespace fshift
