#pragma once

#include <array>

namespace cw
{

// A "model" sets how the cascade spreads the sound: the span of its delay-tap stages, the size
// of its smearing networks, and in which order Density switches things on.
struct ModelSpec
{
    const char* name;
    const char* description;
    float longSpanSec;     // stage A: 8 taps over this time (stages C and D are 3x and 6x longer)
    float midSpanSec;      // stage B: 8 taps filling the gaps between stage A's taps
    float diffusionScale;  // size of the diffuser and bloom networks (1 = medium)
    bool  fastBuild;       // false: long taps first (slow build-up)
                           // true:  diffusion and short taps first (quick swell)
    float envelope;        // tap gain tilt across a stage: < 0 front-loads the energy
    float jitter;          // irregularity of the tap spacing (0 = even grid, 1 = loose)
    float modRate;         // Spin rate multiplier
    float makeupDb;        // level compensation at 0% Density
    float upperMakeupDb;   // additional compensation at 100% Density
    float midMakeupDb;     // extra correction in the middle of the Density range
};

inline constexpr int numModels = 6;

inline const std::array<ModelSpec, numModels>& getModels()
{
    static const std::array<ModelSpec, numModels> models { {
        { "Drift Large",  "Builds slowly into a vast cloud that unfolds over several seconds.",
          3.00f, 0.360f, 1.35f, false,  0.0f, 0.60f, 0.8f, -3.8f, 3.9f, -0.45f },

        { "Drift Medium", "Builds slowly into a cloud about two seconds long.",
          1.60f, 0.190f, 1.00f, false,  0.0f, 0.60f, 1.0f, -3.8f, 4.9f, -1.2f },

        { "Drift Small",  "Builds slowly into a compact, close cloud.",
          0.72f, 0.086f, 0.80f, false,  0.0f, 0.60f, 1.2f,  2.4f, 4.8f, -0.25f },

        { "Surge Large",  "Swells up fast, then streams out over several seconds.",
          3.00f, 0.360f, 1.35f, true,  -1.4f, 0.60f, 0.8f, -4.4f, 4.3f, -0.2f },

        { "Surge Medium", "Swells up fast into a medium-length cloud.",
          1.60f, 0.190f, 1.00f, true,  -1.4f, 0.60f, 1.0f, -2.6f, 5.0f, -1.5f },

        { "Surge Small",  "Swells up fast into a short, dense shimmer.",
          0.72f, 0.086f, 0.80f, true,  -1.4f, 0.60f, 1.2f,  1.8f, 5.2f,  0.55f },
    } };
    return models;
}

} // namespace cw
