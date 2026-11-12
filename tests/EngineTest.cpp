// Stand-alone test harness for the Decoherence engine (no JUCE needed).
//  - stability checks (NaN/Inf, runaway levels, frozen loops)
//  - "pluck to drone" measurements: how much attack survives, how long the sound sustains
//  - level calibration per model
//  - CPU benchmark
//  - renders demo WAV files so the sound can be checked without a DAW
//
// Usage: EngineTest [outputFolderForWavs]

#include "../Source/dsp/CascadeEngine.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using cw::CascadeEngine;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
        if (! ok)
            ++failures;
    }

    struct Stats
    {
        double peak = 0.0, sumSq = 0.0;
        long long count = 0;
        bool finite = true;

        void add (float x)
        {
            if (! std::isfinite (x)) { finite = false; return; }
            peak = std::max (peak, (double) std::abs (x));
            sumSq += (double) x * x;
            ++count;
        }

        double rms() const    { return count > 0 ? std::sqrt (sumSq / (double) count) : 0.0; }
        double rmsDb() const  { return 20.0 * std::log10 (rms() + 1e-15); }
        double peakDb() const { return 20.0 * std::log10 (peak + 1e-15); }
    };

    void run (CascadeEngine& engine, const std::vector<float>& in, std::vector<float>& outL, std::vector<float>& outR)
    {
        const int n = (int) in.size();
        outL.assign ((size_t) n, 0.0f);
        outR.assign ((size_t) n, 0.0f);

        for (int pos = 0; pos < n; pos += blockSize)
        {
            const int len = std::min (blockSize, n - pos);
            engine.process (in.data() + pos, in.data() + pos, outL.data() + pos, outR.data() + pos, len);
        }
    }

    Stats statsOf (const std::vector<float>& l, const std::vector<float>& r, double t0, double t1)
    {
        Stats st;
        const auto a = (size_t) (t0 * sampleRate);
        const auto b = std::min (l.size(), (size_t) (t1 * sampleRate));
        for (size_t i = a; i < b; ++i) { st.add (l[i]); st.add (r[i]); }
        return st;
    }

    Stats monoStats (const std::vector<float>& x, double t0, double t1)
    {
        Stats st;
        const auto a = (size_t) (t0 * sampleRate);
        const auto b = std::min (x.size(), (size_t) (t1 * sampleRate));
        for (size_t i = a; i < b; ++i) st.add (x[i]);
        return st;
    }

    void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
    {
        std::ofstream f (path, std::ios::binary);
        if (! f) { std::printf ("  could not write %s\n", path.c_str()); return; }

        const uint32_t frames = (uint32_t) l.size();
        const uint16_t channels = 2, bits = 16;
        const uint32_t rate = (uint32_t) sampleRate;
        const uint32_t dataBytes = frames * channels * (bits / 8);

        auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
        auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };

        f.write ("RIFF", 4); w32 (36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); w32 (16); w16 (1); w16 (channels); w32 (rate);
        w32 (rate * channels * (bits / 8)); w16 ((uint16_t) (channels * (bits / 8))); w16 (bits);
        f.write ("data", 4); w32 (dataBytes);

        float peak = 1.0e-9f;
        for (size_t i = 0; i < l.size(); ++i)
            peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
        const float gain = 0.89f / peak;

        for (size_t i = 0; i < l.size(); ++i)
        {
            const auto toPcm = [gain] (float x) { return (int16_t) std::lround (std::clamp (x * gain, -1.0f, 1.0f) * 32767.0f); };
            const int16_t sl = toPcm (l[i]), sr = toPcm (r[i]);
            f.write ((const char*) &sl, 2);
            f.write ((const char*) &sr, 2);
        }
    }

    /** One plucked note (decaying harmonics, sharp attack) at time t0. */
    void addPluck (std::vector<float>& x, double t0, double freq, double level)
    {
        const auto start = (size_t) (t0 * sampleRate);
        for (size_t i = 0; i < (size_t) (2.0 * sampleRate) && start + i < x.size(); ++i)
        {
            const double t = (double) i / sampleRate;
            const double env = std::exp (-t * 4.0) * std::min (1.0, t * 1000.0);
            double v = 0.0;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (2.0 * 3.14159265358979 * freq * h * t) * std::exp (-t * h * 1.5) / h;
            x[start + i] += (float) (level * env * v);
        }
    }

    std::vector<float> makePluckPhrase (double seconds)
    {
        std::vector<float> x ((size_t) (seconds * sampleRate), 0.0f);
        const double notes[] = { 220.0, 261.63, 329.63, 392.0, 293.66, 220.0 };
        const double times[] = { 0.1, 1.6, 3.1, 4.6, 6.6, 8.6 };
        for (int k = 0; k < 6; ++k)
            addPluck (x, times[k], notes[k], 0.3);
        return x;
    }

    void fft (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * 3.14159265358979 / (double) len;
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    struct Smoothness { double colorationDb, worstPeakDb, resonanceDb, roughnessDb, clickPct, lumpinessDb; };

    /** Coloration: spread of the impulse response's 1/3-octave band levels (100 Hz - 12 kHz).
        Roughness: millisecond-scale jitter of the wet loudness after a pluck (the "grain"). */
    Smoothness measureSmoothness (CascadeEngine& engine, CascadeEngine::Parameters p)
    {
        Smoothness out {};

        // --- coloration from the impulse response (no feedback, no modulation: the static filter)
        {
            auto q = p;
            q.feedback = 0.0f; q.spin = 0.0f;
            engine.reset();
            engine.setParameters (q);
            const size_t n = 1u << 18;
            std::vector<float> in (n, 0.0f), l, r;
            in[0] = 1.0f;
            run (engine, in, l, r);

            std::vector<std::complex<double>> spec (n);
            for (size_t i = 0; i < n; ++i) spec[i] = l[i];
            fft (spec);

            // divide out the engine's deliberate one-pole high cut: only resonances should count
            const double a = 1.0 - std::exp (-2.0 * 3.14159265358979 * CascadeEngine::dampingHz / sampleRate);
            auto dampingPower = [a] (double hz)
            {
                const double wv = 2.0 * 3.14159265358979 * hz / sampleRate;
                return a * a / (1.0 - 2.0 * (1.0 - a) * std::cos (wv) + (1.0 - a) * (1.0 - a));
            };

            std::vector<double> bandsDb;
            for (double fc = 100.0; fc <= 12500.0; fc *= std::pow (2.0, 1.0 / 3.0))
            {
                const auto lo = (size_t) (fc / std::pow (2.0, 1.0 / 6.0) / sampleRate * (double) n);
                const auto hi = (size_t) (fc * std::pow (2.0, 1.0 / 6.0) / sampleRate * (double) n);
                double e = 0.0;
                for (size_t k = lo; k < hi; ++k) e += std::norm (spec[k]) / dampingPower ((double) k * sampleRate / (double) n);
                bandsDb.push_back (10.0 * std::log10 (e / (double) (hi - lo) + 1e-30));
            }
            double mean = 0.0;
            for (auto v : bandsDb) mean += v;
            mean /= (double) bandsDb.size();
            double var = 0.0, worst = 0.0;
            for (auto v : bandsDb) { var += (v - mean) * (v - mean); worst = std::max (worst, v - mean); }
            out.colorationDb = std::sqrt (var / (double) bandsDb.size());
            out.worstPeakDb = worst;

            // resonance: the biggest narrow peak above its neighbourhood (5-band average),
            // which ignores broad, deliberate tilts like the bloom's darker top end
            double resonance = 0.0;
            for (size_t b = 2; b + 2 < bandsDb.size(); ++b)
            {
                double local = 0.0;
                for (size_t k = b - 2; k <= b + 2; ++k) local += bandsDb[k];
                resonance = std::max (resonance, bandsDb[b] - local / 5.0);
            }
            out.resonanceDb = resonance;
        }

        // --- roughness of the cloud after a sharp, unpitched hit (a decaying noise burst).
        // A pitched pluck would make the 2 ms loudness jitter with the waveform itself;
        // with noise, any jitter above the smooth-noise reference is real graininess.
        {
            engine.reset();
            engine.setParameters (p);
            std::vector<float> in ((size_t) (4.0 * sampleRate), 0.0f), l, r;
            std::mt19937 rng (11);
            std::normal_distribution<float> nd (0.0f, 1.0f);
            for (size_t i = 0; i < (size_t) (0.3 * sampleRate); ++i)
                in[i] = 0.3f * nd (rng) * (float) std::exp (-(double) i / (0.03 * sampleRate));
            run (engine, in, l, r);

            const int win = (int) (0.002 * sampleRate);
            std::vector<double> envDb;
            for (size_t pos = (size_t) (0.6 * sampleRate); pos + (size_t) win < (size_t) (3.0 * sampleRate); pos += (size_t) win)
            {
                double e = 0.0;
                for (int k = 0; k < win; ++k) e += (double) l[pos + (size_t) k] * l[pos + (size_t) k];
                envDb.push_back (10.0 * std::log10 (e / win + 1e-20));
            }
            const int half = 50; // ~200 ms moving average
            double sq = 0.0; int count = 0, clicks = 0;
            for (int i = half; i + half < (int) envDb.size(); ++i)
            {
                double avg = 0.0;
                for (int k = -half; k <= half; ++k) avg += envDb[(size_t) (i + k)];
                avg /= (double) (2 * half + 1);
                const double d = envDb[(size_t) i] - avg;
                sq += d * d; ++count;
                if (d > 6.0) ++clicks;
            }
            out.roughnessDb = count > 0 ? std::sqrt (sq / count) : 0.0;
            out.clickPct = count > 0 ? 100.0 * clicks / count : 0.0;
        }

        // --- lumpiness: slow swells and dips (50 ms loudness vs. its 1 s average) after a
        // pitched pluck, i.e. how steady the drone sounds rather than how grainy it is
        {
            engine.reset();
            engine.setParameters (p);
            std::vector<float> in ((size_t) (6.0 * sampleRate), 0.0f), l, r;
            addPluck (in, 0.0, 220.0, 0.3);
            run (engine, in, l, r);

            const int win = (int) (0.05 * sampleRate);
            std::vector<double> envDb;
            for (size_t pos = (size_t) (0.5 * sampleRate); pos + (size_t) win < (size_t) (4.5 * sampleRate); pos += (size_t) win)
            {
                double e = 0.0;
                for (int k = 0; k < win; ++k)
                    e += (double) l[pos + (size_t) k] * l[pos + (size_t) k] + (double) r[pos + (size_t) k] * r[pos + (size_t) k];
                envDb.push_back (10.0 * std::log10 (e / (2 * win) + 1e-20));
            }
            const int half = 10;
            double sq = 0.0; int count = 0;
            for (int i = half; i + half < (int) envDb.size(); ++i)
            {
                double avg = 0.0;
                for (int k = -half; k <= half; ++k) avg += envDb[(size_t) (i + k)];
                avg /= (double) (2 * half + 1);
                sq += (envDb[(size_t) i] - avg) * (envDb[(size_t) i] - avg);
                ++count;
            }
            out.lumpinessDb = count > 0 ? std::sqrt (sq / count) : 0.0;
        }
        return out;
    }

    CascadeEngine::Parameters defaults (int model)
    {
        CascadeEngine::Parameters p;
        // feedback 0.36 = repeat gain 0.6 (repeat gain is sqrt(feedback))
        p.model = model; p.density = 0.0f; p.feedback = 0.36f; p.timeMs = 250.0f; p.spin = 0.3f; p.tone = 0.0f;
        return p;
    }
}

