#include "CascadeEngine.h"

#include <cstdint>

namespace cw
{

namespace
{
    constexpr float twoPi    = 6.28318530717959f;
    constexpr float invSqrt2 = 0.70710678f;
    constexpr float invSqrt8 = 0.35355339f;

    inline uint32_t hash32 (uint32_t x) noexcept
    {
        x ^= x >> 16; x *= 0x7feb352dU;
        x ^= x >> 15; x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    /** Deterministic pseudo-random value in [0, 1) from a few integer keys. */
    inline float rand01 (uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) noexcept
    {
        const uint32_t h = hash32 (a * 0x9E3779B1U ^ hash32 (b * 0x85EBCA77U ^ hash32 (c * 0xC2B2AE3DU ^ hash32 (d + 0x27D4EB2FU))));
        return (float) (h & 0xFFFFFFu) / 16777216.0f;
    }

    inline float smoothingCoef (float timeSeconds, int len, double fs) noexcept
    {
        return 1.0f - std::exp (-(float) len / (timeSeconds * (float) fs));
    }

    inline void smoothTowards (float& value, float target, float k, float snapEps) noexcept
    {
        value += (target - value) * k;
        if (std::abs (target - value) < snapEps)
            value = target;
    }

    inline void rotate (float& s, float& c, float rotS, float rotC) noexcept
    {
        const float sNext = s * rotC + c * rotS;
        c = c * rotC - s * rotS;
        s = sNext;
    }

    inline void renormalise (float& c, float& s) noexcept
    {
        const float k = 1.5f - 0.5f * (c * c + s * s); // one Newton step towards unit length
        c *= k;
        s *= k;
    }

    /** In-place orthogonal Hadamard mix (energy preserving). */
    template <int N>
    inline void hadamard (float* v) noexcept
    {
        for (int stride = 1; stride < N; stride <<= 1)
            for (int j = 0; j < N; j += 2 * stride)
                for (int k = j; k < j + stride; ++k)
                {
                    const float a = v[k], b = v[k + stride];
                    v[k]          = (a + b) * invSqrt2;
                    v[k + stride] = (a - b) * invSqrt2;
                }
    }

    constexpr float maxPositionScale = 1.06f * 1.011f; // largest tap slot position x detune
    constexpr float maxStretch       = 1.09f;          // largest left/right length difference

    // Base lengths (ms) before the model's scale. Irregular so no two share a period.
    // Diffusers: short ones remove clicks, long ones widen the smear.
    constexpr float diffuserBaseMs[CascadeEngine::numDiffusers] =
    {
         3.1f,  4.3f,  5.6f,  7.1f,  8.9f, 10.7f, 12.9f, 15.2f,
        17.8f, 20.9f, 24.1f, 27.6f, 31.4f, 35.9f, 40.2f, 45.3f
    };
    constexpr float bloomBaseMs[CascadeEngine::bloomLines] =
    {
         47.3f,  59.9f,  71.1f,  83.7f,  97.3f, 109.9f, 124.1f, 139.7f,
        153.1f, 168.9f, 186.7f, 203.9f, 221.3f, 241.7f, 263.9f, 292.1f
    };

    inline float stretch (int channel, int k, float seed) noexcept
    {
        return channel == 0 ? 1.0f : 1.0f + 0.08f * std::sin (2.1f * (float) k + 0.4f + seed);
    }

    inline float rt60Gain (float delaySamples, float rtSeconds, double fs) noexcept
    {
        return std::pow (10.0f, -3.0f * delaySamples / (rtSeconds * (float) fs));
    }
}

//==============================================================================
void CascadeEngine::computeTapLayout (int modelIndex, int channel, TapLayout& out) noexcept
{
    const int mi = std::clamp (modelIndex, 0, numModels - 1);
    const auto& m = getModels()[(size_t) mi];
    const auto ch = (uint32_t) channel;
    const auto sizeSeed = (uint32_t) (mi % 3); // Drift/Surge of the same size share their timing
    const float spans[numTapStages] = { m.longSpanSec, m.midSpanSec,
                                        m.longSpanSec * stageCScale, m.longSpanSec * stageDScale };

    for (int s = 0; s < numTapStages; ++s)
    {
        for (int i = 0; i < tapsPerStage; ++i)
        {
            // Taps sit near even slots across the stage span, nudged off the grid so the
            // arrivals never form a regular (pitched) pulse train.
            const float slot   = (float) (i + 1) / (float) tapsPerStage;
            const float r      = rand01 (17u + sizeSeed, (uint32_t) s, (uint32_t) i, ch) - 0.5f;
            const float p      = std::clamp (slot + r * m.jitter / (float) tapsPerStage, 0.04f, 1.06f);
            const float detune = 1.0f + 0.011f * std::sin (1.7f * (float) (s * tapsPerStage + i) + 0.9f * (float) ch);

            out.delaySec[s][i] = p * spans[s] * detune;
            out.shape[s][i]    = std::exp (m.envelope * (p - 0.5f));
        }
    }

    // Density switch-on order, in units (Density 0% = 32 units, 50% = 64, 100% = 96)
    for (int i = 0; i < tapsPerStage; ++i)
    {
        out.order[0][i] = (float) ((m.fastBuild ? 24 : 0) + i);   // A  (always on in the plugin)
        out.order[1][i] = (float) ((m.fastBuild ? 8 : 16) + i);   // B  (always on in the plugin)
        out.order[2][i] = (float) (32 + 4 * i);                   // C  fades in over 0-50%
        out.order[3][i] = (float) (64 + 4 * i);                   // D  fades in over 50-100%
    }
    out.orderWidth[0] = out.orderWidth[1] = 1.0f;
    out.orderWidth[2] = out.orderWidth[3] = 4.0f;

    for (int k = 0; k < numDiffusers; ++k)
    {
        out.diffuserSec[k] = diffuserBaseMs[k] * 0.001f * m.diffusionScale * stretch (channel, k, 1.0f);

        if (m.fastBuild)
            out.diffuserOrder[k] = (float) (k < 8 ? k : 16 + (k - 8));
        else
            out.diffuserOrder[k] = (float) (k < 8 ? 8 + k : 24 + (k - 8));
    }

    for (int k = 0; k < bloomLines; ++k)
        out.bloomSec[k] = bloomBaseMs[k] * 0.001f * m.diffusionScale;
}

float CascadeEngine::stageGainNorm (const TapLayout& layout, int stage, float units) noexcept
{
    float e = 1.0f; // direct path
    for (int i = 0; i < tapsPerStage; ++i)
    {
        const float g = unitWeight (units, layout.order[stage][i], layout.orderWidth[stage]) * layout.shape[stage][i];
        e += g * g;
    }
    return 1.0f / std::sqrt (e);
}

int CascadeEngine::diffuserCount (int modelIndex, float density) noexcept
{
    TapLayout layout;
    computeTapLayout (modelIndex, 0, layout);
    const float units = unitsFor (density);

    float active = 0.0f;
    for (int k = 0; k < numDiffusers; ++k)
        active += unitWeight (units, layout.diffuserOrder[k]);
    return (int) std::lround (active);
}

int CascadeEngine::echoCount (int modelIndex, float density) noexcept
{
    TapLayout layout;
    computeTapLayout (modelIndex, 0, layout);
    const float units = unitsFor (density);

    double count = 1.0;
    for (int s = 0; s < numTapStages; ++s)
    {
        float active = 1.0f;
        for (int i = 0; i < tapsPerStage; ++i)
            active += unitWeight (units, layout.order[s][i], layout.orderWidth[s]);
        count *= (double) active;
    }
    return (int) std::lround (count);
}

void CascadeEngine::diffuserEnergy (int modelIndex, float density, float binSec, int numBins, std::vector<float>& out)
{
    out.assign ((size_t) numBins, 0.0f);

    TapLayout layout;
    computeTapLayout (modelIndex, 0, layout);
    const float units = unitsFor (density);

    const double rate = 16000.0;
    const int total = std::min ((int) (binSec * (float) numBins * rate), (int) (3.0 * rate));

    std::vector<float> bufs[numDiffusers];
    int idx[numDiffusers] {};
    float w[numDiffusers];

    for (int k = 0; k < numDiffusers; ++k)
    {
        bufs[k].assign ((size_t) std::max (1, (int) std::lround (layout.diffuserSec[k] * rate)), 0.0f);
        w[k] = unitWeight (units, layout.diffuserOrder[k]);
    }

    for (int t = 0; t < total; ++t)
    {
        float v = t == 0 ? 1.0f : 0.0f;

        for (int k = 0; k < numDiffusers; ++k)
        {
            auto& buf = bufs[k];
            const float wd = buf[(size_t) idx[k]];
            const float wn = v + diffusion * wd;
            buf[(size_t) idx[k]] = wn;
            idx[k] = (idx[k] + 1) % (int) buf.size();
            v += w[k] * ((wd - diffusion * wn) - v);
        }

        const int bin = (int) ((double) t / rate / binSec);
        if (bin < numBins)
            out[(size_t) bin] += v * v;
    }
}

//==============================================================================
void CascadeEngine::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double modMax = maxModMs * 0.001 * fs;

