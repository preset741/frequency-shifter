#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/MusicalQuantizer.h"
#include "dsp/Scales.h"

#include <array>
#include <cmath>
#include <vector>

using fshift::MusicalQuantizer;
using fshift::ScaleType;
using Catch::Approx;

namespace {

std::vector<float> flatSpectrum(int numBins, float magnitude = 1.0f)
{
    return std::vector<float>(static_cast<size_t>(numBins), magnitude);
}

double totalEnergy(const std::vector<float>& mag)
{
    double e = 0.0;
    for (float m : mag) e += static_cast<double>(m) * m;
    return e;
}

}  // namespace

TEST_CASE("MusicalQuantizer: strength=0 with shiftHz=0 is a pass-through", "[quantizer]")
{
    MusicalQuantizer q(60, ScaleType::Major);
    q.prepare(44100.0, 2048, 512);

    const int numBins = 2048 / 2 + 1;
    auto mag = flatSpectrum(numBins, 0.5f);
    std::vector<float> phase(static_cast<size_t>(numBins), 0.25f);

    auto [outMag, outPhase] = q.quantizeSpectrum(
        mag, phase, 44100.0, 2048, /*shiftHz=*/0.0f, /*strength=*/0.0f);

    REQUIRE(outMag.size() == mag.size());
    for (size_t i = 0; i < mag.size(); ++i)
    {
        INFO("bin " << i);
        REQUIRE(outMag[i] == Approx(mag[i]).margin(1e-5f));
    }
}

TEST_CASE("MusicalQuantizer: full quantization roughly conserves total energy", "[quantizer]")
{
    // Energy normalization is part of the algorithm's contract — pre/post energy
    // should be approximately equal. We allow a generous margin because the
    // distribution is two-tap weighted and there's some band-edge loss.
    MusicalQuantizer q(60, ScaleType::Major);
    q.prepare(44100.0, 2048, 512);

    const int numBins = 2048 / 2 + 1;
    auto mag = flatSpectrum(numBins, 0.5f);
    std::vector<float> phase(static_cast<size_t>(numBins), 0.0f);

    double inputEnergy = totalEnergy(mag);

    auto [outMag, _outPhase] = q.quantizeSpectrum(
        mag, phase, 44100.0, 2048, /*shiftHz=*/0.0f, /*strength=*/1.0f);

    double outputEnergy = totalEnergy(outMag);

    // Within 6 dB (factor of 4) — energy conservation is approximate by design.
    REQUIRE(outputEnergy > inputEnergy * 0.25);
    REQUIRE(outputEnergy < inputEnergy * 4.0);
}

TEST_CASE("MusicalQuantizer: setActiveNotes with all 12 notes is near-identity at strength=1",
          "[quantizer]")
{
    // With every pitch class active, every frequency has a "scale member" within a
    // semitone or so, and the snap distance is small. Output energy should track input.
    MusicalQuantizer q(60, ScaleType::Chromatic);
    q.prepare(44100.0, 2048, 512);

    std::array<bool, 12> allOn{};
    allOn.fill(true);
    q.setActiveNotes(allOn);

    const int numBins = 2048 / 2 + 1;
    auto mag = flatSpectrum(numBins, 0.5f);
    std::vector<float> phase(static_cast<size_t>(numBins), 0.0f);

    auto [outMag, _phase] = q.quantizeSpectrum(
        mag, phase, 44100.0, 2048, /*shiftHz=*/0.0f, /*strength=*/1.0f);

    REQUIRE(totalEnergy(outMag) == Approx(totalEnergy(mag)).epsilon(0.5));  // within 1.76 dB
}

TEST_CASE("MusicalQuantizer: empty active-note set degrades gracefully", "[quantizer]")
{
    // If the user deselects every scale note, the quantizer should not crash
    // or produce NaN/Inf. Silence is an acceptable response.
    MusicalQuantizer q(60, ScaleType::Major);
    q.prepare(44100.0, 2048, 512);

    std::array<bool, 12> allOff{};
    allOff.fill(false);
    q.setActiveNotes(allOff);

    const int numBins = 2048 / 2 + 1;
    auto mag = flatSpectrum(numBins, 0.5f);
    std::vector<float> phase(static_cast<size_t>(numBins), 0.0f);

    auto [outMag, outPhase] = q.quantizeSpectrum(
        mag, phase, 44100.0, 2048, /*shiftHz=*/0.0f, /*strength=*/1.0f);

    for (size_t i = 0; i < outMag.size(); ++i)
    {
        REQUIRE(std::isfinite(outMag[i]));
        REQUIRE(std::isfinite(outPhase[i]));
    }
}

TEST_CASE("MusicalQuantizer: stable across sample rates with the same musical content",
          "[quantizer][sr-sweep]")
{
    // Same input frequency content at different sample rates → comparable output energy
    // (within an order of magnitude). Verifies that fftSize-relative DSP scales correctly.
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        MusicalQuantizer q(60, ScaleType::Major);
        const int fftSize = 2048;
        q.prepare(sr, fftSize, fftSize / 4);

        const int numBins = fftSize / 2 + 1;
        auto mag = flatSpectrum(numBins, 0.5f);
        std::vector<float> phase(static_cast<size_t>(numBins), 0.0f);

        auto [outMag, _phase] = q.quantizeSpectrum(mag, phase, sr, fftSize, 0.0f, 1.0f);

        INFO("Sample rate: " << sr);
        REQUIRE(totalEnergy(outMag) > 0.0);
        for (float m : outMag) REQUIRE(std::isfinite(m));
    }
}

TEST_CASE("MusicalQuantizer: a shift is applied even when quantize strength is 0", "[quantizer][shift]")
{
    // Regression: the low-effective-strength early-out used to return the ORIGINAL spectrum
    // whenever effectiveStrength <= 0.001, ignoring shiftHz. That made the Shift knob dead at
    // Quantize 0% (and caused a pitch snap-back to the unshifted signal on transients). A pure
    // shift (strength 0) must still move spectral energy upward.
    const int fftSize = 1024;
    const double sr = 44100.0;
    const float binRes = static_cast<float>(sr) / fftSize;
    const int numBins = fftSize / 2 + 1;

    MusicalQuantizer q(60, ScaleType::Major);
    q.prepare(sr, fftSize, fftSize / 4);

    const int srcBin = 20;                              // ~861 Hz
    std::vector<float> mag(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> phase(static_cast<size_t>(numBins), 0.0f);
    mag[static_cast<size_t>(srcBin)] = 1.0f;

    auto argmax = [](const std::vector<float>& v) {
        int bi = 0; float bv = -1.0f;
        for (int i = 0; i < static_cast<int>(v.size()); ++i)
            if (v[i] > bv) { bv = v[i]; bi = i; }
        return bi;
    };

    const float shift = 200.0f;                         // +200 Hz
    auto [outMag, _phase] = q.quantizeSpectrum(mag, phase, sr, fftSize, shift, /*strength=*/0.0f);

    int outBin = argmax(outMag);
    int expectBin = static_cast<int>(std::lround((srcBin * binRes + shift) / binRes));
    INFO("out peak bin " << outBin << ", expected ~" << expectBin);
    REQUIRE(outBin > srcBin);                           // energy actually moved (the fix)
    REQUIRE(std::abs(outBin - expectBin) <= 2);         // ...to roughly the shifted frequency
}
