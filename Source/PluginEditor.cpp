#include "PluginEditor.h"

using namespace juce;

namespace
{
    Rectangle<int> knobBounds (int cx, int cy)
    {
        // 76 px dial centred on (cx, cy) with a 20 px caption underneath
        return { cx - 48, cy - 38, 96, 96 };
    }

    void drawLogo (Graphics& g, Rectangle<float> r, Colour colour)
    {
        // three waves drifting out of phase: coherence coming apart
        for (int k = 0; k < 3; ++k)
        {
            const float y     = r.getY() + 6.0f + (float) k * 11.0f;
            const float phase = (float) k * 1.3f;
            const float amp   = 3.0f + (float) k * 0.7f;

            Path wave;
            for (int i = 0; i <= 26; ++i)
            {
                const float t  = (float) i / 26.0f;
                const float x  = r.getX() + t * r.getWidth();
                const float yy = y + amp * std::sin (MathConstants<float>::twoPi * 1.2f * t + phase);
                if (i == 0)
                    wave.startNewSubPath (x, yy);
                else
                    wave.lineTo (x, yy);
            }

            g.setColour (colour.withAlpha (1.0f - 0.28f * (float) k));
            g.strokePath (wave, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }
}

//==============================================================================
DecoherenceEditor::DecoherenceEditor (DecoherenceProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (ring);
    for (auto* k : { &input, &feedback, &density, &time, &spin, &tone, &mix, &output, &width })
        content.addAndMakeVisible (k);

    content.addAndMakeVisible (modelSelector);
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (freezeButton);
    content.addAndMakeVisible (powerButton);
    content.addAndMakeVisible (echoDisplay);
    content.addAndMakeVisible (statusBar);
    content.addChildComponent (saveOverlay);

    freezeButton.getProperties().set ("paramId", ParamIDs::freeze);
    powerButton.getProperties().set ("paramId", ParamIDs::bypass);
    freezeAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::freeze, freezeButton);
    powerAttachment  = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::bypass, powerButton);

    presetBar.onSaveRequested = [this] { saveOverlay.show (proc.getCurrentPresetName()); };
    saveOverlay.onSave = [this] (const String& name)
    {
        proc.saveUserPreset (name);
        presetBar.refresh();
    };

    content.addMouseListener (this, true);

    // Read the remembered size first: setResizeLimits() below clamps the still-empty editor
    // to its minimum size, which triggers resized() before we've set the real size.
    const int savedWidth = proc.editorWidth;

    setResizable (true, true);
    setResizeLimits (roundToInt (designW * 0.7), roundToInt (designH * 0.7),
                     roundToInt (designW * 1.6), roundToInt (designH * 1.6));
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);

    const int w = savedWidth > 0 ? savedWidth : designW;
    setSize (w, roundToInt (w * (double) designH / (double) designW));
    sizeInitialised = true;

    startTimerHz (30);
}

