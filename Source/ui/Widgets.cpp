#include "Widgets.h"

using namespace juce;

namespace
{
    constexpr float pi         = MathConstants<float>::pi;
    constexpr float knobStart  = MathConstants<float>::pi * 1.25f;
    constexpr float knobEnd    = MathConstants<float>::pi * 2.75f;
}

//==============================================================================
// SmoothSlider
//==============================================================================
void SmoothSlider::valueChanged()
{
    if (shown < 0.0f)
        shown = (float) valueToProportionOfLength (getValue());

    if (! isTimerRunning())
        startTimerHz (60);
}

void SmoothSlider::timerCallback()
{
    const float target = (float) valueToProportionOfLength (getValue());

    if (std::abs (target - shown) < 0.0008f)
    {
        // arrived: let the trail fade out one step per frame, then stop
        shown = target;
        if (--trailCount <= 0)
        {
            trailCount = 0;
            stopTimer();
        }
    }
    else
    {
        // remember where the pointer was, for the fading trail
        for (int i = trailLength - 1; i > 0; --i)
            trail[i] = trail[i - 1];
        trail[0] = shown;
        trailCount = jmin (trailCount + 1, trailLength);

        shown += (target - shown) * 0.28f;   // glide: settles in roughly 100 ms
    }

    repaint();
}

void SmoothSlider::paint (Graphics& g)
{
    if (shown < 0.0f)
        shown = (float) valueToProportionOfLength (getValue());

    const auto rp = getRotaryParameters();
    getLookAndFeel().drawRotarySlider (g, 0, 0, getWidth(), getHeight(), shown,
                                       rp.startAngleRadians, rp.endAngleRadians, *this);
}

//==============================================================================
// Knob
//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId,
            const String& cap, bool isBig, bool bipolar)
    : caption (cap), big (isBig)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (knobStart, knobEnd, true);
    slider.setMouseDragSensitivity (big ? 600 : 440);   // a bit of resistance
    slider.getProperties().set ("big", big);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

    getProperties().set ("paramId", paramId);
}

void Knob::paint (Graphics& g)
{
    if (big || caption.isEmpty())
        return;

    g.setFont (Theme::font (12.5f, "Semibold", 0.2f));
    g.setColour (Theme::ink);
    g.drawText (caption, getLocalBounds().removeFromBottom (20), Justification::centred, false);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    if (! big)
        r.removeFromBottom (20);
    slider.setBounds (r);
}

void Knob::setFrozenLook (bool frozen)
{
    slider.getProperties().set ("frozen", frozen);
    slider.repaint();
}

//==============================================================================
// DensityRing
//==============================================================================
void DensityRing::setLevel (float newLevel01, bool isFrozen)
{
    newLevel01 = jlimit (0.0f, 1.0f, newLevel01);
    if (std::abs (newLevel01 - level) > 0.002f || isFrozen != frozen)
    {
        level = newLevel01;
        frozen = isFrozen;
        repaint();
    }
}

void DensityRing::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float rOuter    = jmin (b.getWidth(), b.getHeight()) * 0.5f - 5.0f;
    const float thickness = 12.0f;
    const float rMid      = rOuter - thickness * 0.5f;
    const int   segs      = 46;
    const float step      = (knobEnd - knobStart) / (float) segs;

    const auto acc  = frozen ? Theme::freeze : Theme::accent;
    const auto deep = frozen ? Theme::freeze.darker (0.7f) : Theme::accentDeep;

    Path hairline;
    hairline.addCentredArc (c.x, c.y, rOuter + 3.0f, rOuter + 3.0f, 0.0f, knobStart, knobEnd, true);
    g.setColour (Theme::plateEdge);
    g.strokePath (hairline, PathStrokeType (1.0f));

    const float lit = level * (float) segs;

    for (int k = 0; k < segs; ++k)
    {
        const float a0 = knobStart + ((float) k + 0.14f) * step;
        const float a1 = knobStart + ((float) k + 0.86f) * step;

        Path seg;
        seg.addCentredArc (c.x, c.y, rMid, rMid, 0.0f, a0, a1, true);

        const float amt = jlimit (0.0f, 1.0f, lit - (float) k);
        const float t   = (float) k / (float) (segs - 1);
        // burnt amber at the bottom of the scale, bright amber toward the top
        const auto on   = deep.interpolatedWith (acc, jmin (1.0f, t * 1.4f))
                              .interpolatedWith (acc.brighter (0.25f), jmax (0.0f, t - 0.8f) * 2.5f);

        if (amt > 0.0f)
        {
            g.setColour (on.withAlpha (0.13f * amt));
            g.strokePath (seg, PathStrokeType (thickness + 9.0f, PathStrokeType::mitered, PathStrokeType::butt));
        }

        g.setColour (Theme::track.interpolatedWith (on, amt));
        g.strokePath (seg, PathStrokeType (thickness, PathStrokeType::mitered, PathStrokeType::butt));
    }
}

