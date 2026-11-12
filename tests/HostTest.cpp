// Loads the built Decoherence.vst3 through JUCE's VST3 hosting code - the same way a DAW
// would - and exercises it: odd block sizes and sample rates, automation, state save/restore,
// programs, bypass and editor creation.
//
// Usage: HostTest "path/to/Decoherence.vst3"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <random>
#include <cstdio>

using namespace juce;

namespace
{
    int failures = 0;

    void check (bool ok, const String& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    std::unique_ptr<AudioPluginInstance> makeInstance (AudioPluginFormatManager& fm, const PluginDescription& d,
                                                       double sr, int bs)
    {
        String err;
        auto inst = fm.createPluginInstance (d, sr, bs, err);
        if (inst == nullptr)
            std::printf ("    create failed: %s\n", err.toRawUTF8());
        return inst;
    }

    void prepare (AudioPluginInstance& p, double sr, int bs)
    {
        AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (AudioChannelSet::stereo());
        layout.outputBuses.add (AudioChannelSet::stereo());
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (sr, bs);
        p.prepareToPlay (sr, bs);
    }

    struct RunResult { bool finite = true; float peak = 0.0f; double seconds = 0.0; };

    RunResult runAudio (AudioPluginInstance& p, double sr, int bs, double seconds, bool automate, std::mt19937& rng)
    {
        prepare (p, sr, bs);
        AudioBuffer<float> buf (2, bs);
        MidiBuffer midi;
        std::normal_distribution<float> nd (0.0f, 0.25f);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        auto params = p.getParameters();

        RunResult r;
        const int blocks = jmax (1, (int) (seconds * sr / bs));
        const auto t0 = Time::getMillisecondCounterHiRes();

        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < bs; ++i)
                    buf.setSample (ch, i, nd (rng));

            if (automate && params.size() > 0 && b % 4 == 0)
            {
                auto* prm = params[(int) (u (rng) * (float) params.size()) % params.size()];
                if (prm != p.getBypassParameter())
                    prm->setValueNotifyingHost (u (rng));
            }

            p.processBlock (buf, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < bs; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) r.finite = false;
                    else r.peak = jmax (r.peak, std::abs (v));
                }
        }

        r.seconds = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
        p.releaseResources();
        return r;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: HostTest <plugin.vst3>\n");
        return 2;
    }

    ScopedJuceInitialiser_GUI gui;
    const String path = String::fromUTF8 (argv[1]);

    AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<VST3PluginFormat>());

    std::printf ("Decoherence VST3 host test\n\n1) Scan\n");
    OwnedArray<PluginDescription> descs;
    fm.getFormat (0)->findAllTypesForFile (descs, path);
    check (descs.size() == 1, "exactly one plugin class found");
    if (descs.isEmpty())
        return 1;

    const auto desc = *descs[0];
    std::printf ("    %s by %s | category \"%s\" | %d in / %d out | instrument: %s\n",
                 desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(), desc.category.toRawUTF8(),
                 desc.numInputChannels, desc.numOutputChannels, desc.isInstrument ? "yes" : "no");
    check (! desc.isInstrument, "reports itself as an effect");

    std::mt19937 rng (42);

    // ------------------------------------------------------------------
    std::printf ("\n2) Sample rates x block sizes (2 s of noise each, with automation)\n");
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int bs : { 1, 17, 64, 512, 4096 })
        {
            auto inst = makeInstance (fm, desc, sr, bs);
            if (inst == nullptr) { check (false, "instantiate"); continue; }
            const auto r = runAudio (*inst, sr, bs, bs == 1 ? 0.5 : 2.0, true, rng);
            check (r.finite && r.peak < 16.0f,
                   String (sr / 1000.0, 1) + " kHz / block " + String (bs) + "  peak " + String (r.peak, 2));
        }
    }

    // ------------------------------------------------------------------
    std::printf ("\n3) State save / restore\n");
    {
        auto a = makeInstance (fm, desc, 48000.0, 512);
        prepare (*a, 48000.0, 512);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        // Pick a program first (it loads a whole preset), then randomise the real parameters.
        a->setCurrentProgram (9);
        for (auto* prm : a->getParameters())
            if (prm != a->getBypassParameter() && prm->getName (32) != "Program")
                prm->setValueNotifyingHost (u (rng));

        MemoryBlock state;
        a->getStateInformation (state);
        check (state.getSize() > 0, "state is not empty (" + String ((int) state.getSize()) + " bytes)");

        auto b = makeInstance (fm, desc, 48000.0, 512);
        prepare (*b, 48000.0, 512);
        b->setStateInformation (state.getData(), (int) state.getSize());

        int mismatches = 0;
        auto pa = a->getParameters(), pb = b->getParameters();
        check (pa.size() == pb.size() && pa.size() >= 12, "parameter count " + String (pa.size()));
        for (int i = 0; i < jmin (pa.size(), pb.size()); ++i)
        {
            // Discrete parameters (switches, choices) snap the host's raw value to a step,
            // so compare what they display rather than the raw normalised number.
            const bool same = pa[i]->isDiscrete() || pa[i]->isBoolean()
                                ? pa[i]->getCurrentValueAsText() == pb[i]->getCurrentValueAsText()
                                : std::abs (pa[i]->getValue() - pb[i]->getValue()) <= 1.0e-3f;
            if (! same)
            {
                ++mismatches;
                std::printf ("    mismatch %s: %s vs %s\n", pa[i]->getName (64).toRawUTF8(),
                             pa[i]->getCurrentValueAsText().toRawUTF8(), pb[i]->getCurrentValueAsText().toRawUTF8());
            }
        }
        check (mismatches == 0, "all parameters restored");

        std::printf ("    parameters: ");
        for (auto* prm : pa)
            std::printf ("%s=%s%s  ", prm->getName (32).toRawUTF8(), prm->getCurrentValueAsText().toRawUTF8(),
                         prm->getLabel().isNotEmpty() ? (" " + prm->getLabel()).toRawUTF8() : "");
        std::printf ("\n");
    }

    // ------------------------------------------------------------------
    std::printf ("\n4) Factory programs\n");
    {
        auto inst = makeInstance (fm, desc, 48000.0, 512);
        prepare (*inst, 48000.0, 512);
        const int n = inst->getNumPrograms();
        check (n == 14, "14 factory programs (" + String (n) + ")");
        StringArray names;
        for (int i = 0; i < n; ++i)
        {
            inst->setCurrentProgram (i);
            names.add (inst->getProgramName (i));
        }
        std::printf ("    %s\n", names.joinIntoString (", ").toRawUTF8());
        check (! names.contains ({}), "every program has a name");
    }

    // ------------------------------------------------------------------
    std::printf ("\n5) Bypass passes the input through untouched\n");
    {
        auto inst = makeInstance (fm, desc, 48000.0, 256);
        prepare (*inst, 48000.0, 256);
        auto* byp = inst->getBypassParameter();
        check (byp != nullptr, "plugin exposes a bypass parameter");
        if (byp != nullptr)
        {
            byp->setValueNotifyingHost (1.0f);
            AudioBuffer<float> buf (2, 256);
            MidiBuffer midi;
            std::normal_distribution<float> nd (0.0f, 0.3f);
            float maxDiff = 0.0f;
            for (int b = 0; b < 200; ++b)
            {
                AudioBuffer<float> in (2, 256);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        in.setSample (ch, i, nd (rng));
                buf.makeCopyOf (in);
                inst->processBlock (buf, midi);
                if (b > 20)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 256; ++i)
                            maxDiff = jmax (maxDiff, std::abs (buf.getSample (ch, i) - in.getSample (ch, i)));
            }
            check (maxDiff < 1.0e-6f, "bypassed output == input (max diff " + String (maxDiff, 8) + ")");
        }
        inst->releaseResources();
    }

    // ------------------------------------------------------------------
    std::printf ("\n6) Editor\n");
    {
        auto inst = makeInstance (fm, desc, 48000.0, 512);
        prepare (*inst, 48000.0, 512);
        std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr, "editor created");
        if (ed != nullptr)
            std::printf ("    editor size %d x %d\n", ed->getWidth(), ed->getHeight());
        ed.reset();
        inst->releaseResources();
    }

    // ------------------------------------------------------------------
    std::printf ("\n7) CPU through the VST3 (30 s @ 48 kHz / 512)\n");
    for (int program : { 0, 6, 11 })
    {
        auto inst = makeInstance (fm, desc, 48000.0, 512);
        inst->setCurrentProgram (program);
        const auto r = runAudio (*inst, 48000.0, 512, 30.0, false, rng);
        std::printf ("    %-22s %5.1f%% of one core\n", inst->getProgramName (program).toRawUTF8(), 100.0 * r.seconds / 30.0);
        check (r.finite, "finite output");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
