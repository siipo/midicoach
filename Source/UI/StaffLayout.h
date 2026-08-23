#pragma once

#include <JuceHeader.h>
#include "../Theory/MusicTheory.h"
#include "Palette.h"

namespace ui
{

/** Geometry of one five-line staff.

    Everything vertical is expressed in diatonic steps rather than pitches,
    which is what makes ledger lines, seconds and clef changes fall out without
    special cases. One step is half a staff space.
*/
struct StaffGeometry
{
    float staffSpace     = 10.0f;
    float bottomLineY    = 0.0f;
    int   bottomLineStep = 30;      ///< E4 for treble, G2 for bass
    float left           = 0.0f;
    float right          = 0.0f;

    float topLineY() const noexcept     { return bottomLineY - 4.0f * staffSpace; }
    float middleLineY() const noexcept  { return bottomLineY - 2.0f * staffSpace; }
    int   middleLineStep() const noexcept { return bottomLineStep + 4; }
    int   topLineStep() const noexcept  { return bottomLineStep + 8; }

    float lineThickness() const noexcept { return juce::jmax (1.0f, staffSpace * 0.09f); }

    float yForStep (int diatonicStep) const noexcept
    {
        return bottomLineY - (float) (diatonicStep - bottomLineStep) * staffSpace * 0.5f;
    }

    /** Nearest diatonic step to a y position - the inverse of yForStep, used to
        turn a mouse position back into a pitch. */
    int stepForY (float y) const noexcept
    {
        return bottomLineStep + juce::roundToInt ((bottomLineY - y) / (staffSpace * 0.5f));
    }
};

//==============================================================================
namespace staffLayout
{
    /** Diatonic step of the bottom line for each clef. Defined with the rest of
        the theory, since it is a fact about the clef rather than the drawing. */
    constexpr int trebleBottomStep = theory::trebleBottomStep;   // E4
    constexpr int bassBottomStep   = theory::bassBottomStep;     // G2

    /** Engraving constants, taken from convention rather than from SMuFL
        metadata, which we don't ship. Units are staff spaces. */
    constexpr float stemLength    = 3.5f;
    constexpr float stemThickness = 0.12f;
    constexpr float beamThickness = 0.5f;

    void drawStaffLines (juce::Graphics& g, const StaffGeometry& geometry,
                         juce::Colour colour = palette::staffLine);

    void  drawClef (juce::Graphics& g, const StaffGeometry& geometry,
                    const juce::Font& font, float x, bool treble);
    float getClefWidth (const juce::Font& font, bool treble);

    /** Draws the key signature and returns the width it used. */
    float drawKeySignature (juce::Graphics& g, const StaffGeometry& geometry,
                            const juce::Font& font, float x, int keySignature, bool treble,
                            juce::Colour colour = palette::ink);
    float getKeySignatureWidth (const juce::Font& font, int keySignature);

    void  drawTimeSignature (juce::Graphics& g, const StaffGeometry& geometry,
                             const juce::Font& font, float x, int numerator, int denominator,
                             juce::Colour colour = palette::ink);
    float getTimeSignatureWidth (const juce::Font& font, int numerator, int denominator);

    void drawLedgerLines (juce::Graphics& g, const StaffGeometry& geometry,
                          int diatonicStep, float noteheadLeft, float noteheadWidth,
                          juce::Colour colour = palette::staffLine);

    juce_wchar getAccidentalGlyph (theory::Accidental accidental);
}

} // namespace ui
