#include "Vfd.h"
#include "Layout.h"

#if RVB1_HAS_SPACE_MONO
 #include "BinaryData.h"
#endif

using namespace infiniverb;

namespace
{
    // Space Mono Bold, embedded (SIL OFL), as the design specifies. Until the
    // font file is in plugin/assets, Menlo Bold stands in.
    juce::Font vfdFont (float size, float tracking)
    {
       #if RVB1_HAS_SPACE_MONO
        static const auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoBold_ttf,
                                                                              BinaryData::SpaceMonoBold_ttfSize);
        auto options = juce::FontOptions (typeface).withHeight (size);
       #else
        auto options = juce::FontOptions ("Menlo", size, juce::Font::bold);
       #endif
        return juce::Font (options.withKerningFactor (tracking / size));
    }

    // Three box passes approximate a gaussian, in linear time — fast enough to
    // redo whenever the display changes.
    void blurAlpha (juce::Image& image, int radius)
    {
        if (radius < 1)
            return;

        const int w = image.getWidth(), h = image.getHeight();
        std::vector<float> a ((size_t) (w * h)), b ((size_t) (w * h));

        {
            juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    a[(size_t) (y * w + x)] = *data.getPixelPointer (x, y) / 255.0f;
        }

        auto pass = [&] (std::vector<float>& src, std::vector<float>& dst, bool horizontal)
        {
            const int len = horizontal ? w : h, lines = horizontal ? h : w;
            const float norm = 1.0f / (float) (2 * radius + 1);
            for (int l = 0; l < lines; ++l)
            {
                auto at = [&] (int i) -> float& { return horizontal ? src[(size_t) (l * w + i)] : src[(size_t) (i * w + l)]; };
                auto out = [&] (int i) -> float& { return horizontal ? dst[(size_t) (l * w + i)] : dst[(size_t) (i * w + l)]; };
                float sum = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                    sum += at (juce::jlimit (0, len - 1, i));
                for (int i = 0; i < len; ++i)
                {
                    out (i) = sum * norm;
                    sum += at (juce::jmin (len - 1, i + radius + 1)) - at (juce::jmax (0, i - radius));
                }
            }
        };

        for (int k = 0; k < 3; ++k)
        {
            pass (a, b, true);
            pass (b, a, false);
        }

        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                *data.getPixelPointer (x, y) = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (a[(size_t) (y * w + x)] * 255.0f));
    }

    struct TextItem { juce::String text, ghost; float size, tracking, x, y; bool alignRight; bool lit; };
}

bool Vfd::State::operator== (const State& o) const
{
    return preset == o.preset && gravity == o.gravity && focusLabel == o.focusLabel && focusValue == o.focusValue
        && juce::approximatelyEqual (decaySeconds, o.decaySeconds)
        && envelope == o.envelope && kill == o.kill && onB == o.onB && dim == o.dim;
}

Vfd::Vfd()
{
    setBounds (kVfdGlass.toNearestInt());
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Vfd::setState (const State& s)
{
    if (s == state)
        return;

    state = s;
    dirty = true;
    repaint();
}

void Vfd::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onClick)
        onClick();
}

void Vfd::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (dirty || ! juce::approximatelyEqual (scale, compositeScale))
        render (scale);

    g.drawImage (composite, getLocalBounds().toFloat());
}

