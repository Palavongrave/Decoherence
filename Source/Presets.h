#pragma once

#include <array>

/** Factory presets. Values are in the parameters' real units. */
struct FactoryPreset
{
    const char* name;
    float input;     // dB
    float feedback;  // %  (repeat gain = sqrt(feedback): 36% -> 0.6, 64% -> 0.8, 100% = forever)
    float density;   // %  how far the cloud stretches (0% ~ 6 s per pluck, 100% = 60 s bloom)
    float timeMs;    // input delay, 20 ms - 20 s
    float spin;      // %
    int   model;     // 0..5  Drift Large/Medium/Small, Surge Large/Medium/Small
    float tone;      // -100 dark .. 0 neutral .. +100 bright
    float mix;       // %
    float output;    // dB
    float width;     // %
};

inline constexpr std::array<FactoryPreset, 14> factoryPresets { {
    //  name                     in     fb    dens     time   spin mdl   tone    mix    out  width
    { "First Light",            0.0f, 36.0f,  20.0f,   250.0f, 30.0f, 0,   0.0f, 100.0f,  0.0f, 110.0f },
    { "Endless Pluck",          0.0f, 64.0f,  50.0f,   180.0f, 25.0f, 0, -10.0f, 100.0f, -2.0f, 110.0f },
    { "Slow Bloom",             0.0f, 49.0f,  35.0f,   300.0f, 35.0f, 1,   5.0f, 100.0f,  0.0f, 120.0f },
    { "Glass Swarm",            0.0f, 42.0f,  10.0f,   120.0f, 40.0f, 5,  45.0f,  90.0f,  0.0f, 130.0f },
    { "Wide Horizon",           0.0f, 50.0f,  25.0f,  1200.0f, 20.0f, 0,  10.0f, 100.0f,  0.0f, 130.0f },
    { "Colored Static",         0.0f, 85.0f, 100.0f,    90.0f, 55.0f, 3,  10.0f, 100.0f, -4.0f, 140.0f },
    { "Low Hum",                0.0f, 90.0f,  20.0f,   220.0f, 20.0f, 1, -60.0f, 100.0f, -3.0f, 100.0f },
    { "Surge Pad",              0.0f, 49.0f,  25.0f,   200.0f, 30.0f, 4,   0.0f, 100.0f,  0.0f, 120.0f },
    { "Tape Ghost",             0.0f, 42.0f,   0.0f,   350.0f, 75.0f, 2, -15.0f,  85.0f,  0.0f,  90.0f },
    { "Dark Undertow",          0.0f, 72.0f,  65.0f,   500.0f, 35.0f, 0, -55.0f, 100.0f, -2.0f, 100.0f },
    { "Bright Dust",            0.0f, 36.0f,   0.0f,   150.0f, 45.0f, 5,  75.0f,  80.0f,  0.0f, 130.0f },
    { "Endless Drone",         -3.0f, 94.0f, 100.0f,   600.0f, 40.0f, 0, -20.0f, 100.0f, -4.0f, 120.0f },
    { "Glacial Loop",           0.0f, 80.0f,  45.0f,  6000.0f, 25.0f, 0, -10.0f, 100.0f, -2.0f, 120.0f },
    { "Halo",                   0.0f, 56.0f,  75.0f,   300.0f, 50.0f, 3,  25.0f, 100.0f, -1.0f, 140.0f },
} };
