#include "NotationComponent.h"
#include "MusicFont.h"
#include "Palette.h"
#include <algorithm>
#include <map>

namespace ui
{

NotationComponent::NotationComponent()
{
    setOpaque (false);
}

void NotationComponent::setScale (const theory::Scale& newScale)
{
    scale = newScale;
    repaint();
}

void NotationComponent::setMidiNotes (const juce::Array<int>& notes)
{
    if (midiNotes != notes)
    {
        midiNotes = notes;
        repaint();
    }
}

void NotationComponent::setDetectedNote (int midiNote)
{
    if (detectedNote != midiNote)
    {
        detectedNote = midiNote;
        repaint();
    }
}

void NotationComponent::setChordNotes (const std::vector<int>& notes)
{
    if (chordNotes != notes)
    {
        chordNotes = notes;
        repaint();
    }
}

//==============================================================================
NotationComponent::StaffGeometry NotationComponent::computeGeometry() const
{
    StaffGeometry geometry;

    const auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);

    // Vertical budget, in staff spaces: 4 for each staff, 6 between them, and
    // 4 of headroom above and below for ledger lines.
    constexpr float totalSpaces = 4.0f + 6.0f + 4.0f + 8.0f;

    geometry.staffSpace = juce::jlimit (4.0f, 22.0f, bounds.getHeight() / totalSpaces);

    const auto space = geometry.staffSpace;
    const auto blockHeight = 14.0f * space;
    const auto top = bounds.getY() + (bounds.getHeight() - blockHeight) * 0.5f;

    geometry.trebleBottomLineY = top + 4.0f * space;
    geometry.bassBottomLineY   = geometry.trebleBottomLineY + 10.0f * space;

    geometry.staffLeft  = bounds.getX() + 2.0f * space;
    geometry.staffRight = bounds.getRight();

    return geometry;
}

float NotationComponent::yForStep (const StaffGeometry& geometry, int diatonicStep, bool treble) const
{
    const auto halfSpace = geometry.staffSpace * 0.5f;

    return treble
        ? geometry.trebleBottomLineY - (float) (diatonicStep - trebleBottomStep) * halfSpace
        : geometry.bassBottomLineY   - (float) (diatonicStep - bassBottomStep)   * halfSpace;
}

