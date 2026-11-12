#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    inline float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 1.0f)
            return x;
        return std::copysign (1.0f + std::tanh (a - 1.0f), x);
    }

    const juce::StringArray presetParamIds { ParamIDs::input,  ParamIDs::feedback, ParamIDs::density,
                                             ParamIDs::time,   ParamIDs::spin,     ParamIDs::model,
                                             ParamIDs::tone,   ParamIDs::mix,      ParamIDs::output,
                                             ParamIDs::width };

    /** Converts a value saved by an older version so it keeps sounding (about) the same.
        v2: Density 0-32 delays.  v3: Density 0-100% where only 50-100% stretched the drone.
        v2/v3: Feedback was the repeat gain itself (now repeat gain = sqrt(Feedback)), and
        Spin's depth curve was 4 ms * s^1.5 (now 8 ms * s^2.08).
        v4: Density's bloom reached 30 s (now 60 s), Tone was a focus note (now a tilt). */
    float migrateValue (const juce::String& id, float value, int fromVersion)
    {
        if (fromVersion < 4)
        {
            if (id == ParamIDs::density)
            {
                const float oldPercent = fromVersion <= 2 ? value * 50.0f / 32.0f : value;
                value = juce::jmax (0.0f, (oldPercent - 50.0f) * 2.0f);
            }
            else if (id == ParamIDs::feedback)
            {
                const float g = juce::jlimit (0.0f, 1.0f, value * 0.01f);
                value = 100.0f * g * g;
            }
            else if (id == ParamIDs::spin)
            {
                const float s = juce::jlimit (0.0f, 1.0f, value * 0.01f);
                value = 100.0f * std::pow (0.5f * std::pow (s, 1.5f), 1.0f / 2.08f);
            }
        }

        if (fromVersion < 5)
        {
            if (id == ParamIDs::density)
                value *= std::log (20.0f) / std::log (40.0f);         // same bloom length
            else if (id == ParamIDs::tone)
                value = juce::jlimit (-100.0f, 100.0f, (value - 60.0f) / 36.0f * 100.0f); // C4 = neutral
        }

        return value;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DecoherenceProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percentText = [] (float v, int) { return String (roundToInt (v)); };
    auto dbText      = [] (float v, int) { return String (v, 1); };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::input, 1 }, "Input",
        NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    // Repeat gain = sqrt(Feedback): every position sustains twice as long as a linear knob; 100% = forever.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::feedback, 4 }, "Feedback",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 36.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));

    // How far the cloud stretches: 0% = about 6 s from one pluck, 100% = two very long stages
    // (up to 18 s) and a 60 s bloom.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::density, 5 }, "Density",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 25.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));

    // 20 ms - 20 s on a logarithmic knob, so every part of its travel is equally useful.
    NormalisableRange<float> timeRange (cw::CascadeEngine::minTimeMs, cw::CascadeEngine::maxTimeMs,
        [] (float start, float end, float t) { return start * std::pow (end / start, t); },
        [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
        [] (float start, float end, float v) { return jlimit (start, end, v); });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::time, 4 }, "Time",
        timeRange, 300.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                return v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v / 1000.0f, 2) + " s";
            })
            .withValueFromStringFunction ([] (const String& s)
            {
                const auto t = s.trim().toLowerCase();
                const float v = t.retainCharacters ("0123456789.").getFloatValue();
                return (t.endsWith ("s") && ! t.endsWith ("ms")) ? v * 1000.0f : v;
            })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::spin, 4 }, "Spin",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::model, 2 }, "Model",
        getModelNames(), 0));

    // Tone: a smooth tilt, Dark (-100) .. Neutral (0) .. Bright (+100).
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tone, 5 }, "Tone",
        NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const int r = roundToInt (v);
                if (r == 0)
                    return String ("Neutral");
                return String (r < 0 ? "Dark " : "Bright ") + String (std::abs (r));
            })
            .withValueFromStringFunction ([] (const String& s)
            {
                const auto t = s.trim().toLowerCase();
                float v = t.retainCharacters ("0123456789.").getFloatValue();
                if (t.contains ("dark") || t.startsWith ("-"))
                    v = -v;
                return v;
            })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::width, 1 }, "Width",
        NormalisableRange<float> (0.0f, 150.0f, 0.01f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
DecoherenceProcessor::DecoherenceProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Decoherence", createParameterLayout())
{
    pInput    = apvts.getRawParameterValue (ParamIDs::input);
    pFeedback = apvts.getRawParameterValue (ParamIDs::feedback);
    pFreeze   = apvts.getRawParameterValue (ParamIDs::freeze);
    pDensity  = apvts.getRawParameterValue (ParamIDs::density);
    pTime     = apvts.getRawParameterValue (ParamIDs::time);
    pSpin     = apvts.getRawParameterValue (ParamIDs::spin);
    pModel    = apvts.getRawParameterValue (ParamIDs::model);
    pTone     = apvts.getRawParameterValue (ParamIDs::tone);
    pMix      = apvts.getRawParameterValue (ParamIDs::mix);
    pOutput   = apvts.getRawParameterValue (ParamIDs::output);
    pWidth    = apvts.getRawParameterValue (ParamIDs::width);
    pBypass   = apvts.getRawParameterValue (ParamIDs::bypass);

    currentPresetName = factoryPresets[0].name;
}

juce::AudioProcessorParameter* DecoherenceProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

//==============================================================================
bool DecoherenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void DecoherenceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    wetBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);

    inputGainSm.reset  (sampleRate, 0.05);
    outputGainSm.reset (sampleRate, 0.05);
    mixSm.reset        (sampleRate, 0.05);
    widthSm.reset      (sampleRate, 0.05);
    bypassSm.reset     (sampleRate, 0.03);

    inputGainSm.setCurrentAndTargetValue  (juce::Decibels::decibelsToGain (pInput->load()));
    outputGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    mixSm.setCurrentAndTargetValue        (pMix->load() * 0.01f);
    widthSm.setCurrentAndTargetValue      (pWidth->load() * 0.01f);
    bypassSm.setCurrentAndTargetValue     (pBypass->load() > 0.5f ? 1.0f : 0.0f);

    meterState = 0.0f;
}

