#include "StaffLayout.h"
#include "MusicFont.h"
#include "Palette.h"

namespace ui
{
namespace staffLayout
{

void drawStaffLines (juce::Graphics& g, const StaffGeometry& geometry, juce::Colour colour)
{
    g.setColour (colour);

    const auto thickness = geometry.lineThickness();

    for (int line = 0; line < 5; ++line)
    {
        const auto y = geometry.bottomLineY - (float) line * geometry.staffSpace;

        g.fillRect (juce::Rectangle<float> (geometry.left, y - thickness * 0.5f,
                                            geometry.right - geometry.left, thickness));
    }
}

//==============================================================================
void drawClef (juce::Graphics& g, const StaffGeometry& geometry,
               const juce::Font& font, float x, bool treble)
{
    // A treble clef's origin sits on the G line, a bass clef's on the F line.
    // Expressed as steps, that needs no per-glyph nudging.
    const auto step  = treble ? 4 * 7 + 4     // G4
                              : 3 * 7 + 3;    // F3
    const auto glyph = treble ? musicFont::gClef : musicFont::fClef;

    musicFont::drawGlyph (g, font, glyph, x, geometry.yForStep (step));
}

float getClefWidth (const juce::Font& font, bool treble)
{
    return musicFont::getGlyphWidth (font, treble ? musicFont::gClef : musicFont::fClef);
}

//==============================================================================
/** Staff positions for key signature accidentals, written in the fixed order
    they always appear. These are the treble positions; bass is the same shape
    two octaves lower. */
static constexpr int sharpSteps[7] = { 38, 35, 39, 36, 33, 37, 34 };
static constexpr int flatSteps[7]  = { 34, 37, 33, 36, 32, 35, 31 };

float getKeySignatureWidth (const juce::Font& font, int keySignature)
{
    if (keySignature == 0)
        return 0.0f;

    const auto glyph = keySignature > 0 ? musicFont::accidentalSharp : musicFont::accidentalFlat;

    return (float) juce::jmin (7, std::abs (keySignature))
             * musicFont::getGlyphWidth (font, glyph) * 1.05f;
}

float drawKeySignature (juce::Graphics& g, const StaffGeometry& geometry,
                        const juce::Font& font, float x, int keySignature, bool treble,
                        juce::Colour colour)
{
    if (keySignature == 0)
        return 0.0f;

    const auto isSharpKey = keySignature > 0;
    const auto count      = juce::jmin (7, std::abs (keySignature));
    const auto glyph      = isSharpKey ? musicFont::accidentalSharp : musicFont::accidentalFlat;
    const auto advance    = musicFont::getGlyphWidth (font, glyph) * 1.05f;
    const auto octaveShift = treble ? 0 : -14;

    g.setColour (colour);

    for (int i = 0; i < count; ++i)
    {
        const auto step = (isSharpKey ? sharpSteps[i] : flatSteps[i]) + octaveShift;

        musicFont::drawGlyph (g, font, glyph, x + (float) i * advance, geometry.yForStep (step));
    }

    return (float) count * advance;
}

//==============================================================================
/** SMuFL time-signature digits are drawn from a baseline on the staff line they
    straddle, so the numerator sits on the line above the middle and the
    denominator on the line below. */
static juce::String timeSignatureDigits (int value)
{
    juce::String digits;

    for (auto character : juce::String (juce::jmax (0, value)))
        digits << juce::String::charToString ((juce_wchar) (0xE080 + (character - '0')));

    return digits;
}

float getTimeSignatureWidth (const juce::Font& font, int numerator, int denominator)
{
    return juce::jmax (font.getStringWidthFloat (timeSignatureDigits (numerator)),
                       font.getStringWidthFloat (timeSignatureDigits (denominator)));
}

void drawTimeSignature (juce::Graphics& g, const StaffGeometry& geometry,
                        const juce::Font& font, float x, int numerator, int denominator,
                        juce::Colour colour)
{
    const auto top    = timeSignatureDigits (numerator);
    const auto bottom = timeSignatureDigits (denominator);
    const auto width  = getTimeSignatureWidth (font, numerator, denominator);

    g.setColour (colour);

    juce::GlyphArrangement upper;
    upper.addLineOfText (font, top,
                         x + (width - font.getStringWidthFloat (top)) * 0.5f,
                         geometry.yForStep (geometry.bottomLineStep + 6));
    upper.draw (g);

    juce::GlyphArrangement lower;
    lower.addLineOfText (font, bottom,
                         x + (width - font.getStringWidthFloat (bottom)) * 0.5f,
                         geometry.yForStep (geometry.bottomLineStep + 2));
    lower.draw (g);
}

//==============================================================================
void drawLedgerLines (juce::Graphics& g, const StaffGeometry& geometry,
                      int diatonicStep, float noteheadLeft, float noteheadWidth,
                      juce::Colour colour)
{
    const auto bottomStep = geometry.bottomLineStep;
    const auto topStep    = geometry.topLineStep();

    if (diatonicStep <= topStep && diatonicStep >= bottomStep)
        return;

    const auto thickness = geometry.lineThickness();
    const auto overhang  = noteheadWidth * 0.3f;

    g.setColour (colour);

    auto drawAt = [&] (int step)
    {
        g.fillRect (juce::Rectangle<float> (noteheadLeft - overhang,
                                            geometry.yForStep (step) - thickness * 0.5f,
                                            noteheadWidth + overhang * 2.0f, thickness));
    };

    // Ledger lines only exist on line positions - the even steps beyond the staff.
    if (diatonicStep > topStep)
        for (int step = topStep + 2; step <= diatonicStep; step += 2)
            drawAt (step);
    else
        for (int step = bottomStep - 2; step >= diatonicStep; step -= 2)
            drawAt (step);
}

//==============================================================================
juce_wchar getAccidentalGlyph (theory::Accidental accidental)
{
    switch (accidental)
    {
        case theory::Accidental::doubleFlat:  return musicFont::accidentalDoubleFlat;
        case theory::Accidental::flat:        return musicFont::accidentalFlat;
        case theory::Accidental::sharp:       return musicFont::accidentalSharp;
        case theory::Accidental::doubleSharp: return musicFont::accidentalDoubleSharp;
        case theory::Accidental::natural:     break;
    }

    return musicFont::accidentalNatural;
}

} // namespace staffLayout
} // namespace ui
