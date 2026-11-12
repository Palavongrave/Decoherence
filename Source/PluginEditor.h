#pragma once

#include "PluginProcessor.h"
#include "ui/WaterfallLookAndFeel.h"
#include "ui/Widgets.h"

class DecoherenceEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit DecoherenceEditor (DecoherenceProcessor&);
    ~DecoherenceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr int designW = 880;
    static constexpr int designH = 520;

    /** Everything is laid out at the design size inside this component, which is then scaled. */
    class Content final : public juce::Component
    {
    public:
        explicit Content (DecoherenceEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void resized() override                 { owner.layoutContent(); }

    private:
        DecoherenceEditor& owner;
    };

    void paintContent (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    void updateStatus();
    juce::Component* findHelpComponent (juce::Component*) const;
    float rawParam (const char* id) const;

    DecoherenceProcessor& proc;
    WaterfallLookAndFeel lnf;
    Content content { *this };

    Knob input    { proc.apvts, ParamIDs::input,    "INPUT" };
    Knob feedback { proc.apvts, ParamIDs::feedback, "FEEDBACK" };
    Knob density  { proc.apvts, ParamIDs::density,  "DENSITY", true };
    Knob time     { proc.apvts, ParamIDs::time,     "TIME" };
    Knob spin     { proc.apvts, ParamIDs::spin,     "SPIN" };
    Knob tone     { proc.apvts, ParamIDs::tone,     "TONE", false, true };
    Knob mix      { proc.apvts, ParamIDs::mix,      "MIX" };
    Knob output   { proc.apvts, ParamIDs::output,   "OUTPUT" };
    Knob width    { proc.apvts, ParamIDs::width,    "WIDTH" };

    DensityRing ring;
    ModelSelector modelSelector { *proc.apvts.getParameter (ParamIDs::model) };
    PresetBar presetBar { proc };
    FreezeButton freezeButton;
    PowerButton powerButton;
    EchoDisplay echoDisplay;
    StatusBar statusBar;
    SavePresetOverlay saveOverlay;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment, powerAttachment;

    juce::Component::SafePointer<juce::Component> hovered;
    bool lastFrozen = false;
    bool sizeInitialised = false;
    juce::String densityText;
    float meterShown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecoherenceEditor)
};