//==============================================================================
// EchoDisplay
//==============================================================================
void EchoDisplay::setSettings (const Settings& s)
{
    if (valid && s == settings)
        return;

    settings = s;
    dirty = true;
}

float EchoDisplay::targetWindow() const
{
    using Engine = cw::CascadeEngine;
    const auto& m = cw::getModels()[(size_t) jlimit (0, cw::numModels - 1, settings.model)];

    const float units  = Engine::unitsFor (settings.density);
    const float bloomE = Engine::bloomAmount (settings.density);
    const float stageC = Engine::unitWeight (units, 32.0f, 32.0f) * m.longSpanSec * Engine::stageCScale;
    const float stageD = Engine::unitWeight (units, 64.0f, 32.0f) * m.longSpanSec * Engine::stageDScale;
    const float cloud  = (m.longSpanSec + m.midSpanSec + stageC + stageD) * 1.07f + 0.5f * m.diffusionScale;

    // The time axis follows the length of the cloud (model and Density), not Time, so turning
    // Time just slides the pattern along. It only grows when Time itself gets very long.
    const float base = cloud + 0.3f * bloomE * Engine::bloomRt (bloomE) + 1.0f;
    return jlimit (2.0f, 60.0f, jmax (base, settings.timeMs * 0.001f + 0.6f * cloud));
}

void EchoDisplay::advanceAnimation (float seconds)
{
    // glide the time axis to its new length instead of jumping
    const float target = targetWindow();
    if (! valid)
    {
        windowSec = target;
    }
    else if (std::abs (target - windowSec) > 0.002f * target)
    {
        windowSec += (target - windowSec) * 0.18f;
        dirty = true;
    }
    else if (windowSec != target)
    {
        windowSec = target;
        dirty = true;
    }

    if (dirty)
    {
        recompute();
        dirty = false;
    }

    // morph the bars toward their new shape
    bool moving = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (size_t i = 0; i < level[ch].size(); ++i)
        {
            const float diff = levelTarget[ch][i] - level[ch][i];
            if (std::abs (diff) > 0.002f)
            {
                level[ch][i] += diff * 0.3f;
                moving = true;
            }
            else
            {
                level[ch][i] = levelTarget[ch][i];
            }
        }
    }

    phase += seconds * (0.6f + 2.2f * settings.spin);
    if (moving || settings.spin > 0.005f)
        repaint();
}

