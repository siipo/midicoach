#include "MusicTheory.h"
#include <cmath>

namespace theory
{

const char* const pitchClassNames[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** Semitone above C for each letter, 0..6 == C D E F G A B. */
static constexpr int naturalPitchClass[7] = { 0, 2, 4, 5, 7, 9, 11 };
static constexpr char letterNames[7]      = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };

/** Order the sharps and flats appear in a key signature, as letter indices. */
static constexpr int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };  // F C G D A E B
static constexpr int flatOrder[7]  = { 6, 2, 5, 1, 4, 0, 3 };  // B E A D G C F

/** Key signature for each major tonic, picking the conventional spelling. */
static constexpr int majorKeySignature[12] =
    { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };

/** Accidentals in text labels stay ASCII on purpose. The proper Unicode signs
    (U+266F, U+266D) are missing from the default Windows UI font and render as
    empty boxes; "#" and "b" are the conventional written substitutes anyway.
    The staff itself draws real engraved accidentals from Bravura. */
static juce::String accidentalGlyphText (Accidental a)
{
    switch (a)
    {
        case Accidental::doubleFlat:  return "bb";
        case Accidental::flat:        return "b";
        case Accidental::sharp:       return "#";
        case Accidental::doubleSharp: return "x";
        case Accidental::natural:     break;
    }

    return {};
}

juce::String SpelledNote::toString (bool withOctave) const
{
    auto s = juce::String::charToString ((juce_wchar) letterNames[(size_t) letter])
               + accidentalGlyphText (alter);

    return withOctave ? s + juce::String (octave) : s;
}

//==============================================================================
const std::vector<ScaleType>& getScaleTypes()
{
    static const std::vector<ScaleType> types
    {
        { "Major",            { 0, 2, 4, 5, 7, 9, 11 },       0 },
        { "Natural Minor",    { 0, 2, 3, 5, 7, 8, 10 },       3 },
        { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 },       3 },
        { "Melodic Minor",    { 0, 2, 3, 5, 7, 9, 11 },       3 },
        { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 },      10 },
        { "Phrygian",         { 0, 1, 3, 5, 7, 8, 10 },       8 },
        { "Lydian",           { 0, 2, 4, 6, 7, 9, 11 },       7 },
        { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 },       5 },
        { "Locrian",          { 0, 1, 3, 5, 6, 8, 10 },       1 },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 },              0 },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 },             3 },
        { "Blues",            { 0, 3, 5, 6, 7, 10 },          3 },
        { "Whole Tone",       { 0, 2, 4, 6, 8, 10 },          0 },
        { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 0 },
    };

    return types;
}

//==============================================================================
Scale::Scale (int rootPitchClass, int scaleTypeIndex)
    : root (((rootPitchClass % 12) + 12) % 12),
      typeIndex (juce::jlimit (0, (int) getScaleTypes().size() - 1, scaleTypeIndex))
{
    rebuild();
}

const ScaleType& Scale::getType() const
{
    return getScaleTypes()[(size_t) typeIndex];
}

void Scale::rebuild()
{
    memberPitchClasses.fill (false);

    for (auto interval : getType().intervals)
        memberPitchClasses[(size_t) ((root + interval) % 12)] = true;

    const auto majorTonic = (root + getType().relativeMajorOffset) % 12;
    keySignature = majorKeySignature[(size_t) majorTonic];

    letterAlteration.fill (0);

    for (int i = 0; i < std::abs (keySignature); ++i)
        letterAlteration[(size_t) (keySignature > 0 ? sharpOrder[i] : flatOrder[i])]
            = keySignature > 0 ? 1 : -1;
}

bool Scale::containsPitchClass (int pitchClass) const noexcept
{
    return memberPitchClasses[(size_t) (((pitchClass % 12) + 12) % 12)];
}