void Vfd::render (float scale)
{
    dirty = false;
    compositeScale = scale;

    const int w = juce::roundToInt ((float) getWidth() * scale), h = juce::roundToInt ((float) getHeight() * scale);
    // Software images, and every drawing context closed before its image is
    // read: a native image can still be holding unflushed drawing otherwise,
    // which on macOS left half the display blank.
    composite = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    juce::Image lit (juce::Image::SingleChannel, w, h, true, juce::SoftwareImageType());

    {
    juce::Graphics cg (composite), lg (lit);
    cg.addTransform (juce::AffineTransform::scale (scale));
    lg.addTransform (juce::AffineTransform::scale (scale));
    lg.setColour (juce::Colours::white);

    const float right = 318.0f;
    const auto gravityText = state.gravity == 0 ? juce::String ("0")
                                                : (state.gravity > 0 ? "+" : "-") + juce::String (std::abs (state.gravity));

    const bool focus = state.focusLabel.isNotEmpty();
    const auto bigLabel = focus ? state.focusLabel : juce::String ("GRAVITY");
    const auto bigValue = focus ? state.focusValue : gravityText;

    // The ghost behind the big readout: every digit an 8, so the segments it
    // could light are always faintly there.
    juce::String bigGhost;
    for (auto c : bigValue)
        bigGhost << (juce::CharacterFunctions::isDigit (c) ? juce::juce_wchar ('8') : c);
    if (! focus) bigGhost = "+88";

    // A long preset name is cut short rather than running under the readout.
    auto presetFont = vfdFont (16.0f, 1.0f);
    auto presetText = state.preset;
    while (presetText.length() > 4 && juce::GlyphArrangement::getStringWidth (presetFont, presetText) > 172.0f)
        presetText = presetText.dropLastCharacters (1);
    if (presetText != state.preset)
        presetText = presetText.dropLastCharacters (1).trimEnd() + ".";

    // Positions and sizes from spec.json's vfd.layout, glass-relative.
    const TextItem items[] {
        { "PRESET",  {},      8.0f, 1.5f, 10.0f,  9.0f, false, true },
        { presetText, {},    16.0f, 1.0f, 10.0f, 21.0f, false, true },
        { bigLabel, {},       8.0f, 1.5f, right,  9.0f, true,  true },
        { bigValue, bigGhost, 34.0f, 0.0f, right, 17.0f, true, true },
        { "DECAY " + juce::String (state.decaySeconds, 1) + "s", {}, 9.0f, 1.0f, right, 64.0f, true, true },
        { "KILL",    {},      8.0f, 1.5f, 10.0f, 92.0f, false, state.kill },
        { "A",       {},      8.0f, 1.5f, 304.0f, 92.0f, true, ! state.onB },
        { "B",       {},      8.0f, 1.5f, right, 92.0f, true, state.onB },
    };

    for (const auto& it : items)
    {
        const auto font = vfdFont (it.size, it.tracking);
        const float width = juce::GlyphArrangement::getStringWidth (font, it.text.isEmpty() ? it.ghost : it.text) + 2.0f;
        const juce::Rectangle<float> box (it.alignRight ? it.x - width : it.x, it.y, width, it.size * 1.5f);
        const auto just = (it.alignRight ? juce::Justification::topRight : juce::Justification::topLeft);

        cg.setFont (font);
        if (it.ghost.isNotEmpty())
        {
            const float gw = juce::GlyphArrangement::getStringWidth (font, it.ghost) + 2.0f;
            cg.setColour (kPhosphor.withAlpha (kGhostAlpha));
            cg.drawText (it.ghost, juce::Rectangle<float> (it.x - gw, it.y, gw, it.size * 1.5f), just, false);
        }

        if (it.lit)
        {
            lg.setFont (font);
            lg.drawText (it.text, box, just, false);
        }
        else
        {
            cg.setColour (kPhosphor.withAlpha (kGhostAlpha));
            cg.drawText (it.text, box, just, false);
        }
    }

    // The envelope: columns lit from the bottom, ghost dots everywhere else.
    constexpr float originX = 10.0f, originY = 47.0f, pitch = 5.0f, radius = 1.5f;
    for (int c = 0; c < kEnvelopeCols; ++c)
        for (int r = 0; r < kEnvelopeRows; ++r)
        {
            const auto dot = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                 .withCentre ({ originX + radius + pitch * (float) c, originY + radius + pitch * (float) r });
            const bool on = (kEnvelopeRows - r) <= state.envelope[(size_t) c];
            if (on)
                lg.fillEllipse (dot);
            else
            {
                cg.setColour (kPhosphor.withAlpha (0.10f));
                cg.fillEllipse (dot);
            }
        }

    }

    // Glow: blur 6 at 80 % and blur 2 at 60 %, then the lit layer itself.
    const float level = state.dim ? 0.15f : 1.0f;
    auto glowWide = lit.createCopy(), glowTight = lit.createCopy();
    blurAlpha (glowWide,  juce::roundToInt (6.0f * scale / 2.0f));
    blurAlpha (glowTight, juce::roundToInt (2.0f * scale / 2.0f));

    juce::Graphics out (composite);
    out.setColour (kPhosphor.withAlpha (0.8f * level));
    out.drawImageAt (glowWide, 0, 0, true);
    out.setColour (kPhosphor.withAlpha (0.6f * level));
    out.drawImageAt (glowTight, 0, 0, true);
    out.setColour (kPhosphor.withAlpha (level));
    out.drawImageAt (lit, 0, 0, true);
}