void DecoherenceProcessor::reset()
{
    engine.reset();
}

void DecoherenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0 || numOut < 2)
        return;

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize (2, numSamples, false, false, true); // only if the host exceeds its promise

    // ---- parameters -> engine
    cw::CascadeEngine::Parameters p;
    p.timeMs   = pTime->load();
    p.density  = pDensity->load() * 0.01f;
    p.feedback = pFeedback->load() * 0.01f;
    p.spin     = pSpin->load() * 0.01f;
    p.tone     = pTone->load() * 0.01f;
    p.freeze   = pFreeze->load() > 0.5f;
    p.model    = (int) pModel->load();
    engine.setParameters (p);

    inputGainSm.setTargetValue  (juce::Decibels::decibelsToGain (pInput->load()));
    outputGainSm.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    mixSm.setTargetValue        (pMix->load() * 0.01f);
    widthSm.setTargetValue      (pWidth->load() * 0.01f);
    bypassSm.setTargetValue     (pBypass->load() > 0.5f ? 1.0f : 0.0f);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    const float* inL = outL;
    const float* inR = numIn > 1 ? outR : outL; // mono in -> feed both sides

    float* wetL = wetBuffer.getWritePointer (0);
    float* wetR = wetBuffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGainSm.getNextValue();
        wetL[i] = inL[i] * g;
        wetR[i] = inR[i] * g;
    }

    engine.process (wetL, wetR, wetL, wetR, numSamples);

    float peak = 0.0f;
    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = inL[i];
        const float dryR = inR[i];

        // stereo width on the wet signal (mid/side)
        const float w    = widthSm.getNextValue();
        const float mid  = 0.5f * (wetL[i] + wetR[i]);
        const float side = 0.5f * (wetL[i] - wetR[i]) * w;
        const float wl   = softClip (mid + side);
        const float wr   = softClip (mid - side);
        peak = juce::jmax (peak, std::abs (wl), std::abs (wr));

        // equal-power dry/wet
        const float mix  = mixSm.getNextValue();
        const float dryG = std::cos (mix * halfPi);
        const float wetG = std::sin (mix * halfPi);
        const float outG = outputGainSm.getNextValue();

        float l = (dryL * dryG + wl * wetG) * outG;
        float r = (dryR * dryG + wr * wetG) * outG;

        const float byp = bypassSm.getNextValue();
        l += (dryL - l) * byp;
        r += (dryR - r) * byp;

        outL[i] = l;
        outR[i] = r;
    }

    const float decay = std::exp (-(float) numSamples / (0.25f * (float) getSampleRate()));
    meterState = juce::jmax (peak, meterState * decay);
    wetLevel.store (meterState, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* DecoherenceProcessor::createEditor()
{
    return new DecoherenceEditor (*this);
}

//==============================================================================
int DecoherenceProcessor::getNumPrograms()
{
    return (int) factoryPresets.size();
}

void DecoherenceProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, (int) factoryPresets.size()))
    {
        PresetEntry e;
        e.name = factoryPresets[(size_t) index].name;
        e.isFactory = true;
        e.factoryIndex = index;
        loadPreset (e);
    }
}

const juce::String DecoherenceProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) factoryPresets.size()))
        return factoryPresets[(size_t) index].name;
    return {};
}

//==============================================================================
juce::File DecoherenceProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Decoherence")
               .getChildFile ("Presets");
}