void EchoDisplay::recompute()
{
    using Engine = cw::CascadeEngine;

    const int mi  = jlimit (0, cw::numModels - 1, settings.model);
    const auto& m = cw::getModels()[(size_t) mi];
    const float T = settings.timeMs * 0.001f;

    const float units   = Engine::unitsFor (settings.density);
    const float bloomE  = Engine::bloomAmount (settings.density);
    const float bloomRt = Engine::bloomRt (bloomE);
    const float stageC  = Engine::unitWeight (units, 32.0f, 32.0f) * m.longSpanSec * Engine::stageCScale;
    const float stageD  = Engine::unitWeight (units, 64.0f, 32.0f) * m.longSpanSec * Engine::stageDScale;
    const float cloud   = (m.longSpanSec + m.midSpanSec + stageC + stageD) * 1.07f + 0.5f * m.diffusionScale;

    earlyEndSec = T + cloud;

    const float loopGain = settings.freeze ? 1.0f : jlimit (0.0f, 1.0f, settings.feedback);
    const float binSec   = windowSec / (float) numBins;

    // how the diffuser smears a single click, simulated with the engine's own sections
    std::vector<float> smear;
    Engine::diffuserEnergy (mi, settings.density, binSec, numBins, smear);

    // the bloom: energy decaying by 60 dB over its RT60
    const float bloomKeep = std::exp (-13.8155f * binSec / bloomRt);
    const float cascG2    = Engine::cascadeGain (bloomE) * Engine::cascadeGain (bloomE);
    const float bloomG2   = Engine::bloomGain (bloomE) * Engine::bloomGain (bloomE);

    std::vector<float> energy ((size_t) numBins), total[2];
    float maxE = 1.0e-12f;

    struct Echo { float t, g; };

    for (int ch = 0; ch < 2; ++ch)
    {
        Engine::TapLayout layout;
        Engine::computeTapLayout (mi, ch, layout);

        // each tap stage: its direct path plus the switched-on taps, with the engine's gains
        std::vector<Echo> stage[Engine::numTapStages];

        for (int s = 0; s < Engine::numTapStages; ++s)
        {
            const float norm = Engine::stageGainNorm (layout, s, units);
            stage[s].push_back ({ 0.0f, norm });

            for (int i = 0; i < Engine::tapsPerStage; ++i)
            {
                const float w = Engine::unitWeight (units, layout.order[s][i], layout.orderWidth[s]);
                if (w > 0.0f)
                    stage[s].push_back ({ layout.delaySec[s][i], w * layout.shape[s][i] * norm });
            }
        }

        // every path through the tap stages (up to 9^4 = 6561) after the first repeat (Time),
        // collected into bins, then smeared once by the diffuser's response
        std::vector<float> arrivals ((size_t) numBins, 0.0f);

        for (const auto& a : stage[0])
            for (const auto& b : stage[1])
                for (const auto& c : stage[2])
                    for (const auto& d : stage[3])
                    {
                        const int bin = (int) ((T + a.t + b.t + c.t + d.t) / binSec);
                        if (bin < numBins)
                        {
                            const float g = a.g * b.g * c.g * d.g;
                            arrivals[(size_t) bin] += g * g;
                        }
                    }

        std::fill (energy.begin(), energy.end(), 0.0f);
        for (int i = 0; i < numBins; ++i)
        {
            const float e = arrivals[(size_t) i];
            if (e <= 0.0f)
                continue;
            for (int j = 0; i + j < numBins; ++j)
                energy[(size_t) (i + j)] += e * smear[(size_t) j];
        }

        // cascade + bloom (the bloom spreads the same energy out along its decay)
        if (bloomE > 0.0f)
        {
            float acc = 0.0f;
            for (int j = 0; j < numBins; ++j)
            {
                acc = acc * bloomKeep + energy[(size_t) j] * (1.0f - bloomKeep);
                energy[(size_t) j] = energy[(size_t) j] * cascG2 + acc * bloomG2;
            }
        }

        // further repeats of the input delay, each one Time later and Feedback quieter
        auto& tot = total[ch];
        tot = energy;
        float amp2 = loopGain * loopGain;

        for (int k = 1; k < 400; ++k)
        {
            const int shiftBins = roundToInt ((float) k * T / binSec);
            if (shiftBins >= numBins || amp2 < 1.0e-7f)
                break;

            for (int j = 0; j + shiftBins < numBins; ++j)
                tot[(size_t) (j + shiftBins)] += energy[(size_t) j] * amp2;

            amp2 *= loopGain * loopGain;
        }

        for (auto v : tot)
            maxE = jmax (maxE, v);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        levelTarget[ch].resize ((size_t) numBins);
        for (int i = 0; i < numBins; ++i)
        {
            const float db = 10.0f * std::log10 (total[ch][(size_t) i] / maxE + 1.0e-12f);
            levelTarget[ch][(size_t) i] = jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f);
        }

        if (level[ch].size() != levelTarget[ch].size())
            level[ch] = levelTarget[ch];   // first time: no morph
    }

    valid = true;
}