DecoherenceEditor::~DecoherenceEditor()
{
    stopTimer();
    content.removeMouseListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void DecoherenceEditor::paint (Graphics& g)
{
    g.fillAll (Theme::bgBottom);
}

void DecoherenceEditor::resized()
{
    const float scale = (float) getWidth() / (float) designW;
    content.setTransform (AffineTransform::scale (scale));
    content.setBounds (0, 0, designW, designH);

    if (sizeInitialised)
        proc.editorWidth = getWidth();
}

void DecoherenceEditor::layoutContent()
{
    // header
    presetBar.setBounds (18, 20, 292, 32);
    powerButton.setBounds (818, 17, 38, 38);

    // centre: the big density knob inside its level ring
    ring.setBounds (316, 80, 248, 248);
    density.setBounds (356, 120, 168, 168);
    modelSelector.setBounds (340, 318, 200, 28);

    // left: what goes in
    input.setBounds    (knobBounds (92, 196));
    feedback.setBounds (knobBounds (186, 196));
    freezeButton.setBounds (66, 286, 146, 36);
    time.setBounds     (knobBounds (282, 304));

    // right: what comes out
    tone.setBounds   (knobBounds (694, 196));
    output.setBounds (knobBounds (788, 196));
    spin.setBounds   (knobBounds (598, 304));
    mix.setBounds    (knobBounds (694, 304));
    width.setBounds  (knobBounds (788, 304));

    // bottom
    echoDisplay.setBounds (16, 384, 848, 92);
    statusBar.setBounds   (16, 484, 848, 28);

    saveOverlay.setBounds (0, 0, designW, designH);
}

void DecoherenceEditor::paintContent (Graphics& g)
{
    const auto W = (float) designW, H = (float) designH;

    // ---- background: warm white at the top melting into amber, with faint falling streaks
    g.setGradientFill (ColourGradient::vertical (Theme::bgTop, 0.0f, Theme::bgBottom, H));
    g.fillRect (0.0f, 0.0f, W, H);

    Random rng (1234);
    for (int i = 0; i < 70; ++i)
    {
        const float x   = rng.nextFloat() * W;
        const float y   = rng.nextFloat() * H * 0.9f;
        const float len = 30.0f + rng.nextFloat() * 160.0f;
        g.setGradientFill (ColourGradient::vertical (Theme::accent.withAlpha (0.0f), y,
                                                     Theme::accent.withAlpha (0.04f + 0.05f * rng.nextFloat()), y + len));
        g.fillRect (x, y, 1.0f, len);
    }

    // ---- title, centred at the top: logo + DE|COHERENCE, subtitle underneath
    const auto titleBold  = Theme::font (21.0f, "Semibold", 0.30f);
    const auto titleLight = Theme::font (21.0f, "Light", 0.30f);
    const int boldW  = GlyphArrangement::getStringWidthInt (titleBold, "DE");
    const int lightW = GlyphArrangement::getStringWidthInt (titleLight, "COHERENCE");
    const int logoW  = 26, gap = 10;
    const int groupW = logoW + gap + boldW - 3 + lightW;
    const int x0     = designW / 2 - groupW / 2;
    const int textX  = x0 + logoW + gap;

    drawLogo (g, { (float) x0, 14.0f, (float) logoW, 34.0f }, Theme::accent);

    g.setFont (titleBold);
    g.setColour (Theme::ink);
    g.drawText ("DE", textX, 12, boldW + 4, 26, Justification::centredLeft, false);
    g.setFont (titleLight);
    g.setColour (Theme::accentDeep);
    g.drawText ("COHERENCE", textX + boldW - 3, 12, lightW + 10, 26, Justification::centredLeft, false);

    g.setFont (Theme::font (11.5f, "Regular", 0.18f));
    g.setColour (Theme::inkDim);
    g.drawText ("drone & soundscape reverb", textX, 38, boldW + lightW, 14, Justification::centred, false);

    g.setFont (Theme::font (10.0f, "Semibold", 0.2f));
    g.drawText ("ON", 818, 56, 38, 12, Justification::centred, false);

    // ---- main plate
    const Rectangle<float> plate (16.0f, 72.0f, 848.0f, 300.0f);
    g.setGradientFill (ColourGradient::vertical (Theme::plateTop, plate.getY(), Theme::plateBottom, plate.getBottom()));
    g.fillRoundedRectangle (plate, 12.0f);

    const auto glowColour = lastFrozen ? Theme::freeze : Theme::accent;
    g.setGradientFill (ColourGradient (glowColour.withAlpha (0.16f), 440.0f, 204.0f,
                                       glowColour.withAlpha (0.0f), 440.0f + 190.0f, 204.0f, true));
    g.fillEllipse (250.0f, 14.0f, 380.0f, 380.0f);

    g.setColour (Colours::white.withAlpha (0.7f));
    g.drawHorizontalLine (73, plate.getX() + 14.0f, plate.getRight() - 14.0f);
    g.setColour (Theme::plateEdge);
    g.drawRoundedRectangle (plate.reduced (0.5f), 12.0f, 1.0f);

    // corner screws
    for (auto pt : { Point<float> (30.0f, 86.0f), Point<float> (850.0f, 86.0f),
                     Point<float> (30.0f, 358.0f), Point<float> (850.0f, 358.0f) })
    {
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillEllipse (pt.x - 4.0f, pt.y - 3.0f, 8.0f, 8.0f);
        g.setColour (Theme::knobTop);
        g.fillEllipse (pt.x - 4.0f, pt.y - 4.0f, 8.0f, 8.0f);
        g.setColour (Theme::knobBottom);
        g.drawLine (pt.x - 2.5f, pt.y - 2.5f, pt.x + 2.5f, pt.y + 2.5f, 1.2f);
    }

    // group captions
    g.setFont (Theme::font (10.5f, "Semibold", 0.35f));
    g.setColour (Theme::inkDim);
    g.drawText ("FEED", 44, 112, 190, 14, Justification::centred, false);
    g.drawText ("SHAPE & OUTPUT", 646, 112, 190, 14, Justification::centred, false);
    g.setColour (Theme::plateEdge);
    g.drawHorizontalLine (130, 60.0f, 218.0f);
    g.drawHorizontalLine (130, 662.0f, 820.0f);

    // density readout in the gap at the bottom of the ring
    g.setFont (Theme::font (13.0f, "Semibold", 0.3f));
    g.setColour (lastFrozen ? Theme::freeze : Theme::ink);
    g.drawText (densityText, 330, 289, 220, 20, Justification::centred, false);
}

//==============================================================================
float DecoherenceEditor::rawParam (const char* id) const
{
    return proc.apvts.getRawParameterValue (id)->load();
}

void DecoherenceEditor::timerCallback()
{
    const bool frozen = rawParam (ParamIDs::freeze) > 0.5f;

    if (frozen != lastFrozen)
    {
        lastFrozen = frozen;
        for (auto* k : { &input, &feedback, &density, &time, &spin, &tone, &mix, &output, &width })
            k->setFrozenLook (frozen);
        modelSelector.setFrozenLook (frozen);
        content.repaint();
    }

    // level ring (fast attack, smooth release)
    const float db     = Decibels::gainToDecibels (proc.getWetLevel(), -60.0f);
    const float target = jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
    meterShown = target > meterShown ? target : meterShown + (target - meterShown) * 0.15f;
    ring.setLevel (meterShown, frozen);

    // density readout
    const float densityValue = rawParam (ParamIDs::density);
    const auto text = "DENSITY  " + String (roundToInt (densityValue)) + "%";
    if (text != densityText)
    {
        densityText = text;
        content.repaint (330, 286, 220, 26);
    }

    // echo pattern
    EchoDisplay::Settings s;
    s.model    = roundToInt (rawParam (ParamIDs::model));
    s.density  = densityValue * 0.01f;
    s.timeMs   = rawParam (ParamIDs::time);
    s.feedback = rawParam (ParamIDs::feedback) * 0.01f;
    s.spin     = rawParam (ParamIDs::spin) * 0.01f;
    s.freeze   = frozen;
    echoDisplay.setSettings (s);
    echoDisplay.advanceAnimation (1.0f / 30.0f);

    presetBar.refresh();
    updateStatus();
}

void DecoherenceEditor::updateStatus()
{
    const int modelIndex = jlimit (0, cw::numModels - 1, roundToInt (rawParam (ParamIDs::model)));
    const auto& model = cw::getModels()[(size_t) modelIndex];

    if (auto* h = hovered.getComponent())
    {
        const auto paramId = h->getProperties()["paramId"].toString();

        if (paramId.isNotEmpty())
        {
            if (auto* param = proc.apvts.getParameter (paramId))
            {
                String name  = param->getName (32).toUpperCase();
                String value = param->getCurrentValueAsText();
                const auto label = param->getLabel();
                String help  = ParamHelp::forId (paramId);

                if (paramId == ParamIDs::bypass)
                {
                    name  = "POWER";
                    value = param->getValue() > 0.5f ? "Off (bypassed)" : "On";
                }
                else if (paramId == ParamIDs::model)
                {
                    value = String (modelIndex + 1) + "  " + String (model.name);
                    help  = model.description;
                }
                else if (paramId == ParamIDs::density)
                {
                    const float d = param->convertFrom0to1 (param->getValue()) * 0.01f;
                    const float bloomAmount = cw::CascadeEngine::bloomAmount (d);
                    value = String (roundToInt (d * 100.0f)) + "%  ->  "
                          + String (cw::CascadeEngine::echoCount (modelIndex, d)) + " copies, "
                          + String (cw::CascadeEngine::diffuserCount (modelIndex, d)) + " diffusers";
                    if (bloomAmount > 0.0f)
                        value << ", " << String (cw::CascadeEngine::bloomRt (bloomAmount), 1) << " s bloom";
                }
                else if (label.isNotEmpty())
                {
                    value << " " << label;
                }

                statusBar.setText (name + "   " + value, help);
                return;
            }
        }

        const auto title = h->getProperties()["helpTitle"].toString();
        if (title.isNotEmpty())
        {
            statusBar.setText (title + "   " + proc.getCurrentPresetName(), h->getProperties()["helpText"].toString());
            return;
        }
    }

    statusBar.setText ("MODEL " + String (modelIndex + 1) + "  " + String (model.name).toUpperCase(),
                       model.description);
}

//==============================================================================
Component* DecoherenceEditor::findHelpComponent (Component* c) const
{
    while (c != nullptr && c != &content)
    {
        if (c->getProperties().contains ("paramId") || c->getProperties().contains ("helpTitle"))
            return c;
        c = c->getParentComponent();
    }
    return nullptr;
}

void DecoherenceEditor::mouseEnter (const MouseEvent& e)
{
    if (auto* c = findHelpComponent (e.eventComponent))
        hovered = c;
}

void DecoherenceEditor::mouseExit (const MouseEvent& e)
{
    if (findHelpComponent (e.eventComponent) == hovered.getComponent())
        hovered = nullptr;
}
