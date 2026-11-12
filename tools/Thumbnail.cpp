// Marketing helper: renders the real Decoherence editor (with audio running through it, so the
// meters and displays are live) and composes a 1280x720 YouTube thumbnail around it.
//
//   DecoherenceThumbnail.exe [outputFolder]      (default: .\marketing)
//
// Writes plugin-ui@2x.png (the editor at 2x) and youtube-thumbnail.png.

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ui/Theme.h"
#include <iostream>

using namespace juce;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    //==============================================================================
    // A few plucked notes so the editor shows a sounding drone
    struct PluckSource
    {
        void fill (AudioBuffer<float>& buffer)
        {
            const float freqs[] = { 220.0f, 329.63f, 440.0f, 554.37f };

            for (int i = 0; i < buffer.getNumSamples(); ++i, ++pos)
            {
                const double t    = (double) (pos % noteEvery) / sampleRate;
                const int note    = (int) (pos / noteEvery) % 4;
                const double env  = std::exp (-t * 5.0);
                const double f    = freqs[note];

                double s = 0.0;
                for (int h = 1; h <= 5; ++h)
                    s += std::sin (MathConstants<double>::twoPi * f * h * t) * std::exp (-t * h * 1.5) / h;

                const auto v = (float) (0.35 * env * s);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
        }

        int64 pos = 0;
        int64 noteEvery = (int64) (sampleRate * 1.4);
    };

    void runAudio (DecoherenceProcessor& proc, PluckSource& src, double seconds)
    {
        AudioBuffer<float> buffer (2, blockSize);
        MidiBuffer midi;

        for (int n = (int) (seconds * sampleRate / blockSize); --n >= 0;)
        {
            src.fill (buffer);
            proc.processBlock (buffer, midi);
        }
    }

    //==============================================================================
    Font font (float height, const String& style, float kerning = 0.0f)
    {
        return Theme::font (height, style, kerning);
    }

    float textWidth (const Font& f, const String& s)
    {
        return GlyphArrangement::getStringWidth (f, s);
    }

    Path textPath (const Font& f, const String& s, float x, float baseline)
    {
        GlyphArrangement ga;
        ga.addLineOfText (f, s, x, baseline);
        Path p;
        ga.createPath (p);
        return p;
    }

    /** Largest font height (<= maxHeight) at which every line fits into maxWidth. */
    float fitHeight (const String& style, float kerning, const StringArray& lines, float maxWidth, float maxHeight)
    {
        float widest = 0.0f;
        for (auto& l : lines)
            widest = jmax (widest, textWidth (font (100.0f, style, kerning), l));

        return jmin (maxHeight, 100.0f * maxWidth / widest);
    }

    void glow (Graphics& g, const Path& p, Colour c, int radius)
    {
        DropShadow (c, radius, {}).drawForPath (g, p);
    }

    //==============================================================================
    // The logo motif: waves that start in phase and drift apart
    void drawDecoheringWaves (Graphics& g, float x0, float x1, float yc, float amp, int count, float alpha)
    {
        for (int k = 0; k < count; ++k)
        {
            const float spread = (float) k - (float) (count - 1) * 0.5f;
            Path wave;

            for (float x = x0; x <= x1; x += 2.0f)
            {
                const float u     = (x - x0) / (x1 - x0);
                const float apart = u * u * (3.0f - 2.0f * u);             // smoothstep: coherent -> decoherent
                const float phase = spread * 1.15f * apart;
                const float a     = amp * (1.0f + 0.22f * spread * apart);
                const float freq  = 0.0105f * (1.0f + 0.05f * spread * apart);
                const float y     = yc + a * std::sin (x * freq + phase) + spread * 6.0f * apart;

                if (x == x0) wave.startNewSubPath (x, y);
                else         wave.lineTo (x, y);
            }

            const float fade = 1.0f - 0.12f * std::abs (spread);
            g.setColour (Theme::accent.withAlpha (alpha * 0.07f * fade));
            g.strokePath (wave, PathStrokeType (12.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Theme::accent.withAlpha (alpha * 0.18f * fade));
            g.strokePath (wave, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colour (0xffffc777).withAlpha (alpha * 0.85f * fade));
            g.strokePath (wave, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    void drawLogo (Graphics& g, Rectangle<float> r, Colour colour, float stroke)
    {
        // same three drifting waves as the editor's title
        const float sx = r.getWidth() / 26.0f, sy = r.getHeight() / 34.0f;

        for (int k = 0; k < 3; ++k)
        {
            const float y     = r.getY() + (6.0f + (float) k * 11.0f) * sy;
            const float phase = (float) k * 1.3f;
            const float amp   = (3.0f + (float) k * 0.7f) * sy;

            Path wave;
            for (int i = 0; i <= 40; ++i)
            {
                const float t  = (float) i / 40.0f;
                const float x  = r.getX() + t * r.getWidth();
                const float yy = y + amp * std::sin (MathConstants<float>::twoPi * 1.2f * t + phase);
                if (i == 0) wave.startNewSubPath (x, yy);
                else        wave.lineTo (x, yy);
            }

            g.setColour (colour.withAlpha (1.0f - 0.28f * (float) k));
            g.strokePath (wave, PathStrokeType (stroke * jmin (sx, sy), PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    //==============================================================================
    Image downsample2x (const Image& src)
    {
        Image dst (Image::ARGB, src.getWidth() / 2, src.getHeight() / 2, false, SoftwareImageType());
        const Image::BitmapData s (src, Image::BitmapData::readOnly);
        Image::BitmapData d (dst, Image::BitmapData::writeOnly);

        for (int y = 0; y < dst.getHeight(); ++y)
            for (int x = 0; x < dst.getWidth(); ++x)
            {
                int acc[4] = {};
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                    {
                        const auto c = s.getPixelColour (x * 2 + dx, y * 2 + dy);
                        acc[0] += c.getRed(); acc[1] += c.getGreen(); acc[2] += c.getBlue(); acc[3] += c.getAlpha();
                    }

                d.setPixelColour (x, y, Colour ((uint8) ((acc[0] + 2) / 4), (uint8) ((acc[1] + 2) / 4),
                                                (uint8) ((acc[2] + 2) / 4), (uint8) ((acc[3] + 2) / 4)));
            }

        return dst;
    }

    void addGrain (Image& img, float amount)
    {
        Image::BitmapData d (img, Image::BitmapData::readWrite);
        Random rng (99);

        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto c = d.getPixelColour (x, y);
                const float n = (rng.nextFloat() - 0.5f) * amount;
                d.setPixelColour (x, y, Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, c.getFloatRed()   + n),
                                                               jlimit (0.0f, 1.0f, c.getFloatGreen() + n),
                                                               jlimit (0.0f, 1.0f, c.getFloatBlue()  + n), 1.0f));
            }
    }

    bool savePng (const Image& img, const File& file)
    {
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (img, out);
    }

    /** The editor painted into an opaque software image (a native snapshot can come back
        with partly transparent pixels, which would let the drop shadow show through). */
    Image renderEditor (Component& editor, float scale)
    {
        Image img (Image::RGB, roundToInt ((float) editor.getWidth() * scale),
                   roundToInt ((float) editor.getHeight() * scale), true, SoftwareImageType());
        {
            Graphics g (img);
            g.addTransform (AffineTransform::scale (scale));
            editor.paintEntireComponent (g, true);
        }
        return img;
    }

    //==============================================================================
    Image composeThumbnail (AudioProcessorEditor& editor)
    {
        constexpr int W = 1280, H = 720, SS = 2; // drawn at 2x, then averaged down
        const Colour cream (0xfffff3e2), amberHi (0xffffb347), dimCream (0xffd8c09e);

        // software image: the Graphics below must be finished before the pixels are read back
        Image img (Image::ARGB, W * SS, H * SS, true, SoftwareImageType());
        {
        Graphics g (img);
        g.addTransform (AffineTransform::scale ((float) SS));
        g.setImageResamplingQuality (Graphics::highResamplingQuality);

        // ---- background: deep warm brown, amber glow behind the plugin, falling streaks
        g.setGradientFill (ColourGradient (Colour (0xff3b1f09), 900.0f, 340.0f, Colour (0xff0b0604), 80.0f, 760.0f, true));
        g.fillAll();

        g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.42f), 905.0f, 370.0f,
                                           Theme::accent.withAlpha (0.0f), 905.0f + 560.0f, 370.0f, true));
        g.fillRect (0, 0, W, H);

        Random rng (1234);
        for (int i = 0; i < 160; ++i)
        {
            const float x   = rng.nextFloat() * (float) W;
            const float y   = rng.nextFloat() * (float) H * 0.95f - 40.0f;
            const float len = 40.0f + rng.nextFloat() * 220.0f;
            g.setGradientFill (ColourGradient::vertical (Theme::accent.withAlpha (0.0f), y,
                                                         Theme::accent.withAlpha (0.05f + 0.10f * rng.nextFloat()), y + len));
            g.fillRect (x, y, 1.2f, len);
        }

        // vignette on the background only, so the plugin and the lettering keep their true colours
        g.setGradientFill (ColourGradient (Colours::transparentBlack, (float) W * 0.5f, (float) H * 0.5f,
                                           Colours::black.withAlpha (0.55f), 0.0f, -60.0f, true));
        g.fillRect (0, 0, W, H);

        drawDecoheringWaves (g, -20.0f, (float) W + 20.0f, 640.0f, 26.0f, 5, 1.0f);

        // ---- the plugin, rendered live at exactly the pixel size it ends up at
        const float plugW  = 650.0f;
        const float plugH  = plugW * (float) editor.getHeight() / (float) editor.getWidth();
        const Point<float> plugCentre (918.0f, 370.0f);
        const float angle  = degreesToRadians (-3.5f);

        const auto snap = renderEditor (editor, plugW * (float) SS / (float) editor.getWidth());

        const auto placement = AffineTransform::translation (-plugW * 0.5f, -plugH * 0.5f)
                                   .rotated (angle)
                                   .translated (plugCentre);

        Path frame;
        frame.addRoundedRectangle (0.0f, 0.0f, plugW, plugH, 14.0f);
        frame.applyTransform (placement);

        glow (g, frame, Theme::accent.withAlpha (0.55f), 70);
        Path shadow (frame);
        shadow.applyTransform (AffineTransform::translation (10.0f, 22.0f));
        glow (g, shadow, Colours::black.withAlpha (0.85f), 46);

        {
            Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (frame);
            g.setOpacity (1.0f); // drawImage uses the current colour's alpha, left at 85% by the shadow
            g.drawImageTransformed (snap, AffineTransform::scale (1.0f / (float) SS).followedBy (placement));
        }

        g.setColour (Colours::white.withAlpha (0.55f));
        g.strokePath (frame, PathStrokeType (1.2f));

        // ---- left column: wordmark, subtitle, badge, hook
        const float left = 58.0f;

        const float markH = fitHeight ("Light", 0.08f, { "DECOHERENCE" }, 560.0f, 76.0f);
        const auto markBold  = font (markH, "Semibold", 0.08f);
        const auto markLight = font (markH, "Light", 0.08f);
        const float logoW = markH * 0.62f;
        const float textX = left + logoW + markH * 0.2f;
        const float base  = 52.0f + markH * 0.78f;
        const float deW   = textWidth (markBold, "DE");

        drawLogo (g, { left, 52.0f + markH * 0.08f, logoW, markH * 0.82f }, amberHi, 2.4f);

        auto dePath  = textPath (markBold, "DE", textX, base);
        auto cohPath = textPath (markLight, "COHERENCE", textX + deW - markH * 0.04f, base);
        g.setColour (cream);
        g.fillPath (dePath);
        g.setGradientFill (ColourGradient::vertical (amberHi, base - markH * 0.7f, Theme::accent, base));
        g.fillPath (cohPath);

        const auto sub = font (19.0f, "Semibold", 0.32f);
        g.setFont (sub);
        g.setColour (dimCream);
        g.drawText ("DRONE & SOUNDSCAPE REVERB", (int) textX, (int) (base + 14.0f), 600, 24, Justification::centredLeft, false);

        // badge
        const String badge ("NEW VST3 PLUGIN");
        const auto badgeFont = font (17.0f, "Bold", 0.18f);
        const float badgeW = textWidth (badgeFont, badge) + 30.0f;
        const Rectangle<float> badgeR (left, 214.0f, badgeW, 34.0f);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (badgeR, 17.0f);
        g.setFont (badgeFont);
        g.setColour (Colour (0xff1a0f06));
        g.drawText (badge, badgeR, Justification::centred, false);

        // hook
        const StringArray hook { "PLUCK IN.", "DRONE OUT." };
        const float hookH = fitHeight ("Black", 0.0f, hook, 520.0f, 108.0f);
        const auto hookFont = font (hookH, "Black");
        const float line1 = 268.0f + hookH * 0.80f;
        const float line2 = line1 + hookH * 1.02f;

        auto p1 = textPath (hookFont, hook[0], left - 3.0f, line1);
        auto p2 = textPath (hookFont, hook[1], left - 3.0f, line2);

        Path both (p1);
        both.addPath (p2);
        glow (g, both, Colours::black.withAlpha (0.9f), 24);

        g.setColour (cream);
        g.fillPath (p1);

        glow (g, p2, Theme::accent.withAlpha (0.65f), 30);
        g.setGradientFill (ColourGradient::vertical (Colour (0xffffc46b), line2 - hookH * 0.75f, Colour (0xffe8800f), line2));
        g.fillPath (p2);
        }

        auto out = downsample2x (img);
        addGrain (out, 0.035f);
        return out;
    }
}

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juce;

    const auto outDir = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                 : File::getCurrentWorkingDirectory().getChildFile ("marketing");
    outDir.createDirectory();

    DecoherenceProcessor proc;
    proc.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    // showcase preset: "Endless Drone"
    const auto presets = proc.getAllPresets();
    for (auto& p : presets)
        if (p.name == "Endless Drone")
            proc.loadPreset (p);

    PluckSource src;
    runAudio (proc, src, 9.0);

    std::unique_ptr<AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
    editor->setResizable (true, false); // no corner grip in the pictures
    editor->setSize (880, 520);
    editor->setVisible (true);

    // keep the audio running while the editor's 30 Hz timer settles knobs, meter and display
    for (int frame = 0; frame < 120; ++frame)
    {
        runAudio (proc, src, 1.0 / 30.0);
        MessageManager::getInstance()->runDispatchLoopUntil (33);
    }

    const auto ui = renderEditor (*editor, 2.0f);
    const auto uiFile = outDir.getChildFile ("plugin-ui@2x.png");
    const auto thumbFile = outDir.getChildFile ("youtube-thumbnail.png");

    const bool ok = savePng (ui, uiFile) && savePng (composeThumbnail (*editor), thumbFile);

    editor.reset();

    std::cout << (ok ? "Wrote " : "FAILED writing ") << uiFile.getFullPathName() << " and "
              << thumbFile.getFullPathName() << std::endl;
    return ok ? 0 : 1;
}
