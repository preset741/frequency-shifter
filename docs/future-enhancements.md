# Future Enhancements

## Phase Vocoder After Quantization

**Priority:** Low
**Type:** Enhancement
**Status:** Proposed

### Problem

Currently, the phase vocoder (Enhanced Mode) has minimal effect when quantization is enabled. This is because of the processing order:

```
1. Phase Vocoder → computes coherent phase for shifted frequencies
2. Frequency Shifting → moves bins by Hz amount
3. Musical Quantization → redistributes energy to scale notes (destroys phase coherence)
```

The quantizer reassigns energy to new bin positions, which discards the carefully computed phase relationships from the phase vocoder.

### Observed Behavior

- **Quantize = 0%**: Enhanced Mode makes an audible difference (less metallic artifacts)
- **Quantize > 0%**: Enhanced Mode has little to no audible effect

### Proposed Solution

Restructure the processing pipeline so phase vocoder runs **after** quantization:

```
1. Frequency Shifting → moves bins by Hz amount
2. Musical Quantization → redistributes energy to scale notes
3. Phase Vocoder → computes coherent phase for final spectrum
```

### Implementation Considerations

1. **Phase vocoder needs bin tracking**: Currently tracks phase evolution per-bin across frames. After quantization, energy moves to different bins, so the vocoder needs to track the *destination* bins, not the original ones.

2. **Quantization strength interpolation**: When `quantizeStrength` is between 0-100%, energy is blended between original and quantized positions. The phase vocoder would need to handle this gracefully.

3. **Peak detection adjustment**: Peak detection should run on the post-quantization spectrum, as those are the actual frequencies being output.

### Alternative Approaches

1. **Quantizer-aware phase synthesis**: Have the quantizer preserve phase relationships when redistributing energy, rather than creating new phases.

2. **Hybrid approach**: Run phase vocoder both before shifting AND after quantization, blending results based on quantize strength.

3. **Per-partial tracking**: Instead of per-bin processing, track individual partials/harmonics through the entire pipeline.

### References

- Current implementation: `plugin/src/PluginProcessor.cpp` lines 295-312
- Phase vocoder: `plugin/src/dsp/PhaseVocoder.cpp`
- Quantizer: `plugin/src/dsp/MusicalQuantizer.cpp`
- Laroche & Dolson (1999): "Improved phase vocoder time-scale modification of audio"

### Notes

The current behavior is not a bug, just a limitation. Enhanced Mode still works correctly when quantization is disabled, which is the primary use case for hearing the "pure" frequency shifting effect.
