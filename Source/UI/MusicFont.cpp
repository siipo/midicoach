#include "MusicFont.h"

namespace ui
{
namespace musicFont
{

juce::Typeface::Ptr getTypeface()
{
    static juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::MidiCoachMusic_ttf, (size_t) BinaryData::MidiCoachMusic_ttfSize);

    return typeface;
}

juce::Font fontForStaffSpace (float staffSpace)
{
    return juce::Font (getTypeface()).withPointHeight (staffSpace * 4.0f);
}

void drawGlyph (juce::Graphics& g, const juce::Font& font, juce_wchar codepoint,
                float x, float baselineY)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText (font, juce::String::charToString (codepoint), x, baselineY);
    arrangement.draw (g);
}

float getGlyphWidth (const juce::Font& font, juce_wchar codepoint)
{
    return font.getStringWidthFloat (juce::String::charToString (codepoint));
}

} // namespace musicFont
} // namespace ui
