#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    using juce::Colour;

    // Light amber-white front panel with dark knobs and dark "screens"
    inline const Colour bgTop       { 0xfffffaf2 };   // warm white
    inline const Colour bgBottom    { 0xfff2d4a0 };   // soft amber
    inline const Colour plateTop    { 0xfffff7ea };
    inline const Colour plateBottom { 0xfff4ddb6 };
    inline const Colour plateEdge   { 0xffd5ad76 };   // edges and lines on the light panel
    inline const Colour accent      { 0xffe88d1c };   // amber (readable on light and dark)
    inline const Colour accentDeep  { 0xffa5560f };   // burnt amber
    inline const Colour freeze      { 0xff3d9be0 };   // blue, used while frozen
    inline const Colour ink         { 0xff3a2a1b };   // dark lettering on the light panel
    inline const Colour inkDim      { 0xff8a6a4a };
    inline const Colour text        { 0xfffff6ea };   // light lettering on dark surfaces (screens, pointers)
    inline const Colour textDim     { 0xffa8957e };
    inline const Colour track       { 0xffe6cca6 };   // unlit arcs and segments on the light panel
    inline const Colour knobTop     { 0xff4d4239 };   // dark knobs
    inline const Colour knobBottom  { 0xff110d0a };
    inline const Colour screen      { 0xff1e1712 };   // dark displays, buttons and menus
    inline const Colour screenEdge  { 0xff4a3a2b };

    inline juce::String uiFamily()
    {
       #if JUCE_WINDOWS
        return "Segoe UI";
       #elif JUCE_MAC
        return "Avenir Next";
       #else
        return juce::Font::getDefaultSansSerifFontName();
       #endif
    }

    /** style: "Regular", "Light", "Semibold", "Bold" ... */
    inline juce::Font font (float height, const juce::String& style = "Regular", float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions().withName (uiFamily())
                                              .withStyle (style)
                                              .withHeight (height)
                                              .withKerningFactor (kerning));
    }
}
