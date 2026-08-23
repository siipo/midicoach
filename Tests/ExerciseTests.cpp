/*  Property tests for the sight-reading generator.

    A generator cannot be checked by looking at one example: the interesting
    failures are the rare ones, the seed in a hundred that emits a tritone leap
    or wanders out of the singer's range. So this asserts the musical
    invariants over tens of thousands of exercises across every combination of
    the difficulty dials, and then checks each of the features graded reading
    introduces - rests, upbeats, ties, syncopation, accidentals - both appears
    when it is asked for and stays away when it is not.

    Build target: ExerciseTests. Returns non-zero if anything fails.
*/

#include <JuceHeader.h>
#include "../Source/Practice/ExerciseGenerator.h"

#include <iostream>

namespace
{
    int failures = 0;
    int checks   = 0;
    juce::String firstFailure;

    void check (bool condition, const juce::String& what)
    {
        ++checks;

        if (! condition)
        {
            ++failures;

            if (firstFailure.isEmpty())
                firstFailure = what;

            if (failures <= 12)
                std::cout << "  FAIL: " << what << std::endl;
        }
    }

    void checkEqual (int actual, int expected, const juce::String& what)
    {
        check (actual == expected,
               what + " (expected " + juce::String (expected)
                    + ", got " + juce::String (actual) + ")");
    }

    juce::String context (const practice::ExerciseSettings& settings,
                          const practice::GeneratedExercise& exercise)
    {
        return "[i" + juce::String (settings.intervalLevel)
             + " r" + juce::String (settings.rhythmLevel)
             + " k" + juce::String (settings.keyLevel)
             + " l" + juce::String (settings.lengthLevel)
             + " seed " + juce::String (exercise.seed)
             + " " + exercise.description + "] ";
    }

    /** How wide a leap the interval level allows, in semitones. A diatonic step
        spans at most two, and a raised seventh can add one more. */
    int widestLeapFor (int intervalLevel)
    {
        static const int degrees[] = { 1, 2, 3, 4, 5, 6, 7, 7 };

        return degrees[juce::jlimit (1, 8, intervalLevel) - 1] * 2 + 1;
    }

