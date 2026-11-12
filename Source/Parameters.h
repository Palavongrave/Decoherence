#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Models.h"

namespace ParamIDs
{
    inline constexpr const char* input    = "input";
    inline constexpr const char* feedback = "feedback";
    inline constexpr const char* freeze   = "freeze";
    inline constexpr const char* density  = "density";
    inline constexpr const char* time     = "time";
    inline constexpr const char* spin     = "spin";
    inline constexpr const char* model    = "model";
    inline constexpr const char* tone     = "tone";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* width    = "width";
    inline constexpr const char* bypass   = "bypass";
}

/** Short help text shown in the status bar when hovering a control. */
namespace ParamHelp
{
    inline juce::String forId (const juce::String& id)
    {
        if (id == ParamIDs::input)    return "Level feeding the effect. With constant input, keep it low and open it up when needed.";
        if (id == ParamIDs::feedback) return "How long the input delay keeps repeating each sound. 100% = forever.";
        if (id == ParamIDs::freeze)   return "Loop the input delay forever (100% feedback) and shut the input. Rings toward the Tone note.";
        if (id == ParamIDs::density)  return "How far the cloud stretches: 0% is about 6 s from one pluck, 100% a huge drone with a 60 s bloom.";
        if (id == ParamIDs::time)     return "Length of the input delay (20 ms - 20 s): how often a sound is fed into the cloud, and the Freeze loop.";
        if (id == ParamIDs::spin)     return "Pitch drift on every delay: subtle in the lower half, deep and wobbly toward the top.";
        if (id == ParamIDs::model)    return "Cascade shape. Drift models build up slowly, Surge models swell up fast.";
        if (id == ParamIDs::tone)     return "Dark and warm to the left, bright and airy to the right. Centre is neutral.";
        if (id == ParamIDs::mix)      return "Balance between the dry signal and the cloud.";
        if (id == ParamIDs::output)   return "Final output level.";
        if (id == ParamIDs::width)    return "Stereo width of the cloud. 0% is mono, 150% is extra wide.";
        if (id == ParamIDs::bypass)   return "Turn the effect on or off.";
        return {};
    }
}

/** "C4  262 Hz" style text for a MIDI note number (fractional notes allowed). */
inline juce::String noteText (float note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = juce::roundToInt (note);
    const float hz = 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1)
         + "  " + (hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                                : juce::String (hz / 1000.0f, 2) + " kHz");
}

inline juce::StringArray getModelNames()
{
    juce::StringArray names;
    for (const auto& m : cw::getModels())
        names.add (m.name);
    return names;
}
