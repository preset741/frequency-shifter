# Feedback Delay Architecture

## Overview

The Frequency Shifter plugin includes a feedback delay that creates cascading pitch shifts. Unlike a standard delay where the delayed signal is simply mixed with the dry signal, this feedback delay routes the delayed signal back through the frequency shifter input, causing each echo to be pitch-shifted again.

## Signal Flow

```
                                    ┌─────────────────────────────────────────┐
                                    │                                         │
                                    ▼                                         │
┌─────────┐   ┌─────────────┐   ┌──────────┐   ┌────────────┐   ┌─────────┐  │
│  Input  │ + │  Feedback   │ → │   STFT   │ → │  Freq      │ → │ Inverse │  │
│  Sample │   │  Sample     │   │  Forward │   │  Shifter   │   │  STFT   │  │
└─────────┘   └─────────────┘   └──────────┘   │  + Quant   │   └────┬────┘  │
                    ▲                          └────────────┘        │       │
                    │                                                │       │
                    │           ┌────────────────────────────────────┘       │
                    │           │                                            │
                    │           ▼                                            │
                    │    ┌─────────────┐                                     │
                    │    │  Shifted    │──────────────────────────────────────┘
                    │    │  Output     │
                    │    └──────┬──────┘
                    │           │
                    │           ▼
                    │    ┌─────────────┐
                    │    │   Delay     │
                    │    │   Buffer    │
                    │    └──────┬──────┘
                    │           │
                    │           ▼
                    │    ┌─────────────┐
                    │    │  HF Damp    │
                    │    │  Filter     │
                    │    └──────┬──────┘
                    │           │
                    │           ▼
                    │    ┌─────────────┐
                    └────│  Soft Clip  │
                         │  × Feedback │
                         └─────────────┘
```

### Cascading Effect

With a +100Hz shift and 50% feedback:
- **Echo 1**: Original pitch + 100Hz
- **Echo 2**: Original pitch + 200Hz
- **Echo 3**: Original pitch + 300Hz
- **Echo 4**: Original pitch + 400Hz
- ... and so on, with each echo quieter due to feedback < 100%

## Implementation Details

### Files

| File | Purpose |
|------|---------|
| `dsp/FeedbackDelay.h` | Core delay line implementation |
| `PluginProcessor.h/cpp` | Parameter handling and signal routing |
| `PluginEditor.h/cpp` | UI controls |

### Key Classes

#### `fshift::FeedbackDelay`

The delay line class with these key methods:

```cpp
// Initialization
void prepare(double sampleRate, float maxDelayMs);
void reset();

// Configuration
void setDelayTimeMs(float ms);        // 10-1000ms
void setSyncMode(SyncMode mode);      // MS or tempo-synced
void setTempo(double bpm);            // From DAW host
void setFeedback(float fb);           // 0-1
void setMix(float m);                 // 0-1
void setDamping(float damp);          // 0-1 (HF rolloff)
void setLatencyCompensation(int samples);

// Processing (two-step for feedback loop)
float peekFeedbackSample();           // Get feedback without advancing
void writeSample(float output);       // Write shifted output to buffer
float getDelayedOutput();             // Get delayed signal for mix
```

### Processing Order

The feedback loop requires careful ordering because:
1. We need the feedback sample BEFORE the shifter processes (to add to input)
2. We can only write to the delay AFTER the shifter produces output

**Per-sample processing in `processBlock()`:**

```cpp
// 1. Peek at feedback (doesn't advance state)
float feedbackSample = fbDelay.peekFeedbackSample() * feedbackAmount;
feedbackSample = FeedbackDelay::softClip(feedbackSample);

// 2. Add feedback to input
float inputSample = channelData[i] + feedbackSample;

// 3. Process through STFT → Shifter → Quantizer → iSTFT
// ... (spectral processing happens here) ...
float shiftedOutput = ...;

// 4. Write output to delay buffer
fbDelay.writeSample(shiftedOutput);
float delayedOutput = fbDelay.getDelayedOutput();

// 5. Mix for final output
channelData[i] = shiftedOutput + delayedOutput * mixAmount;
```

## Parameters

### TIME (10-1000ms)
- Logarithmic scale for fine control at short times
- Only used when SYNC mode is "MS"

### SYNC Mode
| Mode | Multiplier (relative to quarter note) |
|------|---------------------------------------|
| MS | Uses TIME parameter |
| 1/32 | 0.125 |
| 1/16 | 0.25 |
| 1/16D | 0.375 (dotted) |
| 1/8 | 0.5 |
| 1/8D | 0.75 (dotted) |
| 1/4 | 1.0 |
| 1/4D | 1.5 (dotted) |
| 1/2 | 2.0 |
| 1/2D | 3.0 (dotted) |
| 1/1 | 4.0 |

