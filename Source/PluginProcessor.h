#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "dsp/CascadeEngine.h"
#include "Parameters.h"

class DecoherenceProcessor final : public juce::AudioProcessor
{
public:
    DecoherenceProcessor();
    ~DecoherenceProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 60.0; }

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override                    { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Presets (message thread)
    struct PresetEntry
    {
        juce::String name;
        bool isFactory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    juce::Array<PresetEntry> getAllPresets() const;
    void loadPreset (const PresetEntry&);
    bool saveUserPreset (const juce::String& name);
    juce::String getCurrentPresetName() const;
    static juce::File getUserPresetFolder();

    //==============================================================================
    float getWetLevel() const noexcept                  { return wetLevel.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

    // Remembered editor size for this session.
    int editorWidth = 0;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    // 2 = densification engine, 3 = Density in %, 4 = Density always stretches, sqrt Feedback,
    // 2x Spin, 5 = Density reaches 60 s, Tone is a dark/bright tilt
    static constexpr int currentStateVersion = 5;
    void setCurrentPresetName (const juce::String&);
    void applyValue (const juce::String& paramId, float realValue);

    cw::CascadeEngine engine;
    juce::AudioBuffer<float> wetBuffer;

    std::atomic<float>* pInput    = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pFreeze   = nullptr;
    std::atomic<float>* pDensity  = nullptr;
    std::atomic<float>* pTime     = nullptr;
    std::atomic<float>* pSpin     = nullptr;
    std::atomic<float>* pModel    = nullptr;
    std::atomic<float>* pTone     = nullptr;
    std::atomic<float>* pMix      = nullptr;
    std::atomic<float>* pOutput   = nullptr;
    std::atomic<float>* pWidth    = nullptr;
    std::atomic<float>* pBypass   = nullptr;

    juce::SmoothedValue<float> inputGainSm, outputGainSm, mixSm, widthSm, bypassSm;
    std::atomic<float> wetLevel { 0.0f };
    float meterState = 0.0f;

    int currentProgram = 0;
    juce::String currentPresetName { "Init" };
    mutable juce::SpinLock presetNameLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecoherenceProcessor)
};