//==============================================================================
void NotationComponent::paint (juce::Graphics& g)
{
    auto geometry = computeGeometry();
    const auto font = musicFont::fontForStaffSpace (geometry.staffSpace);

    g.setColour (palette::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    drawStaves (g, geometry);
    drawBrace (g, geometry);
    drawClefs (g, geometry, font);

    auto x = geometry.staffLeft + 3.6f * geometry.staffSpace;
    drawKeySignature (g, geometry, font, x);

    auto placed = collectNotes (geometry, font);

    // Accidentals hang to the left of the noteheads, so the chord column has to
    // start far enough right that they clear the key signature.
    const auto numAccidentals = (int) std::count_if (placed.begin(), placed.end(),
                                                     [] (const auto& n) { return n.spelled.needsAccidental; });

    const auto accidentalWidth = musicFont::getGlyphWidth (font, musicFont::accidentalSharp) * 1.15f;

    geometry.notesStartX = x + geometry.staffSpace * 1.2f
                             + (float) numAccidentals * accidentalWidth;

    for (auto& note : placed)
        note.x += geometry.notesStartX;

    drawNotes (g, geometry, font, placed);
}

void NotationComponent::drawStaves (juce::Graphics& g, const StaffGeometry& geometry) const
{
    g.setColour (palette::staffLine);

    const auto lineThickness = juce::jmax (1.0f, geometry.staffSpace * 0.09f);

    for (int staff = 0; staff < 2; ++staff)
    {
        const auto bottom = staff == 0 ? geometry.trebleBottomLineY : geometry.bassBottomLineY;

        for (int line = 0; line < 5; ++line)
        {
            const auto y = bottom - (float) line * geometry.staffSpace;

            g.fillRect (juce::Rectangle<float> (geometry.staffLeft, y - lineThickness * 0.5f,
                                                geometry.staffRight - geometry.staffLeft,
                                                lineThickness));
        }
    }
}

/** The brace is drawn as a path rather than set from the font: Bravura's brace
    glyph is designed to be stretched by a layout engine, and scaling it to an
    arbitrary height distorts the tips. */
void NotationComponent::drawBrace (juce::Graphics& g, const StaffGeometry& geometry) const
{
    const auto space  = geometry.staffSpace;
    const auto top    = geometry.trebleBottomLineY - 4.0f * space;
    const auto bottom = geometry.bassBottomLineY;
    const auto height = bottom - top;
    const auto middle = (top + bottom) * 0.5f;
    const auto width  = space * 1.1f;
    const auto x      = geometry.staffLeft - space * 1.8f;

    juce::Path brace;
    brace.startNewSubPath (x + width, top);
    brace.cubicTo (x + width * 0.2f,  top + height * 0.10f,
                   x + width * 0.95f, middle - height * 0.22f,
                   x,                 middle);
    brace.cubicTo (x + width * 0.95f, middle + height * 0.22f,
                   x + width * 0.2f,  bottom - height * 0.10f,
                   x + width,         bottom);

    g.setColour (palette::staffLine);
    g.strokePath (brace, juce::PathStrokeType (space * 0.3f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // The vertical rule joining the two staves.
    g.fillRect (juce::Rectangle<float> (geometry.staffLeft - space * 0.1f, top,
                                        juce::jmax (1.0f, space * 0.13f), height));
}

void NotationComponent::drawClefs (juce::Graphics& g, const StaffGeometry& geometry,
                                   const juce::Font& font) const
{
    g.setColour (palette::ink);

    const auto x = geometry.staffLeft + geometry.staffSpace * 0.4f;

    // A treble clef's origin sits on the G line (second from the bottom), and a
    // bass clef's on the F line (second from the top). That is exactly what the
    // two staff steps below say, so no manual nudging is needed.
    musicFont::drawGlyph (g, font, musicFont::gClef, x,
                          yForStep (geometry, 4 * 7 + 4, true));    // G4

    musicFont::drawGlyph (g, font, musicFont::fClef, x,
                          yForStep (geometry, 3 * 7 + 3, false));   // F3
}

void NotationComponent::drawKeySignature (juce::Graphics& g, const StaffGeometry& geometry,
                                          const juce::Font& font, float& xInOut) const
{
    const auto signature = scale.getKeySignature();

    if (signature == 0)
        return;

    // Conventional staff positions for the accidentals, in the order they are
    // always written. Bass clef repeats the same shape two octaves lower.
    static constexpr int sharpSteps[7] = { 38, 35, 39, 36, 33, 37, 34 };
    static constexpr int flatSteps[7]  = { 34, 37, 33, 36, 32, 35, 31 };

    const auto isSharpKey = signature > 0;
    const auto count = juce::jmin (7, std::abs (signature));
    const auto glyph = isSharpKey ? musicFont::accidentalSharp : musicFont::accidentalFlat;
    const auto advance = musicFont::getGlyphWidth (font, glyph) * 1.05f;

    g.setColour (palette::ink);

    for (int i = 0; i < count; ++i)
    {
        const auto step = isSharpKey ? sharpSteps[i] : flatSteps[i];
        const auto x = xInOut + (float) i * advance;

        musicFont::drawGlyph (g, font, glyph, x, yForStep (geometry, step, true));
        musicFont::drawGlyph (g, font, glyph, x, yForStep (geometry, step - 14, false));
    }

    xInOut += (float) count * advance;
}

//==============================================================================
std::vector<NotationComponent::PlacedNote>
NotationComponent::collectNotes (const StaffGeometry& geometry, const juce::Font& font) const
{
    // Later entries win, so a note that is both played and heard shows as
    // played - the MIDI keyboard is the more definite of the two sources.
    std::map<int, juce::Colour> noteColours;

    for (auto note : chordNotes)
        if (note != detectedNote)
            noteColours[note] = palette::chordNote;

    if (detectedNote >= 0)
        noteColours[detectedNote] = palette::detectedNote;

    for (auto note : midiNotes)
        noteColours[note] = palette::midiNote;

    std::vector<PlacedNote> placed;
    placed.reserve (noteColours.size());

    for (const auto& entry : noteColours)
    {
        PlacedNote note;
        note.spelled        = scale.spell (entry.first);
        note.colour         = entry.second;
        note.useTrebleStaff = entry.first >= 60;
        note.y              = yForStep (geometry, note.spelled.diatonicStep(), note.useTrebleStaff);
        placed.push_back (note);
    }

    const auto noteheadWidth = musicFont::getGlyphWidth (font, musicFont::noteheadBlack);

    // Notes a step apart cannot share a column - the heads would overlap - so
    // the upper one of each pair is pushed to the right of the stem position.
    // Accidentals get stacked into columns for the same reason.
    int accidentalColumn = 0;

    for (size_t i = 0; i < placed.size(); ++i)
    {
        auto& note = placed[i];
        auto offset = 0.0f;

        if (i > 0)
        {
            const auto& previous = placed[i - 1];

            if (previous.useTrebleStaff == note.useTrebleStaff
                 && note.spelled.diatonicStep() - previous.spelled.diatonicStep() == 1
                 && previous.x == 0.0f)
                offset = noteheadWidth;
        }

        note.x = offset;

        if (note.spelled.needsAccidental)
            note.accidentalColumn = accidentalColumn++;
    }

    return placed;
}

void NotationComponent::drawNotes (juce::Graphics& g, const StaffGeometry& geometry,
                                   const juce::Font& font,
                                   const std::vector<PlacedNote>& placed) const
{
    if (placed.empty())
    {
        g.setColour (palette::inkDim);
        g.setFont (juce::Font (juce::jmax (11.0f, geometry.staffSpace * 1.1f)));
        g.drawText ("play something", getLocalBounds().reduced (12), juce::Justification::centredRight);
        return;
    }

    const auto noteheadWidth = musicFont::getGlyphWidth (font, musicFont::noteheadBlack);
    const auto totalAccidentals = std::count_if (placed.begin(), placed.end(),
                                                 [] (const auto& n) { return n.spelled.needsAccidental; });

    for (const auto& note : placed)
    {
        drawLedgerLines (g, geometry, note, noteheadWidth);

        g.setColour (note.colour);
        musicFont::drawGlyph (g, font, musicFont::noteheadBlack, note.x, note.y);

        if (note.spelled.needsAccidental)
        {
            const auto glyph = [&]() -> juce_wchar
            {
                switch (note.spelled.alter)
                {
                    case theory::Accidental::doubleFlat:  return musicFont::accidentalDoubleFlat;
                    case theory::Accidental::flat:        return musicFont::accidentalFlat;
                    case theory::Accidental::sharp:       return musicFont::accidentalSharp;
                    case theory::Accidental::doubleSharp: return musicFont::accidentalDoubleSharp;
                    case theory::Accidental::natural:     break;
                }

                return musicFont::accidentalNatural;
            }();

            const auto accidentalWidth = musicFont::getGlyphWidth (font, glyph);
            const auto column = (int) totalAccidentals - note.accidentalColumn;
            const auto x = geometry.notesStartX - (float) column * accidentalWidth * 1.15f
                             - geometry.staffSpace * 0.3f;

            musicFont::drawGlyph (g, font, glyph, x, note.y);
        }
    }
}

void NotationComponent::drawLedgerLines (juce::Graphics& g, const StaffGeometry& geometry,
                                         const PlacedNote& note, float noteheadWidth) const
{
    const auto bottomStep = note.useTrebleStaff ? trebleBottomStep : bassBottomStep;
    const auto topStep    = bottomStep + 8;
    const auto step       = note.spelled.diatonicStep();

    if (step <= topStep && step >= bottomStep)
        return;

    const auto thickness = juce::jmax (1.0f, geometry.staffSpace * 0.09f);
    const auto extension = noteheadWidth * 0.3f;

    g.setColour (palette::staffLine);

    // Ledger lines only exist on line positions, which are the even steps
    // above or below the staff.
    if (step > topStep)
    {
        for (int s = topStep + 2; s <= step; s += 2)
            g.fillRect (juce::Rectangle<float> (note.x - extension,
                                                yForStep (geometry, s, note.useTrebleStaff) - thickness * 0.5f,
                                                noteheadWidth + extension * 2.0f, thickness));
    }
    else
    {
        for (int s = bottomStep - 2; s >= step; s -= 2)
            g.fillRect (juce::Rectangle<float> (note.x - extension,
                                                yForStep (geometry, s, note.useTrebleStaff) - thickness * 0.5f,
                                                noteheadWidth + extension * 2.0f, thickness));
    }
}

} // namespace ui