void EchoDisplay::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto acc  = settings.freeze ? Theme::freeze : Theme::accent;
    const auto deep = settings.freeze ? Theme::freeze.darker (0.6f) : Theme::accentDeep;

    g.setColour (Theme::screen);
    g.fillRoundedRectangle (b, 8.0f);
    g.setGradientFill (ColourGradient::vertical (Colours::white.withAlpha (0.035f), b.getY(),
                                                 Colours::transparentWhite, b.getY() + b.getHeight() * 0.5f));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (Theme::screenEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    const auto plot  = b.reduced (16.0f, 14.0f);
    const float midY = plot.getCentreY();
    const float halfH = plot.getHeight() * 0.5f;

    // grid: a steady seconds grid (step chosen for 4-10 lines), so it never flips while turning Time
    const float T = settings.timeMs * 0.001f;
    float gridStep = 10.0f;
    for (float step : { 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f })
        if (windowSec / step <= 10.0f) { gridStep = step; break; }

    const int numGrid = (int) (windowSec / gridStep);
    g.setFont (Theme::font (10.0f, "Semibold", 0.1f));

    const float dashes[] = { 2.0f, 4.0f };
    for (int k = 1; k <= numGrid; ++k)
    {
        const float x = plot.getX() + (float) k * gridStep / windowSec * plot.getWidth();
        g.setColour (Theme::screenEdge.withAlpha (0.8f));
        g.drawDashedLine (Line<float> (x, plot.getY() + 4.0f, x, plot.getBottom()), dashes, 2, 1.0f);
        g.setColour (Theme::textDim.withAlpha (0.8f));
        g.drawText (String ((float) k * gridStep, gridStep < 1.0f ? 2 : 0) + " s",
                    Rectangle<float> (x + 3.0f, b.getY() + 2.0f, 40.0f, 12.0f), Justification::centredLeft);
    }

    g.setColour (Theme::screenEdge);
    g.drawHorizontalLine (roundToInt (midY), plot.getX(), plot.getRight());

    // Time marker: where the first repeat enters the cloud
    if (T < windowSec)
    {
        const float x = plot.getX() + T / windowSec * plot.getWidth();
        Path marker;
        marker.addTriangle (x - 4.0f, b.getBottom() - 2.0f, x + 4.0f, b.getBottom() - 2.0f, x, b.getBottom() - 8.0f);
        g.setColour (acc);
        g.fillPath (marker);
        g.drawText ("T", Rectangle<float> (x + 5.0f, b.getBottom() - 14.0f, 12.0f, 12.0f), Justification::centredLeft);
    }

    g.setColour (Theme::textDim);
    g.drawText ("ECHO PATTERN", Rectangle<float> (b.getX() + 10.0f, b.getY() + 2.0f, 120.0f, 12.0f), Justification::centredLeft);
    g.drawText (String (windowSec, 1) + " s", Rectangle<float> (b.getRight() - 70.0f, b.getBottom() - 14.0f, 60.0f, 12.0f), Justification::centredRight);

    if (settings.freeze)
    {
        g.setColour (Theme::freeze);
        g.drawText ("FROZEN", Rectangle<float> (b.getRight() - 80.0f, b.getY() + 2.0f, 70.0f, 12.0f), Justification::centredRight);
    }

    if (! valid)
        return;

    Path earlyPath, tailPath;
    const float binW   = plot.getWidth() / (float) numBins;
    const float binSec = windowSec / (float) numBins;
    const float spin   = settings.spin;

    for (int i = 0; i < numBins; ++i)
    {
        const float sway = spin * 0.9f * std::sin (phase * 1.7f + (float) i * 0.23f);
        const float x = plot.getX() + (float) i * binW + sway;
        auto& path = ((float) i * binSec <= earlyEndSec) ? earlyPath : tailPath;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float shimmer = 1.0f + 0.10f * spin * std::sin (phase * 2.3f + (float) i * 0.41f + (float) ch * 1.3f);
            const float h = level[ch][(size_t) i] * halfH * jmin (1.0f, shimmer);
            if (h < 0.4f)
                continue;

            const float w = jmax (1.0f, binW * 0.62f);
            if (ch == 0)
                path.addRectangle (x, midY - h, w, h);
            else
                path.addRectangle (x, midY, w, h);
        }
    }

    ColourGradient early (acc, 0.0f, plot.getY(), acc, 0.0f, plot.getBottom(), false);
    early.addColour (0.5, deep.withAlpha (0.55f));
    g.setGradientFill (early);
    g.fillPath (earlyPath);

    ColourGradient tail (deep.brighter (0.4f).withAlpha (0.9f), 0.0f, plot.getY(),
                         deep.brighter (0.4f).withAlpha (0.9f), 0.0f, plot.getBottom(), false);
    tail.addColour (0.5, deep.withAlpha (0.25f));
    g.setGradientFill (tail);
    g.fillPath (tailPath);
}

