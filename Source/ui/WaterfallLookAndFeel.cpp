#include "WaterfallLookAndFeel.h"
#include "Widgets.h"

using namespace juce;

WaterfallLookAndFeel::WaterfallLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId,            Theme::screen);
    setColour (PopupMenu::textColourId,                  Theme::text);
    setColour (PopupMenu::headerTextColourId,            Theme::accent);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accentDeep.withAlpha (0.55f));
    setColour (PopupMenu::highlightedTextColourId,       Colours::white);

    setColour (TextEditor::backgroundColourId,           Theme::screen);
    setColour (TextEditor::textColourId,                 Theme::text);
    setColour (TextEditor::highlightColourId,            Theme::accentDeep.withAlpha (0.6f));
    setColour (TextEditor::highlightedTextColourId,      Colours::white);
    setColour (TextEditor::outlineColourId,              Theme::screenEdge);
    setColour (TextEditor::focusedOutlineColourId,       Theme::accent);
    setColour (CaretComponent::caretColourId,            Theme::accent);

    setColour (TextButton::buttonColourId,               Theme::track);
    setColour (TextButton::textColourOffId,              Theme::text);
    setColour (TextButton::textColourOnId,               Theme::accent);
}

//==============================================================================
void WaterfallLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float startAngle, float endAngle, Slider& slider)
{
    const bool big     = slider.getProperties()["big"];
    const bool bipolar = slider.getProperties()["bipolar"];
    const bool frozen  = slider.getProperties()["frozen"];
    const bool hot     = slider.isMouseOverOrDragging();

    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
    const float size  = jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();

    const float arcW  = big ? 4.5f : 3.2f;
    const float rArc  = size * 0.5f - arcW;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto acc    = frozen ? Theme::freeze : Theme::accent;

    // ---- track
    Path track;
    track.addCentredArc (centre.x, centre.y, rArc, rArc, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::track);
    g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    // ---- value arc with soft glow
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.002f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, rArc, rArc, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (acc.withAlpha (hot ? 0.24f : 0.15f));
        g.strokePath (value, PathStrokeType (arcW * 3.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (acc);
        g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ---- knob body
    const float rBody = rArc - arcW - (big ? 11.0f : 6.0f);
    const auto body = Rectangle<float> (rBody * 2.0f, rBody * 2.0f).withCentre (centre);

    g.setColour (Colours::black.withAlpha (0.32f));
    g.fillEllipse (body.translated (0.0f, rBody * 0.12f).expanded (rBody * 0.06f));

    ColourGradient grad (Theme::knobTop.brighter (hot ? 0.12f : 0.0f),
                         centre.x - rBody * 0.35f, centre.y - rBody * 0.55f,
                         Theme::knobBottom, centre.x + rBody * 0.7f, centre.y + rBody * 0.9f, true);
    g.setGradientFill (grad);
    g.fillEllipse (body);

    // fine knurling around the edge
    const int ticks = big ? 72 : 36;
    g.setColour (Colours::white.withAlpha (big ? 0.07f : 0.05f));
    for (int i = 0; i < ticks; ++i)
    {
        const float a = MathConstants<float>::twoPi * (float) i / (float) ticks;
        g.drawLine (Line<float> (centre.getPointOnCircumference (rBody * 0.86f, a),
                                 centre.getPointOnCircumference (rBody * 0.97f, a)), 1.0f);
    }

    // inner cap
    const auto cap = body.reduced (rBody * 0.2f);
    ColourGradient capGrad (Theme::knobTop.darker (0.15f), cap.getX(), cap.getY(),
                            Theme::knobBottom.brighter (0.08f), cap.getRight(), cap.getBottom(), false);
    g.setGradientFill (capGrad);
    g.fillEllipse (cap);

    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (cap, 1.0f);

    // ---- pointer, with a short fading trail while the knob moves (motion blur)
    const float inner = rBody * (big ? 0.30f : 0.22f);
    const float outer = rBody * 0.80f;

    if (auto* smooth = dynamic_cast<SmoothSlider*> (&slider))
    {
        for (int i = smooth->trailCount - 1; i >= 0; --i)
        {
            const float a = startAngle + smooth->trail[i] * (endAngle - startAngle);
            const float fade = 0.22f * (1.0f - (float) i / (float) SmoothSlider::trailLength);
            g.setColour (Theme::text.withAlpha (fade));
            g.drawLine (Line<float> (centre.getPointOnCircumference (inner, a),
                                     centre.getPointOnCircumference (outer, a)), big ? 4.0f : 3.0f);
        }
    }

    const auto p1 = centre.getPointOnCircumference (inner, angle);
    const auto p2 = centre.getPointOnCircumference (outer, angle);
    g.setColour (acc.withAlpha (0.18f));
    g.drawLine (Line<float> (p1, p2), big ? 9.0f : 7.0f);   // soft halo
    g.setColour (acc.withAlpha (0.35f));
    g.drawLine (Line<float> (p1, p2), big ? 6.0f : 4.5f);
    g.setColour (Theme::text);
    g.drawLine (Line<float> (p1, p2), big ? 3.0f : 2.2f);
}

//==============================================================================
void WaterfallLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour&,
                                                 bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = jmin (6.0f, r.getHeight() * 0.5f);

    g.setColour (Theme::screen.brighter (down ? 0.25f : highlighted ? 0.15f : 0.05f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (highlighted ? Theme::accent.withAlpha (0.7f) : Theme::screenEdge);
    g.drawRoundedRectangle (r, corner, 1.0f);
}

void WaterfallLookAndFeel::drawButtonText (Graphics& g, TextButton& button, bool highlighted, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (highlighted ? Theme::accent : Theme::text.withAlpha (button.isEnabled() ? 0.9f : 0.4f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), Justification::centred, false);
}

Font WaterfallLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return Theme::font (jmin (13.0f, (float) buttonHeight * 0.48f), "Semibold", 0.12f);
}

Font WaterfallLookAndFeel::getPopupMenuFont()
{
    return Theme::font (15.0f);
}

void WaterfallLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (Theme::screen);
    g.setColour (Theme::screenEdge);
    g.drawRect (0, 0, width, height, 1);
}

void WaterfallLookAndFeel::fillTextEditorBackground (Graphics& g, int width, int height, TextEditor&)
{
    g.setColour (Theme::screen);
    g.fillRoundedRectangle (Rectangle<float> ((float) width, (float) height), 5.0f);
}

void WaterfallLookAndFeel::drawTextEditorOutline (Graphics& g, int width, int height, TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? Theme::accent : Theme::screenEdge);
    g.drawRoundedRectangle (Rectangle<float> ((float) width, (float) height).reduced (0.5f), 5.0f, 1.0f);
}