    /** The whole battery, run against one exercise. `strict` is off for the
        grade sweep, where the features deliberately break the plain rules:
        an exercise with accidentals in it is not diatonic, by design. */
    void checkExercise (const practice::ExerciseSettings& settings,
                        const practice::GeneratedExercise& exercise)
    {
        const auto& melody = exercise.melody;
        const auto notes = melody.getRehearsalNotes();
        const auto where = context (settings, exercise);

        check (! notes.empty(), where + "produces notes");

        if (notes.empty())
            return;

        // -- the model's own invariant survives generation ---------------------
        {
            const auto barLength = melody.getTimeSignature().barTicks();
            int cursor = 0;
            auto tiled = true;

            for (const auto& event : melody.getEvents())
            {
                if (event.startTick != cursor)
                    tiled = false;

                if (event.startTick / barLength != (event.endTick() - 1) / barLength)
                    tiled = false;

                if (model::findNoteValue (event.lengthTicks) == nullptr)
                    tiled = false;

                cursor = event.endTick();
            }

            check (tiled && cursor == melody.getTotalTicks(),
                   where + "fills every bar with writable values");
        }

        // -- rests appear only when they were asked for ------------------------
        {
            auto rests = 0;

            for (const auto& event : melody.getEvents())
                if (event.isRest)
                    ++rests;

            if (! settings.rests && ! settings.upbeat)
                check (rests == 0, where + "no unasked-for rests ("
                                     + juce::String (rests) + ")");
        }

        // -- there is enough on the page to be worth reading -------------------
        // A line of whole-bar notes is not a sight-reading test. Bars that come
        // back too empty are rewritten busier, so this is a property now.
        {
            // The pickup bar is not a whole bar and does not owe a whole bar's
            // worth of notes.
            const auto fullBars = melody.getBarCount() - (settings.upbeat ? 1 : 0);

            check ((int) notes.size() * 2 >= fullBars * 3 || fullBars <= 2,
                   where + juce::String ((int) notes.size()) + " notes in "
                         + juce::String (fullBars) + " bars is too thin");
        }

        // -- singable: everything inside the range asked for -------------------
        {
            const auto lowest  = juce::jmin (settings.lowestNote, settings.highestNote);
            const auto highest = juce::jmax (settings.lowestNote, settings.highestNote);
            auto inRange = true;

            for (auto note : notes)
                if (note < lowest || note > highest)
                    inRange = false;

            check (inRange, where + "stays inside the vocal range");
        }

        // -- ends on the tonic -------------------------------------------------
        check (notes.back() % 12 == exercise.scale.getRootPitchClass(),
               where + "cadences on the tonic");

        // -- approached by step ------------------------------------------------
        if (notes.size() >= 2)
        {
            // A step, or the tonic repeated - both are how real tunes land, and
            // the corpus supplies both. What must never happen is a leap into
            // the final note.
            const auto approach = std::abs (notes[notes.size() - 1] - notes[notes.size() - 2]);
            check (approach <= 2, where + "the tonic is not leapt to");
        }

        // -- no leap wider than the level allows -------------------------------
        {
            const auto widest = [&notes]
            {
                auto found = 0;

                for (size_t i = 1; i < notes.size(); ++i)
                    found = juce::jmax (found, std::abs (notes[i] - notes[i - 1]));

                return found;
            }();

            const auto allowed = widestLeapFor (settings.intervalLevel)
                                   + (settings.chromatic ? 1 : 0);

            check (widest <= allowed,
                   where + "widest leap " + juce::String (widest)
                         + " semitones is within the level's " + juce::String (allowed));
        }

        // -- no tritone leaps --------------------------------------------------
        // Chromatic notes can make one, and are allowed to: an augmented fourth
        // out of a raised fourth is a real thing to read. Everywhere else it is
        // a bug.
        if (! settings.chromatic)
        {
            auto clean = true;

            for (size_t i = 1; i < notes.size(); ++i)
                if (std::abs (notes[i] - notes[i - 1]) == 6)
                    clean = false;

            check (clean, where + "no tritone leaps");
        }

        // -- in the key, allowing the raised leading note in minor -------------
        if (! settings.chromatic)
        {
            const auto root = exercise.scale.getRootPitchClass();
            const auto leadingTone = (root + 11) % 12;
            auto diatonic = true;

            for (auto note : notes)
                if (! exercise.scale.containsNote (note) && note % 12 != leadingTone)
                    diatonic = false;

            check (diatonic, where + "stays in the key");
        }

        // -- the metre is one the rhythm level offers --------------------------
        if (settings.timeSignatureNumerator == 0)
        {
            const auto signature = melody.getTimeSignature();
            const auto allowed = practice::ExerciseGenerator::metresForLevel (settings.rhythmLevel);
            auto found = false;

            for (const auto& candidate : allowed)
                if (candidate.numerator == signature.numerator
                     && candidate.denominator == signature.denominator)
                    found = true;

            check (found, where + "writes in a metre the level allows");
        }

        // -- the key signature is no harder than asked for ---------------------
        if (settings.randomiseKey && settings.keyLevel <= 6)
        {
            const auto accidentals = std::abs (exercise.scale.getKeySignature());

            check (accidentals <= settings.keyLevel - 1,
                   where + "key signature of " + juce::String (accidentals)
                         + " is within the level's " + juce::String (settings.keyLevel - 1));
        }
    }

    bool hasRest (const model::Melody& melody)
    {
        for (const auto& event : melody.getEvents())
            if (event.isRest)
                return true;

        return false;
    }

    bool hasTieOverBarline (const model::Melody& melody)
    {
        const auto barLength = melody.getTimeSignature().barTicks();

        for (const auto& event : melody.getEvents())
            if (event.tiedToNext && event.endTick() % barLength == 0)
                return true;

        return false;
    }

    /** A note that starts off the beat and is still sounding when the next one
        arrives, which is what makes a rhythm feel syncopated. */
    bool hasOffbeatNote (const model::Melody& melody)
    {
        const auto group = juce::jmax (1, melody.getTimeSignature().beamGroupTicks());

        for (const auto& event : melody.getEvents())
        {
            if (event.isRest)
                continue;

            const auto intoGroup = event.startTick % group;

            if (intoGroup != 0 && (event.tiedToNext || intoGroup + event.lengthTicks > group))
                return true;
        }

        return false;
    }

