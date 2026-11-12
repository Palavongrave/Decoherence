#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include "Models.h"

namespace cw
{

//==============================================================================
/** Power-of-two circular buffer with 4-point Catmull-Rom fractional reads. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Delay 0 is the most recently pushed sample. Valid range: [1, maxDelay()]. */
    inline float read (float delay) const noexcept
    {
        const int   i = (int) delay;
        const float f = delay - (float) i;
        const int base = writePos - 1 - i;

        const float xm1 = buffer[(size_t) ((base + 1) & mask)];
        const float x0  = buffer[(size_t) ( base      & mask)];
        const float x1  = buffer[(size_t) ((base - 1) & mask)];
        const float x2  = buffer[(size_t) ((base - 2) & mask)];

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    float maxDelay() const noexcept { return (float) (mask - 4); }

private:
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
/**
    The Decoherence engine: sound densification.

    Signal flow:

      input -> (+) -> [feedback delay: Time] --+-> diffuser -> taps A -> taps B -> taps C -> taps D --+-> tilt -> wet
                ^                              |   16 allpass   seconds   fill-in   long      longest  |
                +---- Feedback x Tone <--------+   (soft swell)                                        +-> bloom
                                                                                                         16-line FDN, 1.5 -> 60 s

    The feedback delay repeats the input every Time (Freeze = 100% feedback with the input shut).
    Time changes crossfade to the new length instead of stretching the delay, so turning it
    never bends the pitch. Each repeat is melted into a soft swell by a chain of 16 slowly
    drifting allpass diffusers (flat spectrum: no grain, no ringing), then spread over seconds
    by the tap stages (up to 9^4 = 6561 overlapping copies) and sustained by the bloom.

    Density 0..1 stretches the drone: 0 = taps A + B and the full diffuser (about 6 s from one
    pluck on Drift Large); up to 50% adds stage C (3x stage A's span), up to 100% stage D (6x),
    while the bloom's decay grows from 1.5 s to 60 s. Internally Density maps onto "units"
    (32 at 0%, 96 at 100%); Density down to -0.5 reaches the sparse lower units (plain repeats
    at -0.5), which the tests use but the plugin doesn't expose.

    Tone (-1 dark .. 0 neutral .. +1 bright) is a smooth, cut-only tilt in three places: the
    input-delay loop, the bloom's damping, and a +/-6 dB tilt on the output. No resonances.
*/
class CascadeEngine
{
public:
    static constexpr int   numTapStages     = 4;
    static constexpr int   tapsPerStage     = 8;
    static constexpr int   numDiffusers     = 16;
    static constexpr int   bloomLines       = 16;
    static constexpr int   controlInterval  = 32;
    static constexpr float minTimeMs        = 20.0f;
    static constexpr float maxTimeMs        = 20000.0f;
    static constexpr float timeFadeSec      = 0.08f;   // crossfade length when Time changes
    static constexpr float maxModMs         = 8.0f;
    static constexpr float tiltPivotHz      = 800.0f;  // Tone's output tilt pivot ...
    static constexpr float tiltMaxDb        = 6.0f;    // ... and range at full dark / bright
    static constexpr float diffusion        = 0.62f;   // allpass coefficient
    static constexpr float dampingHz        = 6500.0f; // gentle high cut on the wet signal
    static constexpr float stageCScale      = 3.0f;    // stage C span = 3 x stage A span
    static constexpr float stageDScale      = 6.0f;    // stage D span = 6 x stage A span
    static constexpr float minBloomRt       = 1.5f;    // bloom decay (RT60) at 0% Density ...
    static constexpr float maxBloomRt       = 60.0f;   // ... and at 100%

    struct Parameters
    {
        float timeMs   = 250.0f;  // feedback delay time
        float density  = 0.25f;   // 0..1 (see above; -0.5..0 is a test-only sparse range)
        float feedback = 0.4f;    // 0..1; repeat gain is sqrt(feedback), 1 = forever
        float spin     = 0.3f;    // 0..1
        float tone     = 0.0f;    // -1 dark .. 0 neutral .. +1 bright
        bool  freeze   = false;
        int   model    = 0;
    };

    //==============================================================================
    void prepare (double sampleRate);
    void reset();
    void setParameters (const Parameters& p) noexcept   { params = p; }

    /** Renders the wet signal only. In and out pointers may alias. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    //==============================================================================
    // Pure helpers shared with the UI so the display matches the DSP exactly.
    struct TapLayout
    {
        float delaySec[numTapStages][tapsPerStage]; // tap delay in seconds
        float shape[numTapStages][tapsPerStage];    // gain shape (envelope)
        float order[numTapStages][tapsPerStage];    // Density unit where the tap starts to fade in
        float orderWidth[numTapStages];             // units the fade-in takes
        float diffuserSec[numDiffusers];             // allpass section delays in seconds
        float diffuserOrder[numDiffusers];
        float bloomSec[bloomLines];
    };

    static void  computeTapLayout (int modelIndex, int channel, TapLayout& out) noexcept;
    static float unitWeight (float units, float order, float width = 1.0f) noexcept
    {
        return std::clamp ((units - order) / width, 0.0f, 1.0f);
    }
    static float unitsFor (float density) noexcept      { return 32.0f + 64.0f * std::clamp (density, -0.5f, 1.0f); }
    static float feedbackGain (float feedback) noexcept { return std::sqrt (std::clamp (feedback, 0.0f, 1.0f)); }
    static float stageGainNorm (const TapLayout&, int stage, float units) noexcept;
    static int   echoCount (int modelIndex, float density) noexcept;
    static float bloomAmount (float density) noexcept   { return unitWeight (unitsFor (density), 32.0f, 64.0f); }
    static float bloomRt (float bloom) noexcept         { return minBloomRt * std::pow (maxBloomRt / minBloomRt, bloom); }
    static int   diffuserCount (int modelIndex, float density) noexcept;
    static float stageScale (int stage) noexcept        { return stage == 2 ? stageCScale : stage == 3 ? stageDScale : 1.0f; }

    /** Energy of the diffuser chain's impulse response (left channel, no modulation),
        accumulated into bins of binSec seconds. Used to draw the echo display. */
    static void diffuserEnergy (int modelIndex, float density, float binSec, int numBins, std::vector<float>& out);

    /** Gains the engine uses to blend the cascade and the bloom at a given Density. */
    static float cascadeGain (float bloom) noexcept     { return 1.0f - 0.35f * bloom; }
    static float bloomGain (float bloom) noexcept       { return 1.6f * std::sqrt (bloom); }

private:
    struct Mod
    {
        float delay = 1.0f, delayInc = 0.0f, delayTarget = 1.0f;
        float depth = 0.0f, depthInc = 0.0f, depthTarget = 0.0f;
        float lfoS = 0.0f, lfoC = 1.0f, rotS = 0.0f, rotC = 1.0f;
        float rateMul = 1.0f;
    };

    struct Tap : Mod
    {
        float gain = 0.0f, gainInc = 0.0f, gainTarget = 0.0f;
    };

    struct TapStage
    {
        DelayLine line;
        Tap taps[tapsPerStage];
        float direct = 1.0f, directInc = 0.0f, directTarget = 1.0f;
        float maxDelaySamples = 1.0f;
    };

    struct Diffuser : Mod
    {
        DelayLine line;
        float mix = 0.0f, mixInc = 0.0f, mixTarget = 0.0f;  // 0 = bypassed, 1 = fully in
        float maxDelaySamples = 2.0f;
    };

    struct NetworkLine : Mod
    {
        DelayLine line;
        float gain = 0.0f, gainInc = 0.0f, gainTarget = 0.0f;
        float lp = 0.0f, hp = 0.0f;   // damping (Tone-dependent) states
        float inSign = 1.0f, outSign = 1.0f;
        float maxDelaySamples = 2.0f;
    };

    struct Bloom
    {
        NetworkLine lines[bloomLines];
        float norm = 1.0f, normInc = 0.0f, normTarget = 1.0f;
        float cascGain = 1.0f, cascGainInc = 0.0f, cascGainTarget = 1.0f;
        float wetGain = 0.0f, wetGainInc = 0.0f, wetGainTarget = 0.0f;
    };

    struct FeedbackLoop
    {
        DelayLine line;
        Mod mod;                      // Spin depth / LFO only; the length is below
        float lp = 0.0f, hp = 0.0f;   // Tone filter states (cut-only)

        // Time changes crossfade from the current length (A) to the new one (B), so the
        // repeats never change pitch.
        float lengthA = 1.0f, lengthB = 1.0f;
        float fade = 0.0f, fadeInc = 0.0f;
        bool  fading = false;
    };

    void updateControl (int len) noexcept;
    void finishControl() noexcept;
    void initModulators() noexcept;

    static inline float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 1.0f)
            return x;
        return std::copysign (1.0f + std::tanh (a - 1.0f), x);
    }

