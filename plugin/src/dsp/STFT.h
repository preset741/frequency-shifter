#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

// NOTE: <Accelerate/Accelerate.h> is intentionally NOT included here — it defines the
// legacy Carbon `Point`/`Component` types that clash with juce::Point in any TU that
// includes both. vDSP is used only inside STFT.cpp (which pulls in Accelerate there);
// the FFT setup is held here as an opaque void* and cast in the .cpp.

namespace fshift
{

/**
 * Window function types for STFT analysis/synthesis.
 */
enum class WindowType
{
    Hann,
    Hamming,
    Blackman
};

/**
 * STFT - Short-Time Fourier Transform implementation.
 *
 * Provides windowed FFT analysis and overlap-add synthesis for
 * time-frequency processing of audio signals.
 *
 * Based on the Python implementation in harmonic_shifter/core/stft.py
 */
class STFT
{
public:
    /**
     * Construct STFT processor.
     *
     * @param fftSize FFT window size (must be power of 2)
     * @param hopSize Hop size between frames in samples
     * @param windowType Window function type
     */
    STFT(int fftSize = 4096, int hopSize = 1024, WindowType windowType = WindowType::Hann);

    ~STFT();

    /**
     * Prepare the STFT processor for a given sample rate.
     */
    void prepare(double sampleRate);

    /**
     * Reset internal state.
     */
    void reset();

    /**
     * Perform forward STFT on an input frame.
     *
     * @param inputFrame Time-domain samples (fftSize samples)
     * @return Pair of (magnitude, phase) vectors
     */
    std::pair<std::vector<float>, std::vector<float>> forward(const std::vector<float>& inputFrame);

    /**
     * Perform inverse STFT to reconstruct time-domain signal.
     *
     * @param magnitude Magnitude spectrum
     * @param phase Phase spectrum in radians
     * @return Time-domain frame (fftSize samples)
     */
    std::vector<float> inverse(const std::vector<float>& magnitude, const std::vector<float>& phase);

    /**
     * Get frequency values for each FFT bin.
     *
     * @return Vector of frequency values in Hz
     */
    std::vector<float> getFrequencyBins() const;

    // Getters
    int getFFTSize() const { return fftSize; }
    int getHopSize() const { return hopSize; }
    int getNumBins() const { return numBins; }
    double getSampleRate() const { return sampleRate; }
    float getBinResolution() const { return binResolution; }

private:
    /**
     * Create window function.
     */
    void createWindow();

    /**
     * Perform FFT using Cooley-Tukey algorithm.
     */
    void fft(std::vector<std::complex<float>>& x);

    /**
     * Perform inverse FFT.
     */
    void ifft(std::vector<std::complex<float>>& x);

    /**
     * Bit-reversal permutation.
     */
    void bitReverse(std::vector<std::complex<float>>& x);

    int fftSize;
    int hopSize;
    int numBins;
    WindowType windowType;
    double sampleRate;
    float binResolution;

    std::vector<float> window;
    std::vector<float> windowSquared;
    // Weighted-overlap-add (WOLA) synthesis gain. The window is applied on BOTH analysis
    // and synthesis, so a plain overlap-add sums w^2 and leaves a constant gain of
    // (sum(w^2) / hop). Dividing the synthesis frame by that constant makes an
    // identity forward->inverse->overlap-add round trip unity gain (was +3.52 dB for
    // Hann at 75% overlap). Computed once in createWindow().
    float synthesisScale = 1.0f;
    std::vector<std::complex<float>> fftBuffer;

    // Pre-computed twiddle factors for FFT (scalar fallback path only)
    std::vector<std::complex<float>> twiddleFactors;

#if defined(__APPLE__)
    // Apple vDSP FFT state. Held opaquely (void*) so this header never pulls in
    // <Accelerate/Accelerate.h> (whose Carbon Point/Component types clash with JUCE).
    void* vdspSetup = nullptr;     // FFTSetup, created in ctor, destroyed in dtor
    int vdspLog2 = 0;
    std::vector<float> vdspReal;   // split-complex scratch (size fftSize)
    std::vector<float> vdspImag;
#endif
};

} // namespace fshift
