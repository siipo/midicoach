#pragma once

#include <JuceHeader.h>

namespace ui
{

/** Access to the embedded Bravura music font.

    Bravura follows SMuFL, which fixes two useful conventions: every glyph is
    designed on an em box exactly four staff spaces tall, and each glyph's
    origin sits on the staff line it belongs to - the G line for a treble clef,
    the middle of the notehead for a note. Set the font's point height to four
    staff spaces and glyphs land in the right place from their baseline alone,
    with no per-glyph fudge factors.
*/
namespace musicFont
{
    /** SMuFL codepoints for the glyphs this app draws. */
    enum Glyph : juce_wchar
    {
        gClef                 = 0xE050,
        fClef                 = 0xE062,
        noteheadBlack         = 0xE0A4,
        noteheadHalf          = 0xE0A3,
        noteheadWhole         = 0xE0A2,
        accidentalFlat        = 0xE260,
        accidentalNatural     = 0xE261,
        accidentalSharp       = 0xE262,
        accidentalDoubleSharp = 0xE263,
        accidentalDoubleFlat  = 0xE264,

        flag8thUp             = 0xE240,
        flag8thDown           = 0xE241,
        flag16thUp            = 0xE242,
        flag16thDown          = 0xE243,

        restWhole             = 0xE4E3,
        restHalf              = 0xE4E4,
        restQuarter           = 0xE4E5,
        rest8th               = 0xE4E6,
        rest16th              = 0xE4E7,

        augmentationDot       = 0xE1E7,

        /** Time-signature digits run consecutively from zero. */
        timeSig0              = 0xE080
    };

    juce::Typeface::Ptr getTypeface();

    /** A font whose em box is exactly four staff spaces, as SMuFL expects. */
    juce::Font fontForStaffSpace (float staffSpace);

    /** Draws a glyph with its SMuFL origin at (x, baselineY). */
    void drawGlyph (juce::Graphics& g, const juce::Font& font, juce_wchar codepoint,
                    float x, float baselineY);

    float getGlyphWidth (const juce::Font& font, juce_wchar codepoint);
}

} // namespace ui