juce::Array<DecoherenceProcessor::PresetEntry> DecoherenceProcessor::getAllPresets() const
{
    juce::Array<PresetEntry> list;

    for (int i = 0; i < (int) factoryPresets.size(); ++i)
    {
        PresetEntry e;
        e.name = factoryPresets[(size_t) i].name;
        e.isFactory = true;
        e.factoryIndex = i;
        list.add (e);
    }

    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.dcpreset");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    for (const auto& f : files)
    {
        PresetEntry e;
        e.name = f.getFileNameWithoutExtension();
        e.isFactory = false;
        e.file = f;
        list.add (e);
    }

    return list;
}

void DecoherenceProcessor::applyValue (const juce::String& paramId, float realValue)
{
    if (auto* param = apvts.getParameter (paramId))
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

void DecoherenceProcessor::loadPreset (const PresetEntry& entry)
{
    if (entry.isFactory)
    {
        if (! juce::isPositiveAndBelow (entry.factoryIndex, (int) factoryPresets.size()))
            return;

        const auto& fp = factoryPresets[(size_t) entry.factoryIndex];
        applyValue (ParamIDs::input,    fp.input);
        applyValue (ParamIDs::feedback, fp.feedback);
        applyValue (ParamIDs::density,  fp.density);
        applyValue (ParamIDs::time,     fp.timeMs);
        applyValue (ParamIDs::spin,     fp.spin);
        applyValue (ParamIDs::model,    (float) fp.model);
        applyValue (ParamIDs::tone,     fp.tone);
        applyValue (ParamIDs::mix,      fp.mix);
        applyValue (ParamIDs::output,   fp.output);
        applyValue (ParamIDs::width,    fp.width);
        applyValue (ParamIDs::freeze,   0.0f);

        const bool programChanged = currentProgram != entry.factoryIndex;
        currentProgram = entry.factoryIndex;
        setCurrentPresetName (fp.name);

        if (programChanged)
            updateHostDisplay (ChangeDetails().withProgramChanged (true));
        return;
    }

    auto xml = juce::XmlDocument::parse (entry.file);
    if (xml == nullptr || ! xml->hasTagName ("DecoherencePreset"))
        return;

    // Presets from older versions are converted so they keep their sound.
    const int presetVersion = xml->getIntAttribute ("version", 2);

    for (auto* child : xml->getChildWithTagNameIterator ("Param"))
    {
        const auto id = child->getStringAttribute ("id");
        if (presetParamIds.contains (id))
            applyValue (id, migrateValue (id, (float) child->getDoubleAttribute ("value"), presetVersion));
    }

    applyValue (ParamIDs::freeze, 0.0f);
    setCurrentPresetName (entry.name);
}

bool DecoherenceProcessor::saveUserPreset (const juce::String& name)
{
    const auto cleanName = juce::File::createLegalFileName (name.trim());
    if (cleanName.isEmpty())
        return false;

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    juce::XmlElement root ("DecoherencePreset");
    root.setAttribute ("name", cleanName);
    root.setAttribute ("version", currentStateVersion);

    for (const auto& id : presetParamIds)
    {
        auto* param = root.createNewChildElement ("Param");
        param->setAttribute ("id", id);
        param->setAttribute ("value", (double) apvts.getRawParameterValue (id)->load());
    }

    if (! root.writeTo (folder.getChildFile (cleanName + ".dcpreset")))
        return false;

    setCurrentPresetName (cleanName);
    return true;
}

juce::String DecoherenceProcessor::getCurrentPresetName() const
{
    const juce::SpinLock::ScopedLockType lock (presetNameLock);
    return currentPresetName;
}

void DecoherenceProcessor::setCurrentPresetName (const juce::String& name)
{
    const juce::SpinLock::ScopedLockType lock (presetNameLock);
    currentPresetName = name;
}

//==============================================================================
void DecoherenceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", getCurrentPresetName(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("stateVersion", currentStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DecoherenceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    const int version = (int) tree.getProperty ("stateVersion", 1);

    // State saved by the first engine (reverb network, dark/bright Tone, echo-count Density)
    // doesn't map onto the current parameters, so start from the default preset instead.
    if (version < 2)
    {
        setCurrentProgram (0);
        return;
    }

    // Versions 2 and 3 stored Density, Feedback and Spin on older scales: convert them so the
    // project keeps its sound.
    if (version < currentStateVersion)
        for (auto child : tree)
            if (child.hasType ("PARAM"))
            {
                const auto id = child.getProperty ("id").toString();
                child.setProperty ("value", migrateValue (id, (float) child.getProperty ("value"), version), nullptr);
            }

    setCurrentPresetName (tree.getProperty ("presetName", "Init").toString());
    currentProgram = juce::jlimit (0, (int) factoryPresets.size() - 1, (int) tree.getProperty ("program", 0));
    apvts.replaceState (tree);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecoherenceProcessor();
}