//==============================================================================
// ChevronButton
//==============================================================================
void ChevronButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (Theme::screen.brighter (down ? 0.25f : highlighted ? 0.14f : 0.04f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? Theme::accent.withAlpha (0.7f) : Theme::screenEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    const auto c  = r.getCentre();
    const float s = jmin (r.getWidth(), r.getHeight()) * 0.17f;
    const float dir = right ? 1.0f : -1.0f;

    Path p;
    p.startNewSubPath (c.x - dir * s * 0.55f, c.y - s);
    p.lineTo          (c.x + dir * s * 0.55f, c.y);
    p.lineTo          (c.x - dir * s * 0.55f, c.y + s);

    g.setColour (highlighted ? Theme::accent : Theme::text.withAlpha (0.85f));
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
// ModelSelector
//==============================================================================
ModelSelector::ModelSelector (RangedAudioParameter& p)
    : param (p),
      attachment (p, [this] (float v) { index = jlimit (0, cw::numModels - 1, roundToInt (v)); repaint(); }, nullptr)
{
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };

    getProperties().set ("paramId", ParamIDs::model);
    setRepaintsOnMouseActivity (true);
    attachment.sendInitialUpdate();
}

void ModelSelector::step (int delta)
{
    const int n = cw::numModels;
    attachment.setValueAsCompleteGesture ((float) ((index + delta + n) % n));
}

void ModelSelector::resized()
{
    auto r = getLocalBounds();
    prev.setBounds (r.removeFromLeft (r.getHeight()));
    next.setBounds (r.removeFromRight (r.getHeight()));
}

void ModelSelector::paint (Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced ((float) getHeight() + 4.0f, 1.0f);
    const auto acc  = frozen ? Theme::freeze : Theme::accent;

    g.setColour (Theme::screen);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (isMouseOver (true) ? acc.withAlpha (0.6f) : Theme::screenEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    const String name = cw::getModels()[(size_t) index].name;
    g.setFont (Theme::font (13.5f, "Semibold", 0.22f));
    g.setColour (acc);
    g.drawText (String (index + 1) + "  " + name.toUpperCase(), area, Justification::centred, false);
}

void ModelSelector::mouseUp (const MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    PopupMenu menu;
    menu.addSectionHeader ("NETWORK MODEL");
    for (int i = 0; i < cw::numModels; ++i)
        menu.addItem (i + 1, String (i + 1) + "   " + cw::getModels()[(size_t) i].name, true, i == index);

    SafePointer<ModelSelector> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->attachment.setValueAsCompleteGesture ((float) (result - 1));
                        });
}

//==============================================================================
// PresetBar
//==============================================================================
PresetBar::PresetBar (DecoherenceProcessor& p) : processor (p)
{
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
    addAndMakeVisible (saveButton);

    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    saveButton.onClick = [this] { if (onSaveRequested) onSaveRequested(); };

    setRepaintsOnMouseActivity (true);
    getProperties().set ("helpTitle", "PRESETS");
    getProperties().set ("helpText", "Click the name to browse, use the arrows to step, SAVE to store your own.");
    refresh();
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    saveButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (6);
    prev.setBounds (r.removeFromLeft (r.getHeight()));
    next.setBounds (r.removeFromRight (r.getHeight()));
    nameArea = r.reduced (4, 0);
}