    float longest[numTapStages] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float maxScale = 0.0f;
    for (const auto& m : getModels())
    {
        longest[0] = std::max (longest[0], m.longSpanSec);
        longest[1] = std::max (longest[1], m.midSpanSec);
        longest[2] = std::max (longest[2], m.longSpanSec * stageCScale);
        longest[3] = std::max (longest[3], m.longSpanSec * stageDScale);
        maxScale   = std::max (maxScale, m.diffusionScale);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int s = 0; s < numTapStages; ++s)
        {
            auto& st = stages[ch][s];
            st.line.prepare ((int) std::ceil (longest[s] * maxPositionScale * fs + modMax + 8.0));
            st.maxDelaySamples = st.line.maxDelay();
        }

        for (int k = 0; k < numDiffusers; ++k)
        {
            auto& d = diffusers[ch][k];
            d.line.prepare ((int) std::ceil (diffuserBaseMs[numDiffusers - 1] * 0.001 * maxScale * maxStretch * fs + modMax + 8.0));
            d.maxDelaySamples = d.line.maxDelay();
        }

        loops[ch].line.prepare ((int) std::ceil (maxTimeMs * 0.001 * fs + modMax + 8.0));
    }

    for (int k = 0; k < bloomLines; ++k)
    {
        auto& l = bloom.lines[k];
        l.line.prepare ((int) std::ceil (bloomBaseMs[k] * 0.001 * maxScale * fs + modMax + 8.0));
        l.maxDelaySamples = l.line.maxDelay();
        l.inSign  = rand01 (463u, (uint32_t) k) < 0.5f ? -1.0f : 1.0f;
        l.outSign = rand01 (467u, (uint32_t) k) < 0.5f ? -1.0f : 1.0f;
    }