    int countOutsideKey (const practice::GeneratedExercise& exercise)
    {
        const auto root = exercise.scale.getRootPitchClass();
        const auto leadingTone = (root + 11) % 12;
        auto found = 0;

        for (auto note : exercise.melody.getRehearsalNotes())
            if (! exercise.scale.containsNote (note) && note % 12 != leadingTone)
                ++found;

        return found;
    }
}

//==============================================================================
int main()
{
    std::cout << "ExerciseTests" << std::endl;

    constexpr int lowest  = 55;   // G3
    constexpr int highest = 79;   // G5

    int generated = 0;

    // -- every combination of the four dials, with the features left off -------
    for (int intervalLevel = 1; intervalLevel <= practice::ExerciseGenerator::numLevels; ++intervalLevel)
    for (int rhythmLevel   = 1; rhythmLevel   <= practice::ExerciseGenerator::numLevels; ++rhythmLevel)
    for (int keyLevel      = 1; keyLevel      <= practice::ExerciseGenerator::numLevels; ++keyLevel)
    for (int lengthLevel   = 1; lengthLevel   <= practice::ExerciseGenerator::numLevels; ++lengthLevel)
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        practice::ExerciseSettings settings;
        settings.intervalLevel = intervalLevel;
        settings.rhythmLevel   = rhythmLevel;
        settings.keyLevel      = keyLevel;
        settings.lengthLevel   = lengthLevel;
        settings.randomiseKey  = true;
        settings.lowestNote    = lowest;
        settings.highestNote   = highest;
        settings.seed          = 1000 + attempt * 7919 + intervalLevel * 31
                                   + rhythmLevel * 131 + keyLevel * 517 + lengthLevel * 1231;

        const auto exercise = practice::ExerciseGenerator::generate (settings);
        ++generated;

        checkExercise (settings, exercise);
    }

    // -- and every grade, which is where the features are switched on ----------
    for (int grade = 1; grade <= practice::ExerciseGenerator::numGrades; ++grade)
    for (int attempt = 0; attempt < 120; ++attempt)
    {
        auto settings = practice::ExerciseGenerator::forGrade (grade);
        settings.lowestNote = lowest;
        settings.highestNote = highest;
        settings.seed = 300000 + grade * 9973 + attempt * 7919;

        const auto exercise = practice::ExerciseGenerator::generate (settings);
        ++generated;

        checkExercise (settings, exercise);
    }

    // -- a grade is exactly its dials ------------------------------------------
    for (int grade = 1; grade <= practice::ExerciseGenerator::numGrades; ++grade)
    {
        const auto settings = practice::ExerciseGenerator::forGrade (grade);

        checkEqual (settings.intervalLevel, grade, "grade sets the interval dial");
        checkEqual (settings.rhythmLevel,   grade, "grade sets the rhythm dial");
        checkEqual (settings.keyLevel,      grade, "grade sets the key dial");
        checkEqual (settings.lengthLevel,   grade, "grade sets the length dial");
    }

    // -- the levels really do get harder ---------------------------------------
    // Not decoration: a dial whose levels do not differ is a dial that does
    // nothing, which is what the old four-level scheme was accused of.
    {
        auto previousBars = 0;
        auto previousMetres = 0;

        for (int level = 1; level <= practice::ExerciseGenerator::numLevels; ++level)
        {
            const auto bars = practice::ExerciseGenerator::describeLengthLevel (level)
                                .getIntValue();
            const auto metres = (int) practice::ExerciseGenerator::metresForLevel (level).size();

            check (bars >= previousBars, "length never goes backwards");
            check (metres >= previousMetres, "the metres available never shrink");

            previousBars = bars;
            previousMetres = metres;
        }

        checkEqual (previousBars, 16, "the top length level is sixteen bars");
        checkEqual (previousMetres,
                    (int) practice::ExerciseGenerator::getAllMetres().size(),
                    "the top rhythm level can write in every metre");
    }

    // -- every metre fills its bars --------------------------------------------
    // The longer compound metres are assembled out of beat-groups rather than
    // taken whole from the corpus, so a bar that does not add up is a real risk.
    for (const auto& metre : practice::ExerciseGenerator::getAllMetres())
    {
        auto ok = true;
        auto used = false;

        for (int attempt = 0; attempt < 40; ++attempt)
        {
            practice::ExerciseSettings settings;
            settings.rhythmLevel = 8;
            settings.lengthLevel = 3;
            settings.randomiseKey = true;
            settings.lowestNote = lowest;
            settings.highestNote = highest;
            settings.timeSignatureNumerator   = metre.numerator;
            settings.timeSignatureDenominator = metre.denominator;
            settings.seed = 770000 + attempt * 7919 + metre.numerator * 13 + metre.denominator;

            const auto exercise = practice::ExerciseGenerator::generate (settings);
            const auto& melody = exercise.melody;

            if (melody.getRehearsalNotes().empty())
            {
                ok = false;
                continue;
            }

            used = melody.getTimeSignature() == metre;

            auto cursor = 0;

            for (const auto& event : melody.getEvents())
            {
                if (event.startTick != cursor)
                    ok = false;

                cursor = event.endTick();
            }

            if (cursor != melody.getTotalTicks())
                ok = false;
        }

        const auto name = juce::String (metre.numerator) + "/" + juce::String (metre.denominator);

        check (used, name + " is honoured when it is chosen");
        check (ok, name + " fills every bar exactly");
    }

    // -- rests: there when asked, absent when not -------------------------------
    {
        auto withRests = 0, withoutRests = 0;

        for (int attempt = 0; attempt < 120; ++attempt)
        {
            practice::ExerciseSettings settings;
            settings.rhythmLevel = 4;
            settings.lengthLevel = 3;
            settings.randomiseKey = true;
            settings.lowestNote = lowest;
            settings.highestNote = highest;
            settings.seed = 810000 + attempt * 7919;

            settings.rests = true;
            if (hasRest (practice::ExerciseGenerator::generate (settings).melody))
                ++withRests;

            settings.rests = false;
            if (hasRest (practice::ExerciseGenerator::generate (settings).melody))
                ++withoutRests;
        }

        check (withRests > 60, "rests turn up when they are switched on ("
                                 + juce::String (withRests) + "/120)");
        checkEqual (withoutRests, 0, "and never when they are not");
    }

    // -- the upbeat: a bar's worth of rest, then the pickup ---------------------
    for (const auto& metre : practice::ExerciseGenerator::getAllMetres())
    {
        practice::ExerciseSettings settings;
        settings.rhythmLevel = 8;
        settings.lengthLevel = 3;
        settings.upbeat = true;
        settings.randomiseKey = true;
        settings.lowestNote = lowest;
        settings.highestNote = highest;
        settings.timeSignatureNumerator   = metre.numerator;
        settings.timeSignatureDenominator = metre.denominator;
        settings.seed = 820000 + metre.numerator * 101 + metre.denominator;

        const auto exercise = practice::ExerciseGenerator::generate (settings);
        const auto& events = exercise.melody.getEvents();

        const auto name = juce::String (metre.numerator) + "/" + juce::String (metre.denominator);

        check (! events.empty() && events.front().isRest,
               name + " upbeat starts with a rest");

        checkEqual (exercise.melody.getBarCount(), 8 + 1,
                    name + " upbeat adds a bar for the pickup");

        // The pickup itself is short - it is what is left of the bar.
        auto restTicks = 0;

        for (const auto& event : events)
        {
            if (! event.isRest)
                break;

            restTicks += event.lengthTicks;
        }

        check (restTicks > 0 && restTicks < metre.barTicks(),
               name + " leaves room for a pickup note");
    }

    // -- ties over the barline --------------------------------------------------
    {
        auto tied = 0, untied = 0;

        for (int attempt = 0; attempt < 150; ++attempt)
        {
            practice::ExerciseSettings settings;
            settings.rhythmLevel = 6;
            settings.lengthLevel = 3;
            settings.randomiseKey = true;
            settings.lowestNote = lowest;
            settings.highestNote = highest;
            settings.seed = 830000 + attempt * 7919;

            settings.tiesOverBarlines = true;
            if (hasTieOverBarline (practice::ExerciseGenerator::generate (settings).melody))
                ++tied;

            settings.tiesOverBarlines = false;
            if (hasTieOverBarline (practice::ExerciseGenerator::generate (settings).melody))
                ++untied;
        }

        check (tied > 30, "notes are held over the barline when asked ("
                            + juce::String (tied) + "/150)");
        checkEqual (untied, 0, "and never otherwise");
    }

    // -- syncopation ------------------------------------------------------------
    {
        auto syncopated = 0;

        for (int attempt = 0; attempt < 150; ++attempt)
        {
            practice::ExerciseSettings settings;
            settings.rhythmLevel = 7;
            settings.lengthLevel = 3;
            settings.syncopation = true;
            settings.randomiseKey = true;
            settings.lowestNote = lowest;
            settings.highestNote = highest;
            settings.seed = 840000 + attempt * 7919;

            if (hasOffbeatNote (practice::ExerciseGenerator::generate (settings).melody))
                ++syncopated;
        }

        check (syncopated > 75, "syncopation turns up when it is switched on ("
                                  + juce::String (syncopated) + "/150)");
    }

    // -- accidentals from outside the key ---------------------------------------
    {
        auto coloured = 0, plain = 0;

        for (int attempt = 0; attempt < 150; ++attempt)
        {
            practice::ExerciseSettings settings;
            settings.intervalLevel = 5;
            settings.rhythmLevel = 5;
            settings.keyLevel = 5;
            settings.lengthLevel = 3;
            settings.randomiseKey = true;
            settings.lowestNote = lowest;
            settings.highestNote = highest;
            settings.seed = 850000 + attempt * 7919;

            settings.chromatic = true;
            coloured += countOutsideKey (practice::ExerciseGenerator::generate (settings));

            settings.chromatic = false;
            plain += countOutsideKey (practice::ExerciseGenerator::generate (settings));
        }

        check (coloured > 40, "accidentals appear when they are switched on ("
                                + juce::String (coloured) + ")");
        checkEqual (plain, 0, "and never when they are not");
    }

    // -- the three minor forms --------------------------------------------------
    {
        auto raisedIn = [] (practice::MinorForm form)
        {
            auto found = 0;

            for (int attempt = 0; attempt < 60; ++attempt)
            {
                practice::ExerciseSettings settings;
                settings.intervalLevel = 4;
                settings.rhythmLevel = 4;
                settings.keyLevel = 4;
                settings.lengthLevel = 3;
                settings.minorKey = true;
                settings.rootPitchClass = 9;      // A minor: no key signature to argue with
                settings.minorForm = form;
                settings.lowestNote = 55;
                settings.highestNote = 79;
                settings.seed = 860000 + attempt * 7919;

                const auto exercise = practice::ExerciseGenerator::generate (settings);

                for (auto note : exercise.melody.getRehearsalNotes())
                    if (note % 12 == 8)               // G sharp, the raised seventh
                        ++found;
            }

            return found;
        };

        const auto natural  = raisedIn (practice::MinorForm::natural);
        const auto melodic  = raisedIn (practice::MinorForm::melodic);
        const auto harmonic = raisedIn (practice::MinorForm::harmonic);

        checkEqual (natural, 0, "natural minor never raises the seventh");
        check (melodic > 0, "melodic minor raises it on the way to the tonic");
        check (harmonic > melodic, "harmonic minor raises it everywhere ("
                                     + juce::String (harmonic) + " against "
                                     + juce::String (melodic) + ")");
    }

    // -- the same seed gives the same exercise ---------------------------------
    {
        auto settings = practice::ExerciseGenerator::forGrade (7);
        settings.seed = 424242;

        const auto first  = practice::ExerciseGenerator::generate (settings);
        const auto second = practice::ExerciseGenerator::generate (settings);

        check (first.melody.getRehearsalNotes() == second.melody.getRehearsalNotes(),
               "a seed reproduces the same exercise");
        check (first.description == second.description, "and the same key and metre");
    }

    // -- a fixed key is honoured ----------------------------------------------
    {
        practice::ExerciseSettings settings;
        settings.keyLevel      = 4;
        settings.randomiseKey  = false;
        settings.rootPitchClass = 2;    // D
        settings.seed          = 99;

        const auto exercise = practice::ExerciseGenerator::generate (settings);
        check (exercise.scale.getRootPitchClass() == 2, "a chosen key is used");
    }

    // -- each clef's reading range ---------------------------------------------
    {
        int trebleLow = 0, trebleHigh = 0, bassLow = 0, bassHigh = 0;
        theory::getComfortableRange (true, trebleLow, trebleHigh);
        theory::getComfortableRange (false, bassLow, bassHigh);

        checkEqual (trebleLow, 60, "treble reads down to middle C");
        checkEqual (trebleHigh, 81, "and up to A5");
        checkEqual (bassLow, 40, "bass reads down to E2");
        checkEqual (bassHigh, 60, "and up to middle C");
        checkEqual (bassHigh, trebleLow, "the two clefs meet at middle C");

        // Generated at a clef's own range, nothing should need more than one
        // ledger line - which is the whole point of choosing the range this way.
        for (int attempt = 0; attempt < 60; ++attempt)
        {
            const auto treble = attempt % 2 == 0;

            auto settings = practice::ExerciseGenerator::forGrade (5);
            settings.trebleClef = treble;
            settings.seed = 900000 + attempt * 7919;

            theory::getComfortableRange (treble, settings.lowestNote, settings.highestNote);

            const auto exercise = practice::ExerciseGenerator::generate (settings);
            auto readable = true;

            for (auto note : exercise.melody.getRehearsalNotes())
                if (note < settings.lowestNote || note > settings.highestNote)
                    readable = false;

            check (readable, juce::String (treble ? "treble" : "bass")
                               + " exercises stay inside the clef's reading range");
        }
    }

    // -- the chosen clef is honoured ------------------------------------------
    {
        practice::ExerciseSettings settings;
        settings.trebleClef = false;
        settings.seed = 1234;

        const auto bass = practice::ExerciseGenerator::generate (settings);
        check (! bass.melody.isTrebleClef(), "bass clef survives generation");

        settings.trebleClef = true;
        const auto treble = practice::ExerciseGenerator::generate (settings);
        check (treble.melody.isTrebleClef(), "treble clef survives generation");
    }

    // -- a low voice gets low exercises ---------------------------------------
    {
        practice::ExerciseSettings settings;
        settings.keyLevel     = 4;
        settings.randomiseKey = true;
        settings.lowestNote   = 40;   // E2
        settings.highestNote  = 55;   // G3
        settings.seed         = 7;

        const auto exercise = practice::ExerciseGenerator::generate (settings);
        auto inRange = true;

        for (auto note : exercise.melody.getRehearsalNotes())
            if (note < 40 || note > 55)
                inRange = false;

        check (inRange, "a bass range is respected too");
    }

    // -- variety: the thing that made the old generator tiresome ---------------
    //
    // Worth asserting rather than eyeballing. An early version produced exactly
    // 16 distinct rhythms at level 1, because it had four cells and a fixed bar
    // pattern - a ceiling no amount of extra randomness could lift.
    for (int grade = 1; grade <= practice::ExerciseGenerator::numGrades; ++grade)
    {
        juce::StringArray melodies, rhythms;

        for (int i = 0; i < 200; ++i)
        {
            auto settings = practice::ExerciseGenerator::forGrade (grade);
            settings.lowestNote = lowest;
            settings.highestNote = highest;
            settings.seed = 500000 + i * 104729 + grade;

            const auto exercise = practice::ExerciseGenerator::generate (settings);

            juce::String pitches, rhythm;

            for (auto note : exercise.melody.getRehearsalNotes())
                pitches << juce::String (note) << ",";

            for (const auto& event : exercise.melody.getEvents())
                rhythm << juce::String (event.lengthTicks) << (event.isRest ? "r," : ",");

            melodies.addIfNotAlreadyThere (pitches);
            rhythms.addIfNotAlreadyThere (rhythm);
        }

        std::cout << "  grade " << grade << " of 200: " << melodies.size()
                  << " distinct melodies, " << rhythms.size() << " distinct rhythms"
                  << std::endl;

        check (melodies.size() >= 180,
               "grade " + juce::String (grade) + " gives varied melodies ("
                 + juce::String (melodies.size()) + "/200)");

        check (rhythms.size() >= 70,
               "grade " + juce::String (grade) + " gives varied rhythms ("
                 + juce::String (rhythms.size()) + "/200)");
    }

    std::cout << "generated " << generated << " exercises" << std::endl;
    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
