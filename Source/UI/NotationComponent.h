#pragma once

#include <JuceHeader.h>
#include "../Theory/MusicTheory.h"
#include <vector>

namespace ui
{

/** A grand staff showing whatever is sounding right now.

    This is a live snapshot, not a score: notes appear as a chord stack while
    they are held and vanish when they stop. Notes played on the MIDI keyboard
    and the pitch heard on the audio input are drawn in different colours on
    the same staves, so you can see the two line up.
*/
class NotationComponent : public juce::Component
{
public:
    NotationComponent();

    void paint (juce::Graphics& g) override;

    void setScale (const theory::Scale& newScale);
    void setMidiNotes (const juce::Array<int>& notes);
    void setDetectedNote (int midiNote);          ///< -1 for none
    void setChordNotes (const std::vector<int>& notes);

private:
    /** A note resolved all the way down to where its head goes on the page. */
    struct PlacedNote
    {
        theory::SpelledNote spelled;
        juce::Colour colour;
        bool useTrebleStaff = true;
        float x = 0.0f;        ///< offset from the chord column until placed
        float y = 0.0f;
        bool accidentalOnLeft = true;
        int accidentalColumn = 0;
    };

    struct StaffGeometry
    {
        float staffSpace = 10.0f;
        float trebleBottomLineY = 0.0f;
        float bassBottomLineY = 0.0f;
        float staffLeft = 0.0f;
        float staffRight = 0.0f;
        float notesStartX = 0.0f;
    };

    StaffGeometry computeGeometry() const;

    /** Vertical position for a diatonic step, on whichever staff it belongs to. */
    float yForStep (const StaffGeometry& geometry, int diatonicStep, bool treble) const;

    void drawStaves (juce::Graphics& g, const StaffGeometry& geometry) const;
    void drawBrace (juce::Graphics& g, const StaffGeometry& geometry) const;
    void drawClefs (juce::Graphics& g, const StaffGeometry& geometry, const juce::Font& font) const;
    void drawKeySignature (juce::Graphics& g, const StaffGeometry& geometry,
                           const juce::Font& font, float& xInOut) const;
    void drawNotes (juce::Graphics& g, const StaffGeometry& geometry, const juce::Font& font,
                    const std::vector<PlacedNote>& placed) const;
    void drawLedgerLines (juce::Graphics& g, const StaffGeometry& geometry,
                          const PlacedNote& note, float noteheadWidth) const;

    /** Resolves the sounding notes to staff positions. The returned x values
        are offsets from the chord's column, not absolute - the caller adds the
        column position once it knows how much room the accidentals need. */
    std::vector<PlacedNote> collectNotes (const StaffGeometry& geometry,
                                          const juce::Font& font) const;

    theory::Scale scale { 0, 0 };
    juce::Array<int> midiNotes;
    int detectedNote = -1;
    std::vector<int> chordNotes;

    /** Diatonic step of the lowest line of each staff: E4 for treble, G2 for bass. */
    static constexpr int trebleBottomStep = 4 * 7 + 2;
    static constexpr int bassBottomStep   = 2 * 7 + 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NotationComponent)
};

} // namespace ui