    maxLoopDelay = loops[0].line.maxDelay();
    dcCoef       = 1.0f - std::exp (-twoPi * 15.0f / (float) fs);
    lpCoef       = 1.0f - std::exp (-twoPi * std::min (dampingHz, 0.45f * (float) fs) / (float) fs);
    tiltCoef     = 1.0f - std::exp (-twoPi * tiltPivotHz / (float) fs);

    prepared = true;
    reset();
}

void CascadeEngine::initModulators() noexcept
{
    // Every modulator starts from the same phase after a reset, so the engine is fully
    // repeatable (renders and tests give identical results for identical input).
    auto initLfo = [] (Mod& md, float rateMul, float phase)
    {
        md.rateMul = rateMul;
        md.lfoS = std::sin (phase);
        md.lfoC = std::cos (phase);
    };

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = (uint32_t) ch;

        for (int s = 0; s < numTapStages; ++s)
            for (int i = 0; i < tapsPerStage; ++i)
                initLfo (stages[ch][s].taps[i], 0.6f + 0.8f * rand01 (401u, c, (uint32_t) s, (uint32_t) i),
                         twoPi * rand01 (409u, c, (uint32_t) s, (uint32_t) i));

        for (int k = 0; k < numDiffusers; ++k)
            initLfo (diffusers[ch][k], 0.5f + rand01 (433u, c, (uint32_t) k), twoPi * rand01 (439u, c, (uint32_t) k));

        initLfo (loops[ch].mod, ch == 0 ? 0.83f : 1.17f, ch == 0 ? 0.0f : 1.9f);
    }

    for (int k = 0; k < bloomLines; ++k)
        initLfo (bloom.lines[k], 0.5f + rand01 (479u, (uint32_t) k), twoPi * rand01 (487u, (uint32_t) k));
}

