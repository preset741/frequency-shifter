#include "STFT.h"
#include <stdexcept>
#include <numbers>

namespace fshift
{

STFT::STFT(int fftSize, int hopSize, WindowType windowType)
    : fftSize(fftSize),
      hopSize(hopSize),
      numBins(fftSize / 2 + 1),
      windowType(windowType),
      sampleRate(44100.0),
      binResolution(0.0f)
{
    // Validate FFT size is power of 2
    if (fftSize <= 0 || (fftSize & (fftSize - 1)) != 0)
    {
        throw std::invalid_argument("FFT size must be a power of 2");
    }

    if (hopSize <= 0 || hopSize > fftSize)
    {
        throw std::invalid_argument("Hop size must be positive and <= FFT size");
    }

    createWindow();

    // Allocate buffers
    fftBuffer.resize(fftSize);

    // Pre-compute twiddle factors for FFT
    twiddleFactors.resize(fftSize / 2);
    for (int i = 0; i < fftSize / 2; ++i)
    {
        float angle = -2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(fftSize);
        twiddleFactors[i] = std::complex<float>(std::cos(angle), std::sin(angle));
    }
}

void STFT::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    binResolution = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
}

void STFT::reset()
{
    std::fill(fftBuffer.begin(), fftBuffer.end(), std::complex<float>(0.0f, 0.0f));
}

void STFT::createWindow()
{
    window.resize(fftSize);
    windowSquared.resize(fftSize);

    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i);
        float N = static_cast<float>(fftSize);

        switch (windowType)
        {
            case WindowType::Hann:
                window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * n / N));
                break;

            case WindowType::Hamming:
                window[i] = 0.54f - 0.46f * std::cos(2.0f * std::numbers::pi_v<float> * n / N);
                break;

            case WindowType::Blackman:
                window[i] = 0.42f
                            - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * n / N)
                            + 0.08f * std::cos(4.0f * std::numbers::pi_v<float> * n / N);
                break;
        }

        windowSquared[i] = window[i] * window[i];
    }
}

std::pair<std::vector<float>, std::vector<float>> STFT::forward(const std::vector<float>& inputFrame)
{
    if (static_cast<int>(inputFrame.size()) != fftSize)
    {
        throw std::invalid_argument("Input frame size must match FFT size");
    }

    // Apply window and copy to FFT buffer
    for (int i = 0; i < fftSize; ++i)
    {
        fftBuffer[i] = std::complex<float>(inputFrame[i] * window[i], 0.0f);
    }

    // Perform FFT
    fft(fftBuffer);

    // Extract magnitude and phase (positive frequencies only)
    std::vector<float> magnitude(numBins);
    std::vector<float> phase(numBins);

    for (int i = 0; i < numBins; ++i)
    {
        magnitude[i] = std::abs(fftBuffer[i]);
        phase[i] = std::arg(fftBuffer[i]);
    }

    return { magnitude, phase };
}

std::vector<float> STFT::inverse(const std::vector<float>& magnitude, const std::vector<float>& phase)
{
    if (static_cast<int>(magnitude.size()) != numBins || static_cast<int>(phase.size()) != numBins)
    {
        throw std::invalid_argument("Magnitude and phase must have numBins elements");
    }

    // Reconstruct complex spectrum
    for (int i = 0; i < numBins; ++i)
    {
        fftBuffer[i] = std::polar(magnitude[i], phase[i]);
    }

    // Mirror for negative frequencies (conjugate symmetry for real signal)
    // Skip bin 0 (DC) and bin numBins-1 (Nyquist) as they don't need mirroring
    for (int i = 1; i < numBins - 1; ++i)
    {
        fftBuffer[fftSize - i] = std::conj(fftBuffer[i]);
    }

    // Perform inverse FFT
    ifft(fftBuffer);

    // Extract real part and apply window
    std::vector<float> outputFrame(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        outputFrame[i] = fftBuffer[i].real() * window[i];
    }

    return outputFrame;
}

std::vector<float> STFT::getFrequencyBins() const
{
    std::vector<float> frequencies(numBins);
    for (int i = 0; i < numBins; ++i)
    {
        frequencies[i] = static_cast<float>(i) * binResolution;
    }
    return frequencies;
}

void STFT::bitReverse(std::vector<std::complex<float>>& x)
{
    int n = static_cast<int>(x.size());
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;

    for (int i = 0; i < n; ++i)
    {
        int j = 0;
        for (int b = 0; b < bits; ++b)
        {
            j |= ((i >> b) & 1) << (bits - 1 - b);
        }
        if (j > i)
        {
            std::swap(x[i], x[j]);
        }
    }
}

void STFT::fft(std::vector<std::complex<float>>& x)
{
    int n = static_cast<int>(x.size());

    // Bit-reversal permutation
    bitReverse(x);

    // Cooley-Tukey iterative FFT
    for (int size = 2; size <= n; size *= 2)
    {
        int halfSize = size / 2;
        int step = n / size;

        for (int i = 0; i < n; i += size)
        {
            for (int j = 0; j < halfSize; ++j)
            {
                auto& even = x[i + j];
                auto& odd = x[i + j + halfSize];
                auto t = twiddleFactors[j * step] * odd;
                odd = even - t;
                even = even + t;
            }
        }
    }
}

void STFT::ifft(std::vector<std::complex<float>>& x)
{
    int n = static_cast<int>(x.size());

    // Conjugate
    for (auto& val : x)
    {
        val = std::conj(val);
    }

    // Forward FFT
    fft(x);

    // Conjugate and scale
    float scale = 1.0f / static_cast<float>(n);
    for (auto& val : x)
    {
        val = std::conj(val) * scale;
    }
}

} // namespace fshift