SpelledNote Scale::spell (int midiNote) const
{
    const auto pitchClass = ((midiNote % 12) + 12) % 12;

    int bestLetter = 0, bestAlter = 0, bestCost = std::numeric_limits<int>::max();

    for (int letter = 0; letter < 7; ++letter)
    {
        auto needed = pitchClass - naturalPitchClass[(size_t) letter];

        while (needed >  6) needed -= 12;
        while (needed < -6) needed += 12;

        if (std::abs (needed) > 2)
            continue;

        const auto keyAlter = letterAlteration[(size_t) letter];

        // Spellings the key signature already covers win outright; after that,
        // prefer plain notes, avoid double accidentals, and follow the key's
        // flavour (flats in flat keys, sharps in sharp keys).
        auto cost = std::abs (needed - keyAlter) * 10 + std::abs (needed);

        if (std::abs (needed) == 2)
            cost += 20;

        if (needed != 0)
        {
            const auto prefersFlats = keySignature < 0;

            if ((needed < 0) != prefersFlats)
                cost += 1;
        }

        if (cost < bestCost)
        {
            bestCost   = cost;
            bestLetter = letter;
            bestAlter  = needed;
        }
    }

    SpelledNote n;
    n.midiNote        = midiNote;
    n.letter          = bestLetter;
    n.alter           = (Accidental) bestAlter;
    n.octave          = (midiNote - naturalPitchClass[(size_t) bestLetter] - bestAlter) / 12 - 1;
    n.needsAccidental = bestAlter != letterAlteration[(size_t) bestLetter];

    return n;
}

int Scale::noteForDiatonicStep (int diatonicStep) const
{
    const auto letter = ((diatonicStep % 7) + 7) % 7;
    const auto octave = (diatonicStep - letter) / 7;

    return (octave + 1) * 12
             + naturalPitchClass[(size_t) letter]
             + letterAlteration[(size_t) letter];
}

juce::String Scale::degreeName (int midiNote) const
{
    const auto interval = (((midiNote - root) % 12) + 12) % 12;

    if (! containsPitchClass (midiNote))
        return {};

    static const char* const labels[12] =
        { "1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" };

    // A raised fourth reads as #4 only when the scale has no perfect fourth to
    // sit next to - in the blues scale the same pitch really is a flat fifth.
    if (interval == 6 && ! containsPitchClass (root + 5))
        return "#4";

    return labels[(size_t) interval];
}

juce::String Scale::intervalName (int midiNote) const
{
    static const char* const names[12] =
    {
        "unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
        "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th"
    };

    return names[(size_t) ((((midiNote - root) % 12) + 12) % 12)];
}

juce::String Scale::getName() const
{
    return spell (60 + root).toString (false) + " " + getType().name;
}

//==============================================================================
void getComfortableRange (bool trebleClef, int& lowestNote, int& highestNote)
{
    // Derived from the staff itself rather than written out, so the two can
    // never drift apart. C major is only used to turn staff positions into
    // pitches; the answer is the same whatever key is being read.
    const Scale plain (0, 0);

    const auto bottomLine = trebleClef ? trebleBottomStep : bassBottomStep;

    lowestNote  = plain.noteForDiatonicStep (bottomLine - 2);
    highestNote = plain.noteForDiatonicStep (bottomLine + 8 + 2);
}

double frequencyToMidiNote (double frequency) noexcept
{
    return frequency > 0.0 ? 69.0 + 12.0 * std::log2 (frequency / 440.0) : 0.0;
}

double midiNoteToFrequency (double midiNote) noexcept
{
    return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0);
}

void splitMidiNote (double fractionalNote, int& nearestNote, double& cents) noexcept
{
    nearestNote = (int) std::lround (fractionalNote);
    cents       = (fractionalNote - nearestNote) * 100.0;
}

juce::String midiNoteName (int midiNote)
{
    return juce::String (pitchClassNames[(size_t) (((midiNote % 12) + 12) % 12)])
             + juce::String (midiNote / 12 - 1);
}

} // namespace theory