void CascadeEngine::reset()
{
    initModulators();

    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& st : stages[ch])   st.line.clear();
        for (auto& d : diffusers[ch]) d.line.clear();

        loops[ch].line.clear();
        loops[ch].lp = loops[ch].hp = 0.0f;
        loops[ch].fading = false;
        loops[ch].fade = 0.0f;
        dcState[ch] = lpState[ch] = tiltState[ch] = 0.0f;
    }

    for (auto& l : bloom.lines) { l.line.clear(); l.lp = l.hp = 0.0f; }

    activeModel  = -1;
    pendingModel = -1;
    modelFade    = 1.0f;
    lastBaseW    = -1.0f;
    snapNext     = true;
}

//==============================================================================
void CascadeEngine::updateControl (int len) noexcept
{
    const bool snap = snapNext;
    snapNext = false;

    // ---- model changes: fade the wet out, swap the layout, fade back in
    const int requested = std::clamp (params.model, 0, numModels - 1);
    bool jumpDelays = false;

    if (activeModel < 0 || snap)
    {
        activeModel  = requested;
        pendingModel = -1;
        modelFade    = 1.0f;
        computeTapLayout (activeModel, 0, layouts[0]);
        computeTapLayout (activeModel, 1, layouts[1]);
        jumpDelays = true;
    }
    else if (requested != activeModel)
    {
        pendingModel = requested;
    }

    if (pendingModel >= 0 && modelFade <= 0.0f)
    {
        activeModel  = pendingModel;
        pendingModel = -1;
        computeTapLayout (activeModel, 0, layouts[0]);
        computeTapLayout (activeModel, 1, layouts[1]);
        jumpDelays = true; // inaudible: the wet is fully faded out right now
    }

    const float fadeTarget = pendingModel >= 0 ? 0.0f : 1.0f;
    const float fadeStep   = (float) len / (0.04f * (float) fs);
    const float fadeEnd    = modelFade + std::clamp (fadeTarget - modelFade, -fadeStep, fadeStep);
    modelFadeInc = (fadeEnd - modelFade) / (float) len;

    const auto& m = getModels()[(size_t) activeModel];

    // ---- parameter smoothing (Time isn't smoothed: it crossfades, see the feedback delay)
    const float kFast   = snap ? 1.0f : smoothingCoef (0.05f, len, fs);
    const float kFreeze = snap ? 1.0f : smoothingCoef (0.04f, len, fs);

    smoothTowards (densitySm,  std::clamp (params.density, -1.0f, 1.0f),               kFast,   1.0e-5f);
    smoothTowards (feedbackSm, std::clamp (params.feedback, 0.0f, 1.0f),               kFast,   1.0e-5f);
    smoothTowards (spinSm,     std::clamp (params.spin, 0.0f, 1.0f),                   kFast,   1.0e-5f);
    smoothTowards (toneSm,     std::clamp (params.tone, -1.0f, 1.0f),                  kFast,   1.0e-5f);
    smoothTowards (freezeAmt,  params.freeze ? 1.0f : 0.0f,                            kFreeze, 1.0e-4f);

    const float units = unitsFor (densitySm);

    // ---- Spin. The diffusers and the bloom always drift a little (never static), which
    // keeps any single frequency from ringing; Spin adds more on top. The curves keep the
    // lower half of the knob gentle and put the extra range (up to 8 ms, ~1.2 Hz) at the top.
    const float depthMax    = maxModMs * 0.001f * (float) fs * std::pow (spinSm, 2.08f);
    const float rateHz      = (0.05f + 1.1f * std::pow (spinSm, 1.9f)) * m.modRate;
    const float baseW       = twoPi * rateHz / (float) fs;
    const float netW        = twoPi * std::max (rateHz, 0.12f) / (float) fs;
    const bool  rateChanged = std::abs (baseW - lastBaseW) > 1.0e-10f;
    lastBaseW = baseW;

    const float invLen = 1.0f / (float) len;

    auto rampMod = [&] (Mod& md, float w)
    {
        if (snap)
            md.depth = md.depthTarget;
        if (snap || jumpDelays)
            md.delay = md.delayTarget;

        md.delayInc = (md.delayTarget - md.delay) * invLen;
        md.depthInc = (md.depthTarget - md.depth) * invLen;

        if (rateChanged)
        {
            md.rotC = std::cos (w * md.rateMul);
            md.rotS = std::sin (w * md.rateMul);
        }
    };

    auto ramp = [&] (float& value, float& inc, float target)
    {
        if (snap)
            value = target;
        inc = (target - value) * invLen;
    };

    float spanOn = 0.0f, spanAll = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& layout = layouts[ch];

        // ---- diffusers: 16 allpasses melt every repeat into a soft swell. They always drift
        // a little (a floor of 0.1 ms) so their pattern never sits still and rings.
        for (int k = 0; k < numDiffusers; ++k)
        {
            auto& d = diffusers[ch][k];
            const float on = unitWeight (units, layout.diffuserOrder[k]);

            d.mixTarget   = on;
            d.delayTarget = std::clamp (layout.diffuserSec[k] * (float) fs, 2.0f, d.maxDelaySamples);
            d.depthTarget = std::min (0.0001f * (float) fs + depthMax * 0.12f, 0.2f * d.delayTarget);
            ramp (d.mix, d.mixInc, d.mixTarget);
            rampMod (d, netW);

            if (ch == 0)
            {
                spanOn  += 2.0f * layout.diffuserSec[k] * on;
                spanAll += 2.0f * layout.diffuserSec[k];
            }
        }

        // ---- tap stages A (seconds), B (fill-in), C (very long, above 50%)
        for (int s = 0; s < numTapStages; ++s)
        {
            auto& st = stages[ch][s];
            const float norm       = stageGainNorm (layout, s, units);
            const float stageDepth = depthMax * (s == 1 ? 0.5f : 1.0f);
            const float span       = s == 0 ? m.longSpanSec : m.midSpanSec;

            ramp (st.direct, st.directInc, norm);
            st.directTarget = norm;

            for (int i = 0; i < tapsPerStage; ++i)
            {
                auto& t = st.taps[i];
                const float on = unitWeight (units, layout.order[s][i], layout.orderWidth[s]);

                t.gainTarget  = on * layout.shape[s][i] * norm;
                t.delayTarget = std::clamp (layout.delaySec[s][i] * (float) fs, 1.0f, st.maxDelaySamples);
                t.depthTarget = std::min (stageDepth, 0.45f * t.delayTarget);
                ramp (t.gain, t.gainInc, t.gainTarget);
                rampMod (t, baseW);

                if (ch == 0 && s < 2)
                {
                    spanOn  += span * on;
                    spanAll += span;
                }
            }
        }

    }

    // ---- bloom: a large, slowly drifting FDN whose decay grows from 1.5 s to 30 s above 50%
    const float bloomE  = unitWeight (units, 32.0f, 64.0f);   // = bloomAmount (density): grows over the whole knob
    const float bloomRtS = bloomRt (bloomE);
    float meanB = 0.0f;

    for (int k = 0; k < bloomLines; ++k)
    {
        auto& l = bloom.lines[k];
        l.delayTarget = std::clamp (layouts[0].bloomSec[k] * (float) fs, 2.0f, l.maxDelaySamples);
        l.depthTarget = std::min (0.0005f * (float) fs + depthMax * 0.3f, 0.1f * l.delayTarget);
        l.gainTarget  = rt60Gain (l.delayTarget, bloomRtS, fs);
        ramp (l.gain, l.gainInc, l.gainTarget);
        rampMod (l, netW);
        meanB += l.delayTarget / (float) bloomLines;
    }

    const float gBarB = rt60Gain (meanB, bloomRtS, fs);
    bloom.normTarget     = std::sqrt (1.0f - gBarB * gBarB);
    bloom.cascGainTarget = cascadeGain (bloomE);
    bloom.wetGainTarget  = bloomGain (bloomE);
    ramp (bloom.norm, bloom.normInc, bloom.normTarget);
    ramp (bloom.cascGain, bloom.cascGainInc, bloom.cascGainTarget);
    ramp (bloom.wetGain, bloom.wetGainInc, bloom.wetGainTarget);

    // ---- feedback delay. A new Time starts an 80 ms crossfade from the current length to the
    // new one (no tape-style pitch bend). Changes arriving mid-fade are picked up right after.
    const float timeTarget = std::clamp (params.timeMs, minTimeMs, maxTimeMs) * 0.001f * (float) fs;

    for (auto& L : loops)
    {
        const float target = std::clamp (timeTarget, 2.0f, maxLoopDelay);

        if (snap)
        {
            L.lengthA = L.lengthB = target;
            L.fading = false;
            L.fade = 0.0f;
        }
        else if (! L.fading && std::abs (target - L.lengthA) > 0.5f)
        {
            L.lengthB = target;
            L.fading  = true;
            L.fade    = 0.0f;
            L.fadeInc = 1.0f / (timeFadeSec * (float) fs);
        }

        L.mod.delayTarget = 0.0f; // the Mod only supplies the Spin offset here
        L.mod.depthTarget = std::min (depthMax * 0.4f, 0.2f * std::min (L.lengthA, L.lengthB));
        rampMod (L.mod, baseW);
    }

    const float repeatGain = feedbackGain (feedbackSm);                // each knob position
    fbGainTarget = repeatGain + (1.0f - repeatGain) * freezeAmt;       // sustains 2x longer;
    inGainTarget = 1.0f - freezeAmt;                                   // Freeze = forever, input shut
    ramp (fbGain, fbGainInc, fbGainTarget);
    ramp (inGain, inGainInc, inGainTarget);

    // Level compensation: grows with how far the taps and diffusers spread the energy, plus a
    // model-specific amount for the long stage and the bloom.
    makeupTarget = std::pow (10.0f, (m.makeupDb * (spanOn / spanAll) + m.upperMakeupDb * bloomE
                                     + m.midMakeupDb * 4.0f * bloomE * (1.0f - bloomE)) / 20.0f);
    ramp (makeup, makeupInc, makeupTarget);

    // ---- Tone: a smooth dark <-> bright tilt with no resonances. Every filter inside a loop
    // only cuts (one-pole low-pass / high-pass), so the loops stay stable at any setting.
    auto onePole = [this] (float hz)
    {
        return hz >= 0.45f * (float) fs ? 1.0f : 1.0f - std::exp (-twoPi * hz / (float) fs);
    };

    const float t = toneSm;

    // input-delay loop: dark closes a low-pass (16 kHz -> 600 Hz), bright opens it fully and
    // raises a high-pass (20 Hz -> 900 Hz). Frozen loops bypass both so they hold their timbre.
    const float loopLpHz = t < 0.0f ? 16000.0f * std::pow (600.0f / 16000.0f, std::pow (-t, 0.8f)) : 16000.0f;
    const float loopHpHz = t > 0.0f ? 20.0f * std::pow (900.0f / 20.0f, std::pow (t, 0.9f)) : 20.0f;
    float lpC = onePole (loopLpHz);
    if (t > 0.0f)
        lpC += (1.0f - lpC) * t;
    loopLpCoef = lpC + (1.0f - lpC) * freezeAmt;
    loopHpCoef = onePole (loopHpHz) + (onePole (5.0f) - onePole (loopHpHz)) * freezeAmt;

    // bloom damping: 8 kHz at neutral, 2 kHz fully dark, open fully bright (+ high-pass to 250 Hz)
    bloomLpCoef = onePole (8000.0f * std::pow (2.0f, 2.0f * t));
    bloomHpCoef = onePole (t > 0.0f ? 20.0f * std::pow (250.0f / 20.0f, t) : 20.0f);

    // output tilt around 800 Hz: lows and highs move +/- 6 dB in opposite directions
    tiltLow  = std::pow (10.0f, -t * tiltMaxDb / 20.0f);
    tiltHigh = std::pow (10.0f,  t * tiltMaxDb / 20.0f);
}

