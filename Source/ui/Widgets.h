#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "Theme.h"
#include "../PluginProcessor.h"

//==============================================================================
/** A rotary slider whose drawn position glides after the real value, leaving a short,
    fading trail of pointer "ghosts" while it moves (a little motion blur). */
class SmoothSlider final : public juce::Slider,
                           private juce::Timer
{
public:
    SmoothSlider() = default;

    void paint (juce::Graphics&) override;
    void valueChanged() override;

    static constexpr int trailLength = 5;
    float shown = -1.0f;                 // drawn position, 0..1 (-1 = not yet initialised)
    float trail[trailLength] {};         // recent drawn positions, newest first
    int   trailCount = 0;

private:
    void timerCallback() override;
};

//==============================================================================
/** Rotary knob with its caption underneath, attached to one parameter. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
          const juce::String& caption, bool big = false, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setFrozenLook (bool frozen);

    SmoothSlider slider;

private:
    juce::String caption;
    bool big;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** Segmented level ring that sits around the big Density knob. */
class DensityRing final : public juce::Component
{
public:
    DensityRing() { setInterceptsMouseClicks (false, false); }
    void setLevel (float newLevel01, bool frozen);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
    bool frozen = false;
};

//==============================================================================
/** Shows the echo pattern the engine is producing: the cascade and its repeats. */
class EchoDisplay final : public juce::Component
{
public:
    struct Settings
    {
        int model = 0;
        float density = 32.0f, timeMs = 250.0f, feedback = 0.0f, spin = 0.0f;
        bool freeze = false;
        bool operator== (const Settings& o) const
        {
            return model == o.model && density == o.density && timeMs == o.timeMs
                && feedback == o.feedback && spin == o.spin && freeze == o.freeze;
        }
        bool operator!= (const Settings& o) const { return ! operator== (o); }
    };

    EchoDisplay() { setInterceptsMouseClicks (false, false); }
    void setSettings (const Settings&);
    void advanceAnimation (float seconds);
    void paint (juce::Graphics&) override;

private:
    void recompute();
    float targetWindow() const;

    static constexpr int numBins = 330;
    Settings settings;
    bool valid = false, dirty = true;
    float windowSec = 4.0f, earlyEndSec = 0.0f, phase = 0.0f;
    std::vector<float> level[2], levelTarget[2]; // 0..1 display height per bin (shown, target)
};

//==============================================================================
/** Small chevron arrow button used by the preset bar and the model selector. */
class ChevronButton final : public juce::Button
{
public:
    explicit ChevronButton (bool pointsRight) : juce::Button (pointsRight ? "next" : "previous"), right (pointsRight) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool right;
};

//==============================================================================
class ModelSelector final : public juce::Component
{
public:
    explicit ModelSelector (juce::RangedAudioParameter& modelParam);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void setFrozenLook (bool f)  { frozen = f; repaint(); }

private:
    void step (int delta);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    ChevronButton prev { false }, next { true };
    int index = 0;
    bool frozen = false;
};

//==============================================================================
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (DecoherenceProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

    std::function<void()> onSaveRequested;

private:
    void step (int delta);
    void showMenu();

    DecoherenceProcessor& processor;
    ChevronButton prev { false }, next { true };
    juce::TextButton saveButton { "SAVE" };
    juce::Rectangle<int> nameArea;
    juce::String shownName;
};

//==============================================================================
class FreezeButton final : public juce::ToggleButton
{
public:
    FreezeButton() : juce::ToggleButton ("FREEZE") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
/** Power LED. The attached parameter is "bypass", so the LED is lit when the toggle is off. */
class PowerButton final : public juce::ToggleButton
{
public:
    PowerButton() : juce::ToggleButton ("ON") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
class StatusBar final : public juce::Component
{
public:
    StatusBar() { setInterceptsMouseClicks (false, false); }
    void setText (const juce::String& left, const juce::String& right);
    void paint (juce::Graphics&) override;

private:
    juce::String leftText, rightText;
};

//==============================================================================
/** In-window "save preset" prompt (no native dialog, works the same in every host). */
class SavePresetOverlay final : public juce::Component
{
public:
    SavePresetOverlay();

    void show (const juce::String& suggestedName);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (const juce::String&)> onSave;

private:
    void commit();
    void dismiss();

    juce::TextEditor nameEditor;
    juce::TextButton saveButton { "SAVE" }, cancelButton { "CANCEL" };
    juce::Rectangle<int> panel;
};
