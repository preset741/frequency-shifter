#pragma once

#include <cmath>
#include <numbers>
#include <random>
#include <array>

namespace fshift
{

/**
 * DriftModulator - Generates smooth modulation for pitch drift effect.
 *
 * Supports two modes:
 * - LFO: Sine or triangle wave oscillation
 * - Perlin: Smooth pseudo-random noise (organic drift)
 *
 * Each frequency bin can have independent modulation phase for natural movement.
 */
class DriftModulator
{
public:
    enum class Mode
    {
        LFO = 0,
        Perlin = 1
    };

    enum class LFOShape
    {
        Sine = 0,
        Triangle = 1
    };

    DriftModulator() = default;

    /**
     * Prepare the modulator for processing.
     *
     * @param sampleRate Audio sample rate
     * @param numBins Number of frequency bins to modulate
     */
    void prepare(double sampleRate, int numBins)
    {
        this->sampleRate = sampleRate;
        this->numBins = numBins;

        // Initialize per-bin phase offsets for variety
        binPhaseOffsets.resize(static_cast<size_t>(numBins));
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (size_t i = 0; i < binPhaseOffsets.size(); ++i)
        {
            binPhaseOffsets[i] = dist(gen);
        }

        // Initialize Perlin noise state
        initPerlin();
    }

    /**
     * Reset modulator state.
     */
    void reset()
    {
        phase = 0.0;
        perlinTime = 0.0;
    }

    /**
     * Advance the modulator by one hop (FFT frame).
     *
     * @param hopSize Number of samples per hop
     */
    void advanceFrame(int hopSize)
    {
        double timeIncrement = static_cast<double>(hopSize) / sampleRate;

        // Advance LFO phase
        phase += rate * timeIncrement;
        if (phase >= 1.0)
            phase -= 1.0;

        // Advance Perlin time
        perlinTime += rate * timeIncrement;
    }

    /**
     * Get drift amount for a specific frequency bin.
     * Returns value in range [-1, 1] multiplied by depth.
     *
     * @param binIndex Frequency bin index
     * @return Drift amount in cents (scaled by depth parameter)
     */
    float getDrift(int binIndex) const
    {
        if (depth <= 0.0f || binIndex < 0 || binIndex >= numBins)
            return 0.0f;

        float modValue = 0.0f;
        float binPhase = binPhaseOffsets[static_cast<size_t>(binIndex)];

        if (mode == Mode::LFO)
        {
            modValue = computeLFO(phase + binPhase * phaseSpread);
        }
        else // Perlin
        {
            modValue = computePerlin(binIndex, binPhase);
        }

        // Scale by depth (in cents, max ±50 cents = half semitone)
        return modValue * depth * 50.0f;
    }

    // Setters
    void setMode(Mode newMode) { mode = newMode; }
    void setLFOShape(LFOShape newShape) { lfoShape = newShape; }
    void setRate(float newRate) { rate = std::clamp(newRate, 0.01f, 20.0f); }
    void setDepth(float newDepth) { depth = std::clamp(newDepth, 0.0f, 1.0f); }
    void setPhaseSpread(float newSpread) { phaseSpread = std::clamp(newSpread, 0.0f, 1.0f); }

    // Perlin-specific parameters
    void setPerlinOctaves(int octaves) { perlinOctaves = std::clamp(octaves, 1, 4); }
    void setPerlinLacunarity(float lac) { perlinLacunarity = std::clamp(lac, 1.0f, 4.0f); }
    void setPerlinPersistence(float pers) { perlinPersistence = std::clamp(pers, 0.0f, 1.0f); }

    // Getters
    Mode getMode() const { return mode; }
    LFOShape getLFOShape() const { return lfoShape; }
    float getRate() const { return rate; }
    float getDepth() const { return depth; }

private:
    // LFO computation
    float computeLFO(double p) const
    {
        // Wrap phase to [0, 1)
        p = p - std::floor(p);

        if (lfoShape == LFOShape::Sine)
        {
            return static_cast<float>(std::sin(2.0 * std::numbers::pi * p));
        }
        else // Triangle
        {
            // Triangle wave: rises 0->1 in first half, falls 1->-1 in second half
            if (p < 0.25)
                return static_cast<float>(p * 4.0);
            else if (p < 0.75)
                return static_cast<float>(1.0 - (p - 0.25) * 4.0);
            else
                return static_cast<float>(-1.0 + (p - 0.75) * 4.0);
        }
    }

    // Perlin noise implementation (1D)
    void initPerlin()
    {
        // Initialize permutation table for Perlin noise
        std::random_device rd;
        std::mt19937 gen(42); // Fixed seed for reproducibility

        for (int i = 0; i < 256; ++i)
        {
            perm[i] = i;
        }

        // Shuffle
        for (int i = 255; i > 0; --i)
        {
            std::uniform_int_distribution<int> dist(0, i);
            int j = dist(gen);
            std::swap(perm[i], perm[j]);
        }

        // Duplicate for overflow handling
        for (int i = 0; i < 256; ++i)
        {
            perm[256 + i] = perm[i];
        }
    }

    float fade(float t) const
    {
        // Smoothstep: 6t^5 - 15t^4 + 10t^3
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float grad(int hash, float x) const
    {
        // 1D gradient
        return (hash & 1) ? x : -x;
    }

    float noise1D(float x) const
    {
        int xi = static_cast<int>(std::floor(x)) & 255;
        float xf = x - std::floor(x);
        float u = fade(xf);

        int aa = perm[xi];
        int ab = perm[xi + 1];

        float g1 = grad(perm[aa], xf);
        float g2 = grad(perm[ab], xf - 1.0f);

        return g1 + u * (g2 - g1);
    }

    float computePerlin(int binIndex, float binPhase) const
    {
        // Use bin index and phase to create unique noise for each bin
        float x = static_cast<float>(perlinTime) + binPhase * 10.0f + static_cast<float>(binIndex) * 0.1f;

        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < perlinOctaves; ++i)
        {
            total += noise1D(x * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= perlinPersistence;
            frequency *= perlinLacunarity;
        }

        // Normalize to [-1, 1]
        return total / maxValue;
    }

    // Parameters
    Mode mode = Mode::LFO;
    LFOShape lfoShape = LFOShape::Sine;
    float rate = 1.0f;           // Hz (cycles per second)
    float depth = 0.0f;          // 0-1 (0 = no drift, 1 = max ±50 cents)
    float phaseSpread = 0.5f;    // How much bins differ in phase (0 = sync, 1 = random)

    // Perlin parameters
    int perlinOctaves = 2;       // Number of noise layers
    float perlinLacunarity = 2.0f;   // Frequency multiplier per octave
    float perlinPersistence = 0.5f;  // Amplitude multiplier per octave

    // State
    double sampleRate = 44100.0;
    int numBins = 2048;
    double phase = 0.0;          // LFO phase [0, 1)
    double perlinTime = 0.0;     // Perlin noise time coordinate

    // Per-bin randomization
    std::vector<float> binPhaseOffsets;

    // Perlin permutation table
    std::array<int, 512> perm{};
};

} // namespace fshift