**Tempo sync calculation:**
```cpp
double quarterNoteMs = 60000.0 / hostTempo;
float delayMs = quarterNoteMs * divisionMultiplier;
```

### FEEDBACK (0-100%)
Controls how much of the delayed signal feeds back to the shifter input.
- 0% = No feedback, single echo
- 50% = Each echo is half as loud
- 100% = Infinite sustain (limited by soft clipping)

### MIX (0-100%)
Controls how much delayed signal is added to the output.
- 0% = Dry shifted signal only
- 50% = Equal blend
- 100% = Maximum delay prominence

**Note:** This is additive mixing, not crossfade. The shifted signal is always present.

## Safety Features

### Soft Clipping
Prevents runaway feedback using tanh-style saturation:

```cpp
static float softClip(float x) {
    if (std::abs(x) < 0.8f)
        return x;
    return std::tanh(x);
}
```

### High-Frequency Damping
One-pole lowpass filter on the feedback path simulates natural echo decay:

```cpp
// Default 30% damping
dampingCoeff = damping * 0.9f;

// Applied per sample
filterState = filterState * dampingCoeff + delayed * (1.0f - dampingCoeff);
```

### Latency Compensation
The delay time accounts for FFT processing latency so tempo-synced delays land on beat:

```cpp
float getEffectiveDelayMs() const {
    float requested = getRequestedDelayMs();
    float compensated = requested - latencyCompensationMs;
    return std::max(1.0f, compensated);
}
```

The plugin reports 4096 samples fixed latency. Delay compensation is set to this value:
```cpp
feedbackDelays[ch].setLatencyCompensation(FIXED_LATENCY_SAMPLES); // 4096
```

## Stereo Handling

Each channel has its own independent `FeedbackDelay` instance:

```cpp
std::array<fshift::FeedbackDelay, MAX_CHANNELS> feedbackDelays;
```

This maintains stereo separation - left channel feedback only affects left channel, etc.

## Memory Layout

The delay buffer is a simple circular buffer:

```cpp
std::vector<float> delayBuffer;  // Size = maxDelayMs * sampleRate / 1000
int writePos = 0;                // Current write position
```

**Linear interpolation** is used for fractional delay times to allow smooth time changes:

```cpp
float frac = delaySamples - std::floor(delaySamples);
int readPos2 = (readPos + 1) % bufferSize;
float delayed = delayBuffer[readPos] * (1.0f - frac)
              + delayBuffer[readPos2] * frac;
```

## Timing Diagram

```
Sample:     0    1    2    3    4    5    ...
            │    │    │    │    │    │
Input:      A    B    C    D    E    F    ...
            │    │    │    │    │    │
            ▼    ▼    ▼    ▼    ▼    ▼
        ┌───────────────────────────────────┐
        │  Add feedback from delay buffer   │
        └───────────────────────────────────┘
            │    │    │    │    │    │
            ▼    ▼    ▼    ▼    ▼    ▼
        ┌───────────────────────────────────┐
        │     STFT + Shift + Quantize       │
        │     (buffered, latency = 4096)    │
        └───────────────────────────────────┘
            │    │    │    │    │    │
            ▼    ▼    ▼    ▼    ▼    ▼
Output: [latency]  A'   B'   C'   D'   ...
                   │    │    │    │
                   ▼    ▼    ▼    ▼
        ┌───────────────────────────────────┐
        │     Write to delay buffer         │
        └───────────────────────────────────┘
                   │    │    │    │
           [delay time passes]
                   │    │    │    │
                   └────┴────┴────┴──► Feedback to input
```

## Integration Points

### In `PluginProcessor.cpp`:

**Constructor** - Add parameter listeners:
```cpp
parameters.addParameterListener(PARAM_DELAY_TIME, this);
parameters.addParameterListener(PARAM_DELAY_SYNC, this);
parameters.addParameterListener(PARAM_DELAY_FEEDBACK, this);
parameters.addParameterListener(PARAM_DELAY_MIX, this);
```

**`reinitializeDsp()`** - Initialize delay buffers:
```cpp
for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
    feedbackDelays[ch].prepare(currentSampleRate, 1000.0f);
    feedbackDelays[ch].reset();
    feedbackDelays[ch].setLatencyCompensation(FIXED_LATENCY_SAMPLES);
}
```

**`processBlock()`** - Update tempo and process:
```cpp
// Get host tempo
if (auto bpm = posInfo->getBpm()) {
    for (auto& delay : feedbackDelays)
        delay.setTempo(*bpm);
}
```

### In `PluginEditor.cpp`:

UI controls are positioned in the "FEEDBACK DELAY" panel at the bottom of the window.

---

*Document version: v20 FeedbackDelay*
*Last updated: January 2026*
