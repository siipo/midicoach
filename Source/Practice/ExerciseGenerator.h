#pragma once

#include <JuceHeader.h>
#include "../Model/Melody.h"
#include "../Theory/MusicTheory.h"
#include <vector>

namespace practice
{

/** Which form of the minor scale an exercise is written in.

    Graded material introduces the harmonic minor first - the raised seventh is
    what makes a minor key sound minor rather than modal - and only later asks
    for the melodic form, where the sixth rises too on the way up. Both are
    written against the natural-minor key signature, with the raised notes
    appearing as accidentals, which is how minor keys are actually notated.
*/
enum class MinorForm { natural, harmonic, melodic };

//==============================================================================
/** What to put in front of the reader.

    Four dials, each running 1 to 8, plus the handful of features that graded
    syllabuses introduce at particular points. The dials move independently, so
    you can practise awkward rhythms in an easy key or plain rhythms in five
    flats; `forGrade` sets all of them at once for the common case of working
    at one level.
*/
struct ExerciseSettings
{
    int intervalLevel = 1;   ///< steps, thirds, fourths, the triad, and on to the octave
    int rhythmLevel   = 1;   ///< note values, and the metres they are written in
    int keyLevel      = 1;   ///< how many sharps or flats, and whether minor appears
    int lengthLevel   = 1;   ///< 4 bars up to 16

    //==============================================================================
    /** Features that are worth switching on by themselves, because a reader is
        usually weak at one particular thing rather than at a whole grade. */
    bool rests            = false;   ///< a rest to breathe in, at phrase ends
    bool upbeat           = false;   ///< the tune starts before the first barline
    bool tiesOverBarlines = false;   ///< a note held across the barline
    bool syncopation      = false;   ///< notes that start off the beat
    bool chromatic        = false;   ///< accidentals from outside the key

    MinorForm minorForm = MinorForm::melodic;

    //==============================================================================
    /** Key to write in. Ignored when randomiseKey is set. */
    int  rootPitchClass = 0;
    bool minorKey       = false;
    bool randomiseKey   = false;

    /** Force a metre. Zero in either field means "whatever the rhythm level
        allows", which is the default; setting it is for the week when the
        lesson is about 6/8 and nothing else. */
    int timeSignatureNumerator   = 0;
    int timeSignatureDenominator = 0;

    /** Everything is generated to sit inside this, so a random key never lands
        outside what you can actually sing. */
    int lowestNote  = 60;    ///< C4
    int highestNote = 81;    ///< A5

    /** Which clef to write in. The range above decides where the notes sit;
        this only decides how they are written, so a low voice can read bass
        clef without the generator second-guessing it. */
    bool trebleClef = true;

    double tempoBpm = 84.0;

    /** Zero means pick one; anything else reproduces that exercise exactly. */
    juce::int64 seed = 0;
};

//==============================================================================
struct GeneratedExercise
{
    model::Melody melody;
    theory::Scale scale { 0, 0 };
    juce::String  description;
    juce::int64   seed = 0;
};

//==============================================================================
/** Builds short sight-reading exercises that hold together musically.

    Picking notes at random from a scale produces something unpleasant and not
    much use to practise. Real exercises have a grammar underneath, and this
    follows it: a structural skeleton taken from the shapes real phrases
    actually use, elaborated over a functional progression, with strong beats
    leaning towards chord tones and a proper cadence at the end.

    The rhythm works the same way: bars are built from a vocabulary of real
    rhythmic cells, and cells repeat across the phrase, because repetition is
    most of what makes a phrase sound deliberate rather than arbitrary.

    Above that sits phrase structure. Bars group into four-bar units and the
    units restate each other - a b a b, a a b a - which is what lets a sixteen
    bar exercise stay coherent instead of wandering for sixteen bars.
*/
class ExerciseGenerator
{
public:
    static constexpr int numLevels = 8;
    static constexpr int numGrades = 8;

    static GeneratedExercise generate (const ExerciseSettings& settings);

    //==============================================================================
    /** Everything a lesson working at this grade would put in front of you.

        Grades run 1 to 8 and simply set all four dials to the same number,
        switching on each extra feature at the point graded syllabuses introduce
        it. Any dial can then be moved on its own.
    */
    static ExerciseSettings forGrade (int grade);

    /** One line saying what a grade asks for, for the menu. */
    static juce::String describeGrade (int grade);

    /** Human-readable summary of what a level means, for the menus. */
    static juce::String describeIntervalLevel (int level);
    static juce::String describeRhythmLevel (int level);
    static juce::String describeKeyLevel (int level);
    static juce::String describeLengthLevel (int level);

    /** The metres a rhythm level is allowed to write in. */
    static std::vector<model::TimeSignature> metresForLevel (int rhythmLevel);

    /** Every metre the generator can write at all, for the metre menu. */
    static const std::vector<model::TimeSignature>& getAllMetres();
};

} // namespace practice
