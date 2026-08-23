#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

namespace theory
{

//==============================================================================
/** The accidental drawn in front of a notehead (or carried by the key signature). */
enum class Accidental { doubleFlat = -2, flat = -1, natural = 0, sharp = 1, doubleSharp = 2 };

/** A pitch resolved into how it should be *written*, not just what it sounds like.

    The same MIDI note is a different spelling in different keys - 61 is C# in
    D major but Db in Ab major - so every note has to be spelled against the
    current key before it can be drawn on a staff.
*/
struct SpelledNote
{
    int midiNote = 60;
    int letter = 0;          ///< 0..6 == C D E F G A B
    int octave = 4;          ///< scientific pitch notation, middle C == C4
    Accidental alter = Accidental::natural;
    bool needsAccidental = false;  ///< false when the key signature already covers it

    /** Vertical position on a staff, in diatonic steps above C0. Each step is
        half a staff space, so this maps straight onto staff geometry. */
    int diatonicStep() const noexcept { return octave * 7 + letter; }

    juce::String toString (bool withOctave = true) const;
};

//==============================================================================
/** A scale as a set of semitone offsets from its root. */
struct ScaleType
{
    const char* name;
    std::vector<int> intervals;   ///< semitones from the root, ascending, starting at 0
    int relativeMajorOffset;      ///< semitones to add to the root to get the major key
                                  ///< whose signature this scale shares
};

/** All scales offered in the UI, in menu order. */
const std::vector<ScaleType>& getScaleTypes();

//==============================================================================
/** A concrete key: a root pitch class plus a scale type. */
class Scale
{
public:
    Scale() = default;
    Scale (int rootPitchClass, int scaleTypeIndex);

    int getRootPitchClass() const noexcept    { return root; }
    int getScaleTypeIndex() const noexcept    { return typeIndex; }
    const ScaleType& getType() const;

    /** True if this pitch class is a member of the scale. */
    bool containsPitchClass (int pitchClass) const noexcept;
    bool containsNote (int midiNote) const noexcept { return containsPitchClass (midiNote % 12); }

    /** Number of sharps (positive) or flats (negative) in the key signature. */
    int getKeySignature() const noexcept      { return keySignature; }

    /** The alteration the key signature applies to a given letter (0..6 == C..B). */
    int alterationForLetter (int letter) const noexcept { return letterAlteration[(size_t) letter]; }

    /** Spells a MIDI note the way it should be written in this key. */
    SpelledNote spell (int midiNote) const;

    /** The reverse: which pitch a staff position means in this key. Clicking
        the staff gives a diatonic step, and the key signature decides whether
        that line is, say, an F or an F sharp. */
    int noteForDiatonicStep (int diatonicStep) const;

    /** "b3", "5" etc - which degree of this scale the note is, or {} if it is
        outside the scale. */
    juce::String degreeName (int midiNote) const;

    /** "minor 3rd", "perfect 5th" - the interval from the root, always defined. */
    juce::String intervalName (int midiNote) const;

    juce::String getName() const;

private:
    void rebuild();

    int root = 0;
    int typeIndex = 0;
    int keySignature = 0;
    std::array<bool, 12> memberPitchClasses {};
    std::array<int, 7> letterAlteration {};
};

//==============================================================================
//==============================================================================
/** Diatonic step of the bottom line of each staff: E4 for treble, G2 for bass.
    These live here rather than with the drawing code because they are facts
    about the clefs, not about how they are painted. */
constexpr int trebleBottomStep = 4 * 7 + 2;
constexpr int bassBottomStep   = 2 * 7 + 4;

/** The pitch range that reads comfortably in a clef: the five lines plus one
    ledger line either side.

    Nothing inside it needs more than a single ledger line, which is what makes
    it comfortable, and the two clefs meet exactly at middle C - one ledger line
    below the treble staff and one above the bass. In practice this also lands
    on the ranges people actually have: E2 to C4 is a bass singer's working
    compass and a double bass at written pitch, C4 to A5 the part of the piano
    the right hand mostly lives in.
*/
void getComfortableRange (bool trebleClef, int& lowestNote, int& highestNote);

/** Pitch class names, sharp-spelled, for menus. */
extern const char* const pitchClassNames[12];

/** Converts a frequency to a fractional MIDI note number (A4 = 440 Hz = 69). */
double frequencyToMidiNote (double frequency) noexcept;
double midiNoteToFrequency (double midiNote) noexcept;

/** Splits a fractional MIDI note into the nearest note and its cents error. */
void splitMidiNote (double fractionalNote, int& nearestNote, double& cents) noexcept;

/** "C#4" style name, always sharp-spelled - for when there is no key context. */
juce::String midiNoteName (int midiNote);

} // namespace theory