void PresetBar::paint (Graphics& g)
{
    const auto area = nameArea.toFloat();
    const bool hot = isMouseOver (true) && nameArea.contains (getMouseXYRelative());

    g.setColour (Theme::screen);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (hot ? Theme::accent.withAlpha (0.6f) : Theme::screenEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    g.setFont (Theme::font (14.5f, "Regular", 0.04f));
    g.setColour (Theme::text);
    g.drawText (shownName, area.reduced (24.0f, 0.0f), Justification::centred, true);

    // dropdown caret
    const float cx = area.getRight() - 14.0f, cy = area.getCentreY();
    Path caret;
    caret.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (Theme::textDim);
    g.fillPath (caret);
}

void PresetBar::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasClicked() && nameArea.contains (e.getPosition()))
        showMenu();
}

void PresetBar::refresh()
{
    const auto name = processor.getCurrentPresetName();
    if (name != shownName)
    {
        shownName = name;
        repaint();
    }
}

void PresetBar::step (int delta)
{
    const auto list = processor.getAllPresets();
    if (list.isEmpty())
        return;

    int idx = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].name == shownName) { idx = i; break; }

    const int n = list.size();
    const int target = idx < 0 ? 0 : (idx + delta + n) % n;
    processor.loadPreset (list[target]);
    refresh();
}

void PresetBar::showMenu()
{
    const auto list = processor.getAllPresets();

    PopupMenu menu;
    menu.addSectionHeader ("FACTORY");

    bool userHeader = false;
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& e = list[i];
        if (! e.isFactory && ! userHeader)
        {
            menu.addSeparator();
            menu.addSectionHeader ("USER");
            userHeader = true;
        }
        menu.addItem (i + 1, e.name, true, e.name == shownName);
    }

    if (! userHeader)
    {
        menu.addSeparator();
        menu.addSectionHeader ("USER");
        menu.addItem (9999, "(none saved yet)", false);
    }

    menu.addSeparator();
    menu.addItem (10001, "Save preset...");
    menu.addItem (10002, "Open presets folder");

    SafePointer<PresetBar> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe, list] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result == 10001)
                            {
                                if (safe->onSaveRequested)
                                    safe->onSaveRequested();
                                return;
                            }

                            if (result == 10002)
                            {
                                auto folder = DecoherenceProcessor::getUserPresetFolder();
                                folder.createDirectory();
                                folder.startAsProcess();
                                return;
                            }

                            if (isPositiveAndBelow (result - 1, list.size()))
                            {
                                safe->processor.loadPreset (list[result - 1]);
                                safe->refresh();
                            }
                        });
}