    //==============================================================================
    Parameters params;
    double fs = 48000.0;
    bool prepared = false, snapNext = true;

    TapStage stages[2][numTapStages];
    Diffuser diffusers[2][numDiffusers];
    Bloom bloom;
    FeedbackLoop loops[2];
    TapLayout layouts[2];

    int   activeModel = -1, pendingModel = -1;
    float modelFade = 1.0f, modelFadeInc = 0.0f;   // fades the wet out and back in on model change

    float densitySm = 0.25f, feedbackSm = 0.0f, spinSm = 0.0f, toneSm = 0.0f;
    float freezeAmt = 0.0f, lastBaseW = -1.0f;

    float inGain = 1.0f, inGainInc = 0.0f, inGainTarget = 1.0f;
    float fbGain = 0.0f, fbGainInc = 0.0f, fbGainTarget = 0.0f;
    float makeup = 1.0f, makeupInc = 0.0f, makeupTarget = 1.0f;

    // Tone: one-pole coefficients (1 = filter open) for the loop, the bloom, and the output tilt
    float loopLpCoef = 1.0f, loopHpCoef = 0.0f;
    float bloomLpCoef = 1.0f, bloomHpCoef = 0.0f;
    float tiltCoef = 0.0f, tiltLow = 1.0f, tiltHigh = 1.0f;
    float tiltState[2] {};

    float dcState[2] {}, lpState[2] {};
    float dcCoef = 0.0f, lpCoef = 1.0f;
    float maxLoopDelay = 1.0f;
};

} // namespace cw