void CascadeEngine::finishControl() noexcept
{
    auto snapMod = [] (Mod& md)
    {
        md.delay = md.delayTarget;
        md.depth = md.depthTarget;
        renormalise (md.lfoC, md.lfoS);
    };

    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& st : stages[ch])
        {
            st.direct = st.directTarget;
            for (auto& t : st.taps) { t.gain = t.gainTarget; snapMod (t); }
        }

        for (auto& d : diffusers[ch]) { d.mix = d.mixTarget; snapMod (d); }

        snapMod (loops[ch].mod);
    }

    for (auto& l : bloom.lines) { l.gain = l.gainTarget; snapMod (l); }
    bloom.norm     = bloom.normTarget;
    bloom.cascGain = bloom.cascGainTarget;
    bloom.wetGain  = bloom.wetGainTarget;

    fbGain = fbGainTarget;
    inGain = inGainTarget;
    makeup = makeupTarget;
    modelFade = std::clamp (modelFade, 0.0f, 1.0f);
}

//==============================================================================
void CascadeEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
{
    if (! prepared)
    {
        std::fill (outL, outL + numSamples, 0.0f);
        std::fill (outR, outR + numSamples, 0.0f);
        return;
    }

    auto stepMod = [] (Mod& md)
    {
        md.delay += md.delayInc;
        md.depth += md.depthInc;
        rotate (md.lfoS, md.lfoC, md.rotS, md.rotC);
        return md.delay + md.depth * md.lfoS;
    };

    int done = 0;

    while (done < numSamples)
    {
        const int len = std::min (controlInterval, numSamples - done);
        updateControl (len);

        for (int n = done; n < done + len; ++n)
        {
            inGain    += inGainInc;
            fbGain    += fbGainInc;
            makeup    += makeupInc;
            modelFade += modelFadeInc;
            bloom.norm     += bloom.normInc;
            bloom.cascGain += bloom.cascGainInc;
            bloom.wetGain  += bloom.wetGainInc;

            const float input[2] = { inL[n] * inGain, inR[n] * inGain };
            float casc[2];

            for (int ch = 0; ch < 2; ++ch)
            {
                // ---------------- feedback delay (Time / Feedback / Tone / Freeze)
                auto& L = loops[ch];
                const float spinOffset = stepMod (L.mod);

                // Read happens before this sample's write, hence the -1.
                float y = L.line.read (std::clamp (L.lengthA + spinOffset - 1.0f, 1.0f, maxLoopDelay));

                if (L.fading)
                {
                    const float yB = L.line.read (std::clamp (L.lengthB + spinOffset - 1.0f, 1.0f, maxLoopDelay));
                    L.fade += L.fadeInc;

                    if (L.fade >= 1.0f)
                    {
                        y = yB;
                        L.lengthA = L.lengthB;
                        L.fading  = false;
                        L.fade    = 0.0f;
                    }
                    else
                    {
                        y += (yB - y) * L.fade;
                    }
                }

                // Tone, cut-only: low-pass then high-pass on what feeds back
                L.lp += loopLpCoef * (y - L.lp);
                L.hp += loopHpCoef * (L.lp - L.hp);
                const float toned = L.lp - L.hp;

                L.line.push (softClip (input[ch] + fbGain * toned));

                float sig = y;

                // ---------------- diffuser (sections keep running while switched off,
                // so they come back in smoothly)
                for (auto& d : diffusers[ch])
                {
                    d.mix += d.mixInc;
                    const float wd = d.line.read (std::clamp (stepMod (d) - 1.0f, 1.0f, d.maxDelaySamples));
                    const float wn = sig + diffusion * wd;
                    d.line.push (wn);
                    sig += d.mix * ((wd - diffusion * wn) - sig);
                }

                // ---------------- tap stages: spread the smeared repeats over seconds
                for (auto& st : stages[ch])
                {
                    st.line.push (sig);

                    st.direct += st.directInc;
                    float acc = st.direct * sig;

                    for (auto& t : st.taps)
                    {
                        if (t.gain == 0.0f && t.gainInc == 0.0f)
                            continue;

                        t.gain += t.gainInc;
                        const float d = std::clamp (stepMod (t), 1.0f, st.maxDelaySamples);
                        acc += t.gain * st.line.read (d);
                    }

                    sig = acc;
                }

                casc[ch] = sig;
            }

            // ---------------- bloom: one stereo network fed by both sides of the cascade
            float v[bloomLines], bL = 0.0f, bR = 0.0f;
            for (int k = 0; k < bloomLines; ++k)
            {
                auto& l = bloom.lines[k];
                l.gain += l.gainInc;
                v[k] = l.line.read (std::clamp (stepMod (l) - 1.0f, 1.0f, l.maxDelaySamples));
                if (k & 1) bR += l.outSign * v[k];
                else       bL += l.outSign * v[k];
            }
            bL *= bloom.norm;
            bR *= bloom.norm;

            hadamard<bloomLines> (v);

            for (int k = 0; k < bloomLines; ++k)
            {
                auto& l = bloom.lines[k];
                const float u = l.gain * v[k] + l.inSign * casc[k & 1] * invSqrt8;

                // damping that follows Tone: the long tail gets darker or brighter on every pass
                l.lp += bloomLpCoef * (u - l.lp);
                l.hp += bloomHpCoef * (l.lp - l.hp);
                l.line.push (l.lp - l.hp);
            }

            const float bloomOut[2] = { bL, bR };

            for (int ch = 0; ch < 2; ++ch)
            {
                float sig = casc[ch] * bloom.cascGain + bloomOut[ch] * bloom.wetGain;

                // Tone's output tilt (neutral = exactly flat)
                tiltState[ch] += tiltCoef * (sig - tiltState[ch]);
                sig = tiltState[ch] * tiltLow + (sig - tiltState[ch]) * tiltHigh;

                // remove sub-sonic build-up, then a gentle high cut for warmth
                dcState[ch] += dcCoef * (sig - dcState[ch]);
                sig -= dcState[ch];
                lpState[ch] += lpCoef * (sig - lpState[ch]);

                const float out = lpState[ch] * makeup * modelFade;
                if (ch == 0) outL[n] = out;
                else         outR[n] = out;
            }
        }

        finishControl();
        done += len;
    }
}

} // namespace cw