//==============================================================================
// FreezeButton
//==============================================================================
void FreezeButton::paintButton (Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState();
    const float corner = r.getHeight() * 0.5f;

    if (on)
    {
        g.setColour (Theme::freeze.withAlpha (0.10f));
        g.fillRoundedRectangle (r.expanded (1.0f), corner);
    }

    g.setColour (on ? Theme::freeze.withAlpha (0.14f) : Theme::screen.brighter (highlighted ? 0.12f : 0.04f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (on ? Theme::freeze : (highlighted ? Theme::accent.withAlpha (0.7f) : Theme::screenEdge));
    g.drawRoundedRectangle (r, corner, on ? 1.6f : 1.0f);

    const float d = r.getHeight() * 0.32f;
    const auto led = Rectangle<float> (d, d).withCentre ({ r.getX() + r.getHeight() * 0.62f, r.getCentreY() });

    if (on)
    {
        g.setColour (Theme::freeze.withAlpha (0.25f));
        g.fillEllipse (led.expanded (d * 0.7f));
    }

    g.setColour (on ? Theme::freeze : Theme::screenEdge.brighter (0.4f));
    g.fillEllipse (led);

    g.setFont (Theme::font (14.0f, "Semibold", 0.32f));
    g.setColour (on ? Theme::freeze : Theme::text.withAlpha (0.85f));
    g.drawText ("FREEZE", r.withTrimmedLeft (r.getHeight() * 0.8f), Justification::centred, false);
}

//==============================================================================
// PowerButton
//==============================================================================
void PowerButton::paintButton (Graphics& g, bool highlighted, bool)
{
    const bool on = ! getToggleState(); // parameter is "bypass"
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float d = jmin (r.getWidth(), r.getHeight());
    const auto circle = Rectangle<float> (d, d).withCentre (r.getCentre());

    g.setColour (Theme::screen.brighter (highlighted ? 0.12f : 0.04f));
    g.fillEllipse (circle);
    g.setColour (on ? Theme::accent : Theme::screenEdge);
    g.drawEllipse (circle.reduced (0.5f), 1.2f);

    const auto c   = circle.getCentre();
    const float ri = d * 0.24f;

    Path icon;
    icon.addCentredArc (c.x, c.y, ri, ri, 0.0f, 0.75f, MathConstants<float>::twoPi - 0.75f, true);
    icon.startNewSubPath (c.x, c.y - ri * 1.3f);
    icon.lineTo (c.x, c.y - ri * 0.2f);

    if (on)
    {
        g.setColour (Theme::accent.withAlpha (0.3f));
        g.strokePath (icon, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setColour (on ? Theme::accent : Theme::textDim);
    g.strokePath (icon, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
// StatusBar
//==============================================================================
void StatusBar::setText (const String& left, const String& right)
{
    if (left != leftText || right != rightText)
    {
        leftText = left;
        rightText = right;
        repaint();
    }
}

void StatusBar::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::screen.withAlpha (0.9f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Theme::screenEdge.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto inner = getLocalBounds().reduced (14, 0);

    const auto leftFont = Theme::font (13.0f, "Semibold", 0.14f);
    const int leftW = jmin (inner.getWidth() / 2, GlyphArrangement::getStringWidthInt (leftFont, leftText) + 24);

    g.setFont (leftFont);
    g.setColour (Theme::accent);
    g.drawText (leftText, inner.removeFromLeft (leftW), Justification::centredLeft, true);

    g.setFont (Theme::font (12.5f));
    g.setColour (Theme::textDim.brighter (0.35f));
    g.drawText (rightText, inner, Justification::centredRight, true);
}

//==============================================================================
// SavePresetOverlay
//==============================================================================
SavePresetOverlay::SavePresetOverlay()
{
    nameEditor.setFont (Theme::font (16.0f));
    nameEditor.setJustification (Justification::centredLeft);
    nameEditor.setIndents (10, 0);
    nameEditor.setInputRestrictions (60);
    nameEditor.onReturnKey = [this] { commit(); };
    nameEditor.onEscapeKey = [this] { dismiss(); };

    saveButton.onClick   = [this] { commit(); };
    cancelButton.onClick = [this] { dismiss(); };

    addAndMakeVisible (nameEditor);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (cancelButton);
    setVisible (false);
}

void SavePresetOverlay::show (const String& suggestedName)
{
    nameEditor.setText (suggestedName, false);
    setVisible (true);
    toFront (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void SavePresetOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.45f));

    const auto p = panel.toFloat();
    g.setGradientFill (ColourGradient::vertical (Theme::plateTop, p.getY(), Theme::plateBottom, p.getBottom()));
    g.fillRoundedRectangle (p, 10.0f);
    g.setColour (Theme::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (p.reduced (0.5f), 10.0f, 1.0f);

    g.setFont (Theme::font (14.0f, "Semibold", 0.25f));
    g.setColour (Theme::ink);
    g.drawText ("SAVE PRESET", panel.withHeight (44).reduced (20, 0), Justification::centredLeft, false);

    g.setFont (Theme::font (11.5f));
    g.setColour (Theme::inkDim);
    g.drawText ("Documents / Decoherence / Presets",
                panel.withTop (panel.getBottom() - 30).reduced (20, 0), Justification::centredLeft, true);
}

void SavePresetOverlay::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (400, 172);

    auto r = panel.reduced (20, 0);
    r.removeFromTop (44);
    nameEditor.setBounds (r.removeFromTop (36));
    r.removeFromTop (14);

    auto buttons = r.removeFromTop (32);
    cancelButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 5));
    buttons.removeFromLeft (10);
    saveButton.setBounds (buttons);
}

void SavePresetOverlay::mouseDown (const MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        dismiss();
}

void SavePresetOverlay::commit()
{
    const auto name = nameEditor.getText().trim();
    if (name.isEmpty())
        return;

    setVisible (false);
    if (onSave)
        onSave (name);
}

void SavePresetOverlay::dismiss()
{
    setVisible (false);
}