int main (int argc, char** argv)
{
    const std::string wavDir = argc > 1 ? argv[1] : "";
    CascadeEngine engine;
    engine.prepare (sampleRate);

    std::printf ("Decoherence engine test\n\n");

    // ------------------------------------------------------------------ 1. single pluck -> drone
    for (float density : { 0.0f, 0.5f, 1.0f })
    {
        std::printf ("%s One pluck through each model (density %d%%, repeat gain 0.6, time 250 ms)\n",
                     density < 0.25f ? "1)" : density < 0.75f ? "\n1b)" : "\n1c)", (int) (density * 100.0f));
        std::printf ("    model         wet/dry   attack   sustain 2-3s   at 10-14s\n");

        for (int m = 0; m < cw::numModels; ++m)
        {
            auto p = defaults (m);
            p.density = density;
            engine.reset();
            engine.setParameters (p);

            std::vector<float> in ((size_t) (40.0 * sampleRate), 0.0f), l, r;
            addPluck (in, 0.0, 220.0, 0.3);
            run (engine, in, l, r);

            // loudness of the cloud vs the dry pluck: over 5 s at 0%, 15 s at 50%, 30 s at 100%
            // (the long drone keeps building for more than 10 s)
            const double span = density < 0.25f ? 5.0 : density < 0.75f ? 15.0 : 30.0;
            const double wetDry = statsOf (l, r, 0.0, span).rmsDb() - monoStats (in, 0.0, 5.0).rmsDb();
            // how much of the attack survives: peak right after the first echo vs the cloud's RMS
            const double attack = statsOf (l, r, 0.25, 0.35).peakDb() - statsOf (l, r, 0.25, 2.5).rmsDb();
            // does it keep sounding like a drone?
            const double sustain = statsOf (l, r, 2.0, 3.0).rmsDb() - statsOf (l, r, 0.25, 1.0).rmsDb();
            const auto tail = statsOf (l, r, 10.0, 14.0);
            const double tailRel = tail.rmsDb() - statsOf (l, r, 0.0, 5.0).rmsDb();

            std::printf ("    %-12s %6.1f dB %6.1f dB %9.1f dB %10.1f dB\n", cw::getModels()[(size_t) m].name,
                         wetDry, attack, sustain, tailRel);
            check (tail.finite, std::string ("finite: ") + cw::getModels()[(size_t) m].name);
            check (std::abs (wetDry) < 6.0, "cloud level within 6 dB of the dry pluck");

            if (density < 0.25f)
                check (tailRel < -30.0, "dies away within ~10 s at 0% density");
            else if (density > 0.75f)
                check (tailRel > -25.0, "still a strong drone 10-14 s later at 100% density");
        }
    }

    // ------------------------------------------------------------------ 2. density 0 = clean echo
    // (Density -1 is the engine's test-only sparse end: no taps, no diffusion, no bloom)
    std::printf ("\n2) Plain repeat path, feedback 0: one clean echo at Time, unity gain\n");
    {
        auto p = defaults (0);
        p.density = -0.5f; p.feedback = 0.0f; p.timeMs = 250.0f; p.spin = 0.0f; p.tone = 0.0f;
        engine.reset();
        engine.setParameters (p);

        // a 440 Hz burst, centred on the Tone note (where every Tone focus is transparent)
        std::vector<float> in ((size_t) sampleRate, 0.0f), l, r;
        const int burst = (int) (0.05 * sampleRate);
        for (int i = 0; i < burst; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / burst);
            in[(size_t) i] = (float) (0.5 * w * std::sin (2.0 * 3.14159265358979 * 440.0 * i / sampleRate));
        }
        run (engine, in, l, r);

        int bestLag = 0;
        double best = -1.0;
        for (int lag = 0; lag < (int) (0.5 * sampleRate); ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i < burst; i += 2) acc += (double) in[(size_t) i] * l[(size_t) (i + lag)];
            if (acc > best) { best = acc; bestLag = lag; }
        }
        const double ms = 1000.0 * bestLag / sampleRate;
        const double gainDb = monoStats (l, bestLag / sampleRate, (bestLag + burst) / sampleRate).rmsDb()
                            - monoStats (in, 0.0, burst / sampleRate).rmsDb();
        std::printf ("    echo at %.2f ms, level %.2f dB vs. the input\n", ms, gainDb);
        check (std::abs (ms - 250.0) < 0.5, "echo lands at Time");
        check (std::abs (gainDb) < 0.3, "echo has unity gain");
    }

    // ------------------------------------------------------------------ 3. feedback repeats
    std::printf ("\n3) Feedback 25%% = repeat gain 0.5: each repeat half as loud (plain repeat path)\n");
    {
        auto p = defaults (0);
        p.density = -0.5f; p.feedback = 0.25f; p.timeMs = 200.0f; p.spin = 0.0f; p.tone = 0.0f;
        engine.reset();
        engine.setParameters (p);
        std::vector<float> in ((size_t) (2.0 * sampleRate), 0.0f), l, r;
        for (size_t i = 0; i < (size_t) (0.05 * sampleRate); ++i)
            in[i] = (float) std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / sampleRate) * 0.5f;
        run (engine, in, l, r);
        const double r1 = monoStats (l, 0.20, 0.25).rmsDb();
        const double r2 = monoStats (l, 0.40, 0.45).rmsDb();
        const double r3 = monoStats (l, 0.60, 0.65).rmsDb();
        std::printf ("    repeats: %.1f, %.1f, %.1f dB\n", r1, r2, r3);
        check (std::abs ((r1 - r2) - 6.02) < 1.0 && std::abs ((r2 - r3) - 6.02) < 1.0, "each repeat is ~6 dB quieter");
    }

    // ------------------------------------------------------------------ 4. freeze
    std::printf ("\n4) Freeze holds the loop without growing\n");
    {
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        auto p = defaults (0);
        p.feedback = 0.7f;
        engine.reset();
        engine.setParameters (p);

        const int total = (int) (40.0 * sampleRate);
        std::vector<float> in ((size_t) total, 0.0f), l ((size_t) total), r ((size_t) total);
        for (int i = 0; i < (int) (3.0 * sampleRate); ++i) in[(size_t) i] = nd (rng);

        for (int pos = 0; pos < total; pos += blockSize)
        {
            p.freeze = pos >= (int) (2.5 * sampleRate);
            engine.setParameters (p);
            const int len = std::min (blockSize, total - pos);
            engine.process (in.data() + pos, in.data() + pos, l.data() + pos, r.data() + pos, len);
        }

        const auto a = statsOf (l, r, 8.0, 10.0);
        const auto b = statsOf (l, r, 38.0, 40.0);
        std::printf ("    frozen rms at 8-10 s: %.1f dB, at 38-40 s: %.1f dB \n", a.rmsDb(), b.rmsDb());
        check (a.finite && b.finite, "finite while frozen");
        check (b.rmsDb() <= a.rmsDb() + 1.0, "frozen level does not grow");
        check (b.rmsDb() > -50.0, "frozen loop is still ringing after 30 s");
    }

    // ------------------------------------------------------------------ 4b. Time changes
    std::printf ("\n4b) Changing Time doesn't bend the pitch (440 Hz tone, Time 300 -> 700 ms at 2 s)\n");
    {
        auto p = defaults (0);
        p.density = -0.5f; p.feedback = 0.25f; p.timeMs = 300.0f; p.spin = 0.0f; p.tone = 0.0f;
        engine.reset();
        engine.setParameters (p);

        const int total = (int) (4.0 * sampleRate);
        std::vector<float> in ((size_t) total), l ((size_t) total), r ((size_t) total);
        for (int i = 0; i < total; ++i)
            in[(size_t) i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / sampleRate);

        for (int pos = 0; pos < total; pos += blockSize)
        {
            if (pos >= (int) (2.0 * sampleRate))
                p.timeMs = 700.0f;
            engine.setParameters (p);
            const int len = std::min (blockSize, total - pos);
            engine.process (in.data() + pos, in.data() + pos, l.data() + pos, r.data() + pos, len);
        }

        // instantaneous frequency from the spacing of rising zero crossings, 1.9 - 2.6 s
        double worstHz = 0.0, prevCross = -1.0;
        for (int i = (int) (1.9 * sampleRate); i < (int) (2.6 * sampleRate); ++i)
        {
            if (l[(size_t) i - 1] < 0.0f && l[(size_t) i] >= 0.0f)
            {
                const double t = (i - 1) + l[(size_t) i - 1] / (double) (l[(size_t) i - 1] - l[(size_t) i]);
                if (prevCross >= 0.0)
                    worstHz = std::max (worstHz, std::abs (sampleRate / (t - prevCross) - 440.0));
                prevCross = t;
            }
        }
        std::printf ("    largest pitch deviation during the change: %.2f Hz\n", worstHz);
        check (worstHz < 2.0, "no audible pitch bend while Time changes");
    }

    // ------------------------------------------------------------------ 5. worst case
    std::printf ("\n5) Worst case: 100%% feedback, continuous loud noise\n");
    {
        std::mt19937 rng (2);
        std::normal_distribution<float> nd (0.0f, 0.5f);
        for (int m = 0; m < cw::numModels; ++m)
        {
            auto p = defaults (m);
            p.feedback = 1.0f; p.timeMs = 60.0f; p.spin = 1.0f;
            engine.reset();
            engine.setParameters (p);
            std::vector<float> in ((size_t) (20.0 * sampleRate)), l, r;
            for (auto& v : in) v = nd (rng);
            run (engine, in, l, r);
            const auto st = statsOf (l, r, 15.0, 20.0);
            std::printf ("    %-12s peak %6.1f dB  rms %6.1f dB\n", cw::getModels()[(size_t) m].name, st.peakDb(), st.rmsDb());
            check (st.finite && st.peak < 40.0, "bounded (soft-clipped loop)");
        }
    }

    // ------------------------------------------------------------------ 6. parameter abuse
    std::printf ("\n6) Random parameter changes every few blocks for 60 s\n");
    {
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        engine.reset();
        Stats st;
        std::vector<float> in (blockSize), l (blockSize), r (blockSize);
        CascadeEngine::Parameters p;
        for (int b = 0; b < (int) (60.0 * sampleRate) / blockSize; ++b)
        {
            if (b % 8 == 0)
            {
                p.timeMs   = 20.0f + 1980.0f * u (rng);
                p.density  = u (rng);
                p.feedback = u (rng);
                p.spin     = u (rng);
                p.tone     = 2.0f * u (rng) - 1.0f;
                p.model    = (int) (u (rng) * 5.99f);
                p.freeze   = u (rng) < 0.1f;
                engine.setParameters (p);
            }
            for (auto& v : in) v = nd (rng);
            engine.process (in.data(), in.data(), l.data(), r.data(), blockSize);
            for (int i = 0; i < blockSize; ++i) { st.add (l[(size_t) i]); st.add (r[(size_t) i]); }
        }
        std::printf ("    peak %.1f dB, rms %.1f dB\n", st.peakDb(), st.rmsDb());
        check (st.finite, "finite under parameter abuse");
    }

    // ------------------------------------------------------------------ 7. CPU
    std::printf ("\n7) CPU (density 100%%, max spin, 60 s of stereo audio @ 48 kHz)\n");
    {
        auto p = defaults (0);
        p.spin = 1.0f; p.feedback = 0.9f; p.density = 1.0f;
        engine.reset();
        engine.setParameters (p);
        std::mt19937 rng (4);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        std::vector<float> in ((size_t) (60.0 * sampleRate)), l, r;
        for (auto& v : in) v = nd (rng);
        const auto t0 = std::chrono::steady_clock::now();
        run (engine, in, l, r);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("    %.2f s to render 60 s  ->  %.1f%% of one core in real time\n", secs, 100.0 * secs / 60.0);
        check (secs < 30.0, "faster than half real time");
    }

    // ------------------------------------------------------------------ 8. smoothness
    std::printf ("\n8) Smoothness (feedback 60%%, time 250 ms, spin 30%%)\n");
    std::printf ("    model        density coloration  worst peak  resonance  roughness  clicks  lumpiness\n");
    {
        // reference: what plain Gaussian noise scores on the roughness measure
        std::mt19937 rng (5);
        std::normal_distribution<double> nd (0.0, 1.0);
        const int win = (int) (0.002 * sampleRate);
        double sq = 0.0; int count = 0;
        std::vector<double> envDb;
        for (int w = 0; w < 1200; ++w)
        {
            double e = 0.0;
            for (int k = 0; k < win; ++k) { const double v = nd (rng); e += v * v; }
            envDb.push_back (10.0 * std::log10 (e / win));
        }
        double mean = 0.0;
        for (auto v : envDb) mean += v;
        mean /= (double) envDb.size();
        for (auto v : envDb) { sq += (v - mean) * (v - mean); ++count; }
        std::printf ("    (smooth noise reference: roughness %.2f dB)\n", std::sqrt (sq / count));

        for (float density : { 0.0f, 1.0f })
        {
            for (int m = 0; m < cw::numModels; ++m)
            {
                auto p = defaults (m);
                p.density = density;
                const auto s = measureSmoothness (engine, p);
                std::printf ("    %-12s %4d%% %8.2f dB %9.2f dB %8.2f dB %8.2f dB %5.1f %% %7.2f dB\n", cw::getModels()[(size_t) m].name,
                             (int) (density * 100.0f), s.colorationDb, s.worstPeakDb, s.resonanceDb, s.roughnessDb, s.clickPct, s.lumpinessDb);
            }
        }
    }

    // ------------------------------------------------------------------ 9. demos
    if (! wavDir.empty())
    {
        std::printf ("\n8) Rendering demo WAVs (100%% wet) to %s\n", wavDir.c_str());
        struct Demo { const char* file; int model; float density, feedback, timeMs, spin, tone; };
        const Demo demos[] = {
            { "01_Drift_Large_d0.wav",             0, 0.00f, 0.36f, 250.0f, 0.30f,  0.00f },
            { "02_Drift_Large_d100_long_drone.wav",0, 1.00f, 0.36f, 250.0f, 0.30f,  0.00f },
            { "03_Drift_Medium_d50.wav",           1, 0.50f, 0.49f, 300.0f, 0.35f,  0.05f },
            { "04_Drift_Small_d20.wav",            2, 0.20f, 0.42f, 200.0f, 0.30f,  0.10f },
            { "05_Surge_Large_d100.wav",           3, 1.00f, 0.36f, 250.0f, 0.30f,  0.00f },
            { "06_Surge_Medium_d30.wav",           4, 0.30f, 0.49f, 180.0f, 0.40f,  0.10f },
            { "07_Surge_Small_shimmer_d20.wav",    5, 0.20f, 0.42f, 120.0f, 0.45f,  0.45f },
            { "08_Long_time_4s.wav",               0, 0.60f, 0.50f, 4000.0f, 0.30f, -0.10f },
            { "09_Colored_static_d100.wav",        3, 1.00f, 0.85f,  90.0f, 0.55f,  0.10f },
            { "10_Low_hum_dark.wav",               1, 0.20f, 0.90f, 220.0f, 0.20f, -0.40f },
        };

        // a single pluck at 0% and 100% density, to see the shape of one cloud
        {
            std::vector<float> one ((size_t) (45.0 * sampleRate), 0.0f), l, r;
            addPluck (one, 0.1, 220.0, 0.3);
            writeWav (wavDir + "/11_single_pluck_dry.wav", one, one);
            for (float density : { 0.0f, 1.0f })
            {
                auto p = defaults (0);
                p.density = density;
                engine.reset();
                engine.setParameters (p);
                run (engine, one, l, r);
                writeWav (wavDir + (density < 1.0f ? "/11_single_pluck_Drift_Large_d0.wav"
                                                   : "/11_single_pluck_Drift_Large_d100.wav"), l, r);
            }
            std::printf ("    wrote 11_single_pluck_*.wav\n");
        }

        const auto dry = makePluckPhrase (16.0);
        for (const auto& d : demos)
        {
            CascadeEngine::Parameters p;
            p.model = d.model; p.density = d.density; p.feedback = d.feedback;
            p.timeMs = d.timeMs; p.spin = d.spin; p.tone = d.tone;
            engine.reset();
            engine.setParameters (p);
            std::vector<float> l, r;
            run (engine, dry, l, r);
            writeWav (wavDir + "/" + d.file, l, r);
            std::printf ("    wrote %s\n", d.file);
        }

        // the dry phrase too, for comparison
        writeWav (wavDir + "/00_dry_plucks.wav", dry, dry);
        std::printf ("    wrote 00_dry_plucks.wav\n");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
