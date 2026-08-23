#include "ExerciseGenerator.h"
#include "CorpusData.h"
#include <algorithm>
#include <limits>
#include <functional>
#include <map>

namespace practice
{

namespace
{
    constexpr int compoundGroupTicks = 3 * model::ticksPerQuarter / 2;   // a dotted quarter

    //==========================================================================
    /** Picks an index in proportion to its weight. */
    int weightedChoice (const std::vector<int>& weights, juce::Random& random)
    {
        auto total = 0;

        for (auto weight : weights)
            total += juce::jmax (0, weight);

        if (total <= 0)
            return weights.empty() ? -1 : random.nextInt ((int) weights.size());

        auto target = random.nextInt (total);

        for (size_t i = 0; i < weights.size(); ++i)
        {
            target -= juce::jmax (0, weights[i]);

            if (target < 0)
                return (int) i;
        }

        return (int) weights.size() - 1;
    }

    //==========================================================================
    /** Triads by scale degree, as the degrees they contain (1-based). */
    std::vector<int> chordTones (int chordRootDegree)
    {
        auto wrap = [] (int degree) { return ((degree - 1) % 7 + 7) % 7 + 1; };

        return { wrap (chordRootDegree), wrap (chordRootDegree + 2), wrap (chordRootDegree + 4) };
    }

    /** A four-bar harmonic cycle, repeated to length and closed on the tonic.

        Repeating rather than inventing a fresh chord per bar is deliberate: the
        phrase structure below also works in four-bar units, so a restated bar
        lands on the harmony it was written over.
    */
    std::vector<int> chooseProgression (int bars, juce::Random& random)
    {
        static const std::vector<std::vector<int>> cycles
        {
            { 1, 4, 5, 1 }, { 1, 5, 1, 1 }, { 1, 6, 5, 1 },
            { 1, 4, 1, 5 }, { 1, 2, 5, 1 }, { 1, 5, 6, 4 },
        };

        const auto& cycle = cycles[(size_t) random.nextInt ((int) cycles.size())];

        std::vector<int> progression;

        for (int bar = 0; bar < juce::jmax (1, bars); ++bar)
            progression.push_back (cycle[(size_t) (bar % 4)]);

        progression.back() = 1;

        return progression;
    }

    //==========================================================================
    /** How a phrase is put together, bar by bar.

        -1 means "a fresh bar"; anything else means "say bar N again". Restating
        an earlier bar is what makes a phrase sound intended, and it is the
        cheapest source of both variety and coherence at once.

        Bars group into fours, and the groups restate each other as well - a a b
        a, a b a b - which is the structure real phrases have and the reason a
        sixteen-bar exercise can stay coherent instead of wandering for sixteen
        bars. The final bar is always the cadence and is always fresh.
    */
    std::vector<int> buildPhraseForm (int bars, juce::Random& random)
    {
        static const std::vector<std::vector<int>> withinGroup
        {
            { -1, -1,  0, -1 },     // a b a c
            { -1,  0, -1, -1 },     // a a b c
            { -1, -1, -1,  0 },     // a b c a
            { -1,  0,  1, -1 },     // a a b c, restating both
            { -1, -1,  0,  0 },     // a b a a
            { -1, -1, -1, -1 },     // all fresh
        };

        const auto groupSize = 4;
        const auto groups = (bars + groupSize - 1) / groupSize;

        // Which earlier group each group says again, or -1 for a fresh one.
        std::vector<int> plan ((size_t) groups, -1);

        if (groups == 2)
        {
            plan = { -1, 0 };                       // antecedent and consequent
        }
        else if (groups == 3)
        {
            static const std::vector<std::vector<int>> threes { { -1, 0, -1 }, { -1, -1, 0 } };
            plan = threes[(size_t) random.nextInt (2)];
        }
        else if (groups >= 4)
        {
            static const std::vector<std::vector<int>> fours
            {
                { -1, 0, -1, 0 },   // a b a b
                { -1, 0, -1, 1 },   // a a b a - the shape of most songs
                { -1, -1, 0, 1 },
                { -1, 0, -1, -1 },
            };

            const auto& chosen = fours[(size_t) random.nextInt ((int) fours.size())];

            for (int group = 0; group < groups; ++group)
            {
                const auto entry = chosen[(size_t) (group % 4)];

                plan[(size_t) group] = entry < 0 ? -1 : group - (group % 4) + entry;
            }
        }

        std::vector<int> form ((size_t) bars, -1);

        for (int group = 0; group < groups; ++group)
        {
            const auto base = group * groupSize;
            const auto restates = plan[(size_t) group];

            if (restates >= 0 && restates < group)
            {
                for (int i = 0; i < groupSize && base + i < bars; ++i)
                    form[(size_t) (base + i)] = restates * groupSize + i;

                continue;
            }

            const auto& shape = withinGroup[(size_t) random.nextInt ((int) withinGroup.size())];

            for (int i = 0; i < groupSize && base + i < bars; ++i)
                form[(size_t) (base + i)] = shape[(size_t) i] < 0 ? -1 : base + shape[(size_t) i];
        }

        form[0] = -1;
        form[(size_t) bars - 1] = -1;      // the cadence is written, not quoted

        return form;
    }

    //==========================================================================
    int maxLeapForLevel (int level)
    {
        // In scale steps: 1 is a second, 2 a third, and so on up to the octave.
        static const int leaps[] = { 1, 2, 3, 4, 5, 6, 7, 7 };

        return leaps[juce::jlimit (1, ExerciseGenerator::numLevels, level) - 1];
    }

    void compassForLevel (int level, int& lowestDegree, int& highestDegree)
    {
        // How far the line may roam, in scale degrees. A steps-only exercise
        // that wanders over a twelfth is not really a beginner's exercise any
        // more, however small each step was.
        static const int lows[]  = { -1, -2, -2, -3, -4, -5, -7, -7 };
        static const int highs[] = {  7,  8,  8,  9,  9, 10, 12, 12 };

        const auto index = (size_t) (juce::jlimit (1, ExerciseGenerator::numLevels, level) - 1);

        lowestDegree  = lows[index];
        highestDegree = highs[index];
    }

    /** How busy a bar the rhythm level may draw, on the corpus's own 1-4 scale
        of "shortest note in the bar, and whether anything is dotted". */
    int cellLevelForRhythmLevel (int level)
    {
        static const int cellLevels[] = { 1, 2, 2, 3, 4, 4, 4, 4 };

        return cellLevels[juce::jlimit (1, ExerciseGenerator::numLevels, level) - 1];
    }

    int barsForLengthLevel (int level)
    {
        static const int bars[] = { 4, 4, 8, 8, 8, 12, 12, 16 };

        return bars[juce::jlimit (1, ExerciseGenerator::numLevels, level) - 1];
    }

    /** Keys with at most this many sharps or flats, as pitch classes, majors. */
    std::vector<int> allowedRoots (int keyLevel)
    {
        // C, then a sharp key and a flat key for each accidental added.
        static const int sharpKeys[] = { 7, 2, 9, 4, 11, 6 };    // G D A E B F#
        static const int flatKeys[]  = { 5, 10, 3, 8, 1, 6 };    // F Bb Eb Ab Db Gb

        const auto level = juce::jlimit (1, ExerciseGenerator::numLevels, keyLevel);

        if (level >= 7)
            return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

        std::vector<int> roots { 0 };

        for (int accidentals = 1; accidentals <= level - 1; ++accidentals)
        {
            roots.push_back (sharpKeys[(size_t) accidentals - 1]);
            roots.push_back (flatKeys[(size_t) accidentals - 1]);
        }

        std::sort (roots.begin(), roots.end());
        roots.erase (std::unique (roots.begin(), roots.end()), roots.end());

        return roots;
    }

    //==========================================================================
    /** One bar's rhythm, or one beat-group of one, ready to be chosen from. */
    struct Pattern
    {
        std::vector<int> ticks;
        int level  = 1;
        int weight = 1;
        bool syncopated = false;
    };

    /** Does anything in this pattern start off the beat and hold across it?
        That is what syncopation is, and it is worth knowing because a reader
        who wants to practise it should not have to wait for it to turn up. */
    bool isSyncopated (const std::vector<int>& ticks, int groupTicks)
    {
        if (groupTicks <= 0)
            return false;

        auto position = 0;

        for (auto length : ticks)
        {
            const auto offset = position % groupTicks;

            if (offset != 0 && offset + length > groupTicks)
                return true;

            position += length;
        }

        return false;
    }

    /** Whole-bar rhythms for a metre.

        The corpus supplies 4/4, 3/4, 2/4 and 6/8 directly. Cut common time is
        the same bar length as 4/4 and takes its rhythms unchanged - alla breve
        differs in how it is felt and beamed, not in what note values fill the
        bar - and the longer compound metres are built from beat-groups below.
    */
    const std::vector<Pattern>& getWholeBars (int numerator, int denominator)
    {
        static std::map<std::pair<int, int>, std::vector<Pattern>> cache;
        static juce::CriticalSection lock;

        const juce::ScopedLock guard (lock);

        const auto key = std::make_pair (numerator, denominator);
        const auto existing = cache.find (key);

        if (existing != cache.end())
            return existing->second;

        auto sourceNumerator = numerator;
        auto sourceDenominator = denominator;

        if (numerator == 2 && denominator == 2)
        {
            sourceNumerator = 4;
            sourceDenominator = 4;
        }

        const model::TimeSignature signature { numerator, denominator };
        const auto groupTicks = signature.beamGroupTicks();

        std::vector<Pattern> patterns;

        for (const auto& cell : corpus::getRhythmCells())
        {
            if (cell.numerator != sourceNumerator || cell.denominator != sourceDenominator
                 || cell.ticks.empty())
                continue;

            Pattern pattern;
            pattern.ticks      = cell.ticks;
            pattern.level      = cell.level;
            pattern.weight     = cell.weight;
            pattern.syncopated = isSyncopated (cell.ticks, groupTicks);

            patterns.push_back (std::move (pattern));
        }

        return cache.emplace (key, std::move (patterns)).first->second;
    }

    /** The dotted-quarter beat-groups the corpus's 6/8 bars are made of.

        9/8 and 12/8 are not in the corpus in any useful quantity - slip jigs
        are rare and nothing is in 12/8 at all - but they are the same beat as
        6/8, three or four of them to the bar. Splitting the 6/8 bars into their
        halves gives a vocabulary of real compound beats to build the longer
        bars from, rather than a hand-written list of guesses.
    */
    const std::vector<Pattern>& getCompoundGroups()
    {
        static const std::vector<Pattern> groups = []
        {
            std::map<std::vector<int>, Pattern> found;

            for (const auto& cell : corpus::getRhythmCells())
            {
                if (cell.numerator != 6 || cell.denominator != 8)
                    continue;

                std::vector<std::vector<int>> halves { {} };
                auto position = 0;
                auto usable = true;

                for (auto length : cell.ticks)
                {
                    if (position == compoundGroupTicks)
                        halves.push_back ({});

                    // A note straddling the middle of the bar belongs to no one
                    // group, so the bar it came from is no use here.
                    if (position < compoundGroupTicks && position + length > compoundGroupTicks)
                    {
                        usable = false;
                        break;
                    }

                    halves.back().push_back (length);
                    position += length;
                }

                if (! usable || position != 2 * compoundGroupTicks || halves.size() != 2)
                    continue;

                for (const auto& half : halves)
                {
                    if (half.empty())
                        continue;

                    auto& pattern = found[half];
                    pattern.ticks   = half;
                    pattern.level   = pattern.weight == 0 ? cell.level
                                                          : juce::jmin (pattern.level, cell.level);
                    pattern.weight += cell.weight;
                }
            }

            std::vector<Pattern> result;

            for (auto& entry : found)
                result.push_back (entry.second);

            return result;
        }();

        return groups;
    }

    /** True when a bar ends on something long enough to land a cadence on. */
    bool endsLongEnough (const std::vector<int>& ticks, bool compound)
    {
        return ! ticks.empty()
                 && ticks.back() >= (compound ? compoundGroupTicks : model::ticksPerQuarter);
    }

    //==========================================================================
    /** Chooses one bar of rhythm in a metre, at or below a difficulty.

        Widens the level until something is available rather than coming back
        empty: some metres have no bars at the easiest level at all - compound
        time is built from dotted beats - and an empty exercise would be worse
        than a slightly busier one.
    */
    std::vector<int> pickBar (model::TimeSignature signature, int maxCellLevel,
                              bool cadential, bool preferSyncopation, juce::Random& random)
    {
        const auto compound = signature.denominator == 8 && signature.numerator % 3 == 0
                                && signature.numerator > 3;

        auto choose = [&] (const std::vector<Pattern>& from, bool wantCadential)
                          -> const Pattern*
        {
            for (auto level = maxCellLevel; level <= 4; ++level)
            {
                std::vector<const Pattern*> allowed;
                std::vector<int> weights;

                for (const auto& pattern : from)
                {
                    if (pattern.level > level)
                        continue;

                    if (wantCadential && ! endsLongEnough (pattern.ticks, compound))
                        continue;

                    auto weight = pattern.weight;

                    if (preferSyncopation && pattern.syncopated)
                        weight *= 8;

                    allowed.push_back (&pattern);
                    weights.push_back (juce::jmax (1, weight));
                }

                if (! allowed.empty())
                    return allowed[(size_t) juce::jmax (0, weightedChoice (weights, random))];
            }

            return nullptr;
        };

        const auto& wholeBars = getWholeBars (signature.numerator, signature.denominator);

        if (! wholeBars.empty())
        {
            if (const auto* pattern = choose (wholeBars, cadential))
                return pattern->ticks;

            if (const auto* pattern = choose (wholeBars, false))
                return pattern->ticks;

            return {};
        }

        // The long compound metres: three or four real compound beats.
        const auto& groups = getCompoundGroups();

        if (groups.empty())
            return {};

        const auto groupCount = juce::jmax (1, signature.numerator / 3);

        std::vector<int> ticks;

        for (int group = 0; group < groupCount; ++group)
        {
            const auto last = group == groupCount - 1;
            const auto* pattern = choose (groups, cadential && last);

            if (pattern == nullptr)
                pattern = choose (groups, false);

            if (pattern == nullptr)
                return {};

            ticks.insert (ticks.end(), pattern->ticks.begin(), pattern->ticks.end());
        }

        return ticks;
    }

    //==========================================================================
    /** The corpus transitions, indexed by the degree they start from. */
    struct TransitionTable
    {
        std::array<std::vector<int>, 7> intervals;
        std::array<std::vector<int>, 7> weights;
    };

    const TransitionTable& getTransitionTable()
    {
        static const TransitionTable table = []
        {
            TransitionTable built;

            for (const auto& transition : corpus::getTransitions())
            {
                const auto degree = juce::jlimit (0, 6, transition.fromDegree);
                built.intervals[(size_t) degree].push_back (transition.interval);
                built.weights[(size_t) degree].push_back (transition.weight);
            }

            return built;
        }();

        return table;
    }

    //==========================================================================
    /** One written slot: a note or a rest, and how long it lasts. */
    struct Slot
    {
        int  ticks = 0;
        bool rest  = false;
    };

    struct Bar
    {
        std::vector<Slot> slots;
        int source = -1;      ///< which earlier bar this restates, or -1
    };

    int countNotes (const std::vector<Bar>& bars)
    {
        auto total = 0;

        for (const auto& bar : bars)
            for (const auto& slot : bar.slots)
                if (! slot.rest)
                    ++total;

        return total;
    }

    /** How long an upbeat should be: one beat in simple time, one eighth in
        compound, which is what tunes that start before the barline do. */
    int upbeatTicksFor (model::TimeSignature signature)
    {
        const auto compound = signature.denominator == 8 && signature.numerator % 3 == 0
                                && signature.numerator > 3;

        const auto ticks = compound ? model::ticksPerQuarter / 2 : model::ticksPerQuarter;

        return juce::jmin (ticks, signature.barTicks() / 2);
    }
}

//==============================================================================
juce::String ExerciseGenerator::describeIntervalLevel (int level)
{
    static const char* const names[]
    {
        "steps only",
        "adds thirds",
        "adds fourths",
        "the whole triad",
        "adds sixths",
        "adds sevenths",
        "up to the octave",
        "octaves and accidentals",
    };

    return names[juce::jlimit (1, numLevels, level) - 1];
}

juce::String ExerciseGenerator::describeRhythmLevel (int level)
{
    static const char* const names[]
    {
        "halves and quarters",
        "adds eighths",
        "adds 2/4",
        "adds dotted figures",
        "adds sixteenths and 6/8",
        "adds cut common time",
        "adds 9/8",
        "adds 12/8",
    };

    return names[juce::jlimit (1, numLevels, level) - 1];
}

juce::String ExerciseGenerator::describeKeyLevel (int level)
{
    static const char* const names[]
    {
        "C major",
        "1 sharp or flat",
        "2 sharps or flats, minor",
        "3 sharps or flats",
        "4 sharps or flats",
        "5 sharps or flats",
        "6 sharps or flats",
        "any key",
    };

    return names[juce::jlimit (1, numLevels, level) - 1];
}

juce::String ExerciseGenerator::describeLengthLevel (int level)
{
    return juce::String (barsForLengthLevel (level)) + " bars";
}

//==============================================================================
const std::vector<model::TimeSignature>& ExerciseGenerator::getAllMetres()
{
    return model::getWritableMetres();
}

std::vector<model::TimeSignature> ExerciseGenerator::metresForLevel (int rhythmLevel)
{
    // Each level keeps everything below it, in the order graded reading meets
    // them: the simple metres first, then compound time, then the ones that
    // need a different beat felt underneath.
    static const std::vector<std::vector<model::TimeSignature>> added
    {
        { { 4, 4 }, { 3, 4 } },
        { },
        { { 2, 4 } },
        { },
        { { 6, 8 } },
        { { 2, 2 } },
        { { 9, 8 } },
        { { 12, 8 } },
    };

    std::vector<model::TimeSignature> metres;

    for (int level = 0; level < juce::jlimit (1, numLevels, rhythmLevel); ++level)
        for (const auto& signature : added[(size_t) level])
            metres.push_back (signature);

    return metres;
}

//==============================================================================
ExerciseSettings ExerciseGenerator::forGrade (int grade)
{
    const auto level = juce::jlimit (1, numGrades, grade);

    ExerciseSettings settings;
    settings.intervalLevel = level;
    settings.rhythmLevel   = level;
    settings.keyLevel      = level;
    settings.lengthLevel   = level;

    // Where each feature joins in. Rests come early because real music is full
    // of them from the first year; the ones that change how the beat is felt -
    // ties over the barline, syncopation - come once the reader is secure in
    // the metre, and accidentals from outside the key come last.
    settings.rests            = level >= 3;
    settings.upbeat           = level >= 4;
    settings.tiesOverBarlines = level >= 6;
    settings.syncopation      = level >= 7;
    settings.chromatic        = level >= 8;

    // The melodic form raises the seventh only where it leads to the tonic;
    // the harmonic form raises it everywhere, augmented second and all, which
    // is the harder thing to pitch and so comes later.
    settings.minorForm = level >= 6 ? MinorForm::harmonic : MinorForm::melodic;
    settings.randomiseKey = true;

    return settings;
}

juce::String ExerciseGenerator::describeGrade (int grade)
{
    const auto level = juce::jlimit (1, numGrades, grade);

    // Deliberately not the whole story - the rhythm dial and the feature
    // switches say the rest. This is what you scan a menu for.
    return "Grade " + juce::String (level) + "  -  " + describeKeyLevel (level)
             + ", " + describeIntervalLevel (level)
             + ", " + describeLengthLevel (level);
}

//==============================================================================
GeneratedExercise ExerciseGenerator::generate (const ExerciseSettings& settings)
{
    GeneratedExercise result;

    result.seed = settings.seed != 0 ? settings.seed
                                     : (juce::int64) juce::Time::getHighResolutionTicks();

    juce::Random random ((juce::int64) result.seed);

    const auto intervalLevel = juce::jlimit (1, numLevels, settings.intervalLevel);
    const auto rhythmLevel   = juce::jlimit (1, numLevels, settings.rhythmLevel);
    const auto keyLevel      = juce::jlimit (1, numLevels, settings.keyLevel);
    const auto lengthLevel   = juce::jlimit (1, numLevels, settings.lengthLevel);
    const auto maxLeap       = maxLeapForLevel (intervalLevel);

    // --- key -----------------------------------------------------------------
    auto root  = ((settings.rootPitchClass % 12) + 12) % 12;
    auto minor = settings.minorKey;

    if (settings.randomiseKey)
    {
        const auto roots = allowedRoots (keyLevel);
        root  = roots[(size_t) random.nextInt ((int) roots.size())];
        minor = keyLevel >= 3 && random.nextInt (3) == 0;

        // The allowed roots are major keys, so a minor one takes the relative
        // minor and keeps the signature the level asked for.
        if (minor)
            root = (root + 9) % 12;
    }
    else if (keyLevel < 3)
    {
        minor = false;
    }

    result.scale = theory::Scale (root, minor ? 1 : 0);

    // --- metre and length ----------------------------------------------------
    model::TimeSignature signature { 4, 4 };

    if (settings.timeSignatureNumerator > 0 && settings.timeSignatureDenominator > 0)
    {
        signature = { settings.timeSignatureNumerator, settings.timeSignatureDenominator };
    }
    else
    {
        const auto metres = metresForLevel (rhythmLevel);

        if (! metres.empty())
            signature = metres[(size_t) random.nextInt ((int) metres.size())];
    }

    const auto bars = barsForLengthLevel (lengthLevel);
    const auto maxCellLevel = cellLevelForRhythmLevel (rhythmLevel);

    // --- rhythm, taken from what real tunes actually play ---------------------
    const auto form = buildPhraseForm (bars, random);

    std::vector<Bar> written;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto restates = form[(size_t) bar];

        if (restates >= 0 && restates < (int) written.size())
        {
            Bar copy;
            copy.slots  = written[(size_t) restates].slots;
            copy.source = restates;
            written.push_back (std::move (copy));
            continue;
        }

        const auto cadential = bar == bars - 1;
        const auto ticks = pickBar (signature, maxCellLevel, cadential,
                                    settings.syncopation, random);

        if (ticks.empty())
            return result;

        Bar fresh;

        for (auto length : ticks)
            fresh.slots.push_back ({ length, false });

        written.push_back (std::move (fresh));
    }

    // An exercise can legitimately draw a whole bar's worth of note in every
    // bar and end up with almost nothing to read. Real graded tests average
    // rather more than one note to the bar, so the emptiest bars are rewritten
    // busier until there is something on the page - which is a better answer
    // than rejecting the seed and trying again, because it keeps the phrase
    // structure that was already chosen.
    //
    // Run twice: once here, and again after the rests, since turning notes into
    // rests can take the count back under.
    const auto wantedNotes = juce::jmax (4, bars * 3 / 2);

    auto fillOut = [&]
    {
        for (int attempt = 0; attempt < 12 && countNotes (written) < wantedNotes; ++attempt)
        {
            auto sparsest = -1;

            for (size_t bar = 0; bar < written.size(); ++bar)
            {
                if (written[bar].source >= 0)
                    continue;

                if (sparsest < 0
                     || written[bar].slots.size() < written[(size_t) sparsest].slots.size())
                    sparsest = (int) bar;
            }

            if (sparsest < 0)
                break;

            auto busiest = pickBar (signature, maxCellLevel, false, settings.syncopation, random);

            for (int pick = 0; pick < 6; ++pick)
            {
                const auto candidate = pickBar (signature, maxCellLevel, false,
                                                settings.syncopation, random);

                if (candidate.size() > busiest.size())
                    busiest = candidate;
            }

            if (busiest.size() <= written[(size_t) sparsest].slots.size())
                break;      // the metre has nothing busier to offer

            written[(size_t) sparsest].slots.clear();

            for (auto length : busiest)
                written[(size_t) sparsest].slots.push_back ({ length, false });
        }
    };

    fillOut();

    if (countNotes (written) < 3)
        return result;

    // --- rests, where a singer would take a breath ---------------------------
    if (settings.rests)
    {
        for (size_t bar = 0; bar + 1 < written.size(); ++bar)
        {
            if (written[bar].source >= 0)
                continue;               // a copy; it inherits whatever it copies

            auto& slots = written[bar].slots;

            if (slots.size() < 3 || slots.back().rest
                 || slots.back().ticks > model::ticksPerQuarter)
                continue;

            // A rest belongs at the end of a phrase, which is every fourth bar.
            // Elsewhere it turns up only occasionally, so the reader cannot
            // simply learn where to expect one.
            const auto chance = bar % 4 == 3 ? 4 : 1;

            if (random.nextInt (5) >= chance)
                continue;

            slots.back().rest = true;
        }
    }

    fillOut();

    // Restated bars are re-made from what they restate, so anything done above
    // to a bar reaches every bar that quotes it - including a bar quoting a bar
    // that was itself a quotation, which is why this runs in order.
    for (size_t bar = 1; bar < written.size(); ++bar)
        if (written[bar].source >= 0 && written[bar].source < (int) bar)
            written[bar].slots = written[(size_t) written[bar].source].slots;

    // --- the upbeat, an incomplete bar before the first barline ---------------
    // Written as a full bar opening with rests rather than as a short bar: the
    // model's own rule is that every bar is full, and the two read identically.
    auto upbeat = false;

    if (settings.upbeat)
    {
        const auto lead = upbeatTicksFor (signature);

        if (lead > 0 && lead < signature.barTicks())
        {
            Bar pickup;
            pickup.slots.push_back ({ signature.barTicks() - lead, true });
            pickup.slots.push_back ({ lead, false });

            for (auto& bar : written)
                if (bar.source >= 0)
                    ++bar.source;

            written.insert (written.begin(), std::move (pickup));
            upbeat = true;
        }
    }

    const auto totalNotes = countNotes (written);

    if (totalNotes < 3)
        return result;

    // --- pitches -------------------------------------------------------------
    const auto& transitions = getTransitionTable();
    auto progression = chooseProgression (bars, random);

    // The upbeat belongs to the first bar's harmony, not to a bar of its own.
    if (upbeat)
        progression.insert (progression.begin(), 1);

    int lowestDegree = 0, highestDegree = 0;
    compassForLevel (intervalLevel, lowestDegree, highestDegree);

    std::vector<int> degrees;
    degrees.reserve ((size_t) totalNotes);

    // Tunes rarely begin on the tonic - the fifth and the third are commoner,
    // and the corpus says so.
    {
        std::vector<int> candidates, weights;

        for (const auto& opening : corpus::getOpenings())
        {
            if (opening.degree < lowestDegree || opening.degree > highestDegree)
                continue;

            candidates.push_back (opening.degree + 1);   // the corpus counts from zero
            weights.push_back (opening.weight);
        }

        const auto choice = weightedChoice (weights, random);
        degrees.push_back (choice >= 0 ? candidates[(size_t) choice] : 1);
    }

    /** Samples the next degree from the corpus, refusing anything the level or
        the range rules out. */
    auto stepFrom = [&] (int current, bool wantChordTone, const std::vector<int>& tones)
    {
        const auto index = (size_t) (((current - 1) % 7 + 7) % 7);
        const auto& options = transitions.intervals[index];
        const auto& optionWeights = transitions.weights[index];

        std::vector<int> allowed, allowedWeights;

        for (size_t i = 0; i < options.size(); ++i)
        {
            const auto interval = options[i];
            const auto next = current + interval;

            if (std::abs (interval) > maxLeap)
                continue;

            if (next < lowestDegree || next > highestDegree)
                continue;

            auto weight = optionWeights[i];

            // Downbeats are nudged towards the prevailing harmony rather than
            // forced onto it: forcing flattens the line, and the corpus already
            // leans that way by itself.
            if (wantChordTone)
            {
                const auto within = ((next - 1) % 7 + 7) % 7 + 1;

                if (std::find (tones.begin(), tones.end(), within) != tones.end())
                    weight *= 4;
            }

            // Tessitura. The corpus transitions know nothing about where in the
            // range the line has got to, so on their own they random-walk and
            // drift off the bottom of the staff. Straying from the middle is
            // damped in proportion to how far out it already is.
            const auto centre = (lowestDegree + highestDegree) / 2;
            const auto strayed = next - centre;

            if ((strayed > 1 && interval > 0) || (strayed < -1 && interval < 0))
                weight = juce::jmax (1, weight / (1 + std::abs (strayed) * std::abs (strayed)));

            allowed.push_back (next);
            allowedWeights.push_back (weight);
        }

        if (allowed.empty())
            return juce::jlimit (lowestDegree, highestDegree,
                                 current + (current > 1 ? -1 : 1));

        return allowed[(size_t) juce::jmax (0, weightedChoice (allowedWeights, random))];
    };

    // Walk bar by bar, so a restated bar can reuse the shape of the one it
    // restates instead of being invented afresh.
    std::vector<std::vector<int>> barIntervals (written.size());
    std::vector<int> barStart (written.size(), 1);

    for (size_t bar = 0; bar < written.size(); ++bar)
    {
        const auto chord = progression[juce::jmin (bar, progression.size() - 1)];
        const auto tones = chordTones (chord);
        const auto source = written[bar].source;
        const auto restating = source >= 0 && ! barIntervals[(size_t) source].empty();

        auto noteInBar = 0;

        for (const auto& slot : written[bar].slots)
        {
            if (slot.rest)
                continue;

            if (degrees.size() == 1 && noteInBar == 0 && bar == 0)
            {
                barStart[0] = degrees.back();
                ++noteInBar;
                continue;   // the opening degree is already placed
            }

            const auto current = degrees.back();
            int next;

            if (noteInBar == 0)
            {
                // A restatement begins where the bar it restates began. Picking
                // up from wherever the previous bar happened to end would make
                // each repeat start lower than the last, and the whole line
                // would sink - which is exactly what it did.
                next = restating ? barStart[(size_t) source]
                                 : stepFrom (current, true, tones);
            }
            else if (restating && noteInBar - 1 < (int) barIntervals[(size_t) source].size())
            {
                next = juce::jlimit (lowestDegree, highestDegree,
                                     current + barIntervals[(size_t) source][(size_t) noteInBar - 1]);
            }
            else
            {
                next = stepFrom (current, false, tones);
            }

            next = juce::jlimit (lowestDegree, highestDegree, next);

            if (noteInBar == 0)
                barStart[bar] = next;
            else
                barIntervals[bar].push_back (next - current);

            degrees.push_back (next);
            ++noteInBar;
        }
    }

    degrees.resize ((size_t) totalNotes, degrees.empty() ? 1 : degrees.back());

    // --- cadence, using a formula tunes actually finish with -----------------
    if (degrees.size() >= 3)
    {
        std::vector<int> weights;

        for (const auto& cadence : corpus::getCadences())
            weights.push_back (cadence.weight);

        const auto choice = weightedChoice (weights, random);

        if (choice >= 0)
        {
            const auto& cadence = corpus::getCadences()[(size_t) choice];

            degrees[degrees.size() - 1] = 1;
            degrees[degrees.size() - 2] = 1 + cadence.secondLast;
            degrees[degrees.size() - 3] = 1 + cadence.thirdLast;
        }
    }
    else if (! degrees.empty())
    {
        degrees.back() = 1;
    }

    // --- keep every interval inside the level, working back from the cadence -
    for (int i = (int) degrees.size() - 1; i >= 1; --i)
    {
        const auto interval = degrees[(size_t) i] - degrees[(size_t) i - 1];

        if (std::abs (interval) > maxLeap)
            degrees[(size_t) i - 1] = degrees[(size_t) i]
                                        - (interval > 0 ? maxLeap : -maxLeap);
    }

    // --- degrees to pitches, fitted to the singer's range ---------------------
    const auto lowest  = juce::jmin (settings.lowestNote, settings.highestNote);
    const auto highest = juce::jmax (settings.lowestNote, settings.highestNote);

    // Degrees become staff positions by counting up from the tonic's own letter,
    // and the key signature then decides the pitch - so an exercise in D major
    // gets its F sharps without the generator knowing anything about them.
    const auto tonicLetter = result.scale.spell (60 + root).letter;

    auto pitchForDegree = [&result, minor, tonicLetter] (int degree, int octave, bool raiseSeventh)
    {
        const auto step = octave * 7 + tonicLetter + (degree - 1);
        auto note = result.scale.noteForDiatonicStep (step);

        if (raiseSeventh && minor && ((degree - 1) % 7 + 7) % 7 == 6)
            ++note;

        return note;
    };

    auto bestOctave = 4;
    auto bestPenalty = std::numeric_limits<int>::max();

    for (int octave = 2; octave <= 6; ++octave)
    {
        auto penalty = 0;

        for (auto degree : degrees)
        {
            const auto note = pitchForDegree (degree, octave, false);

            if (note < lowest)  penalty += lowest - note;
            if (note > highest) penalty += note - highest;
        }

        if (penalty < bestPenalty)
        {
            bestPenalty = penalty;
            bestOctave = octave;
        }
    }

    // Confine the line to the degrees that fit, rather than octave-shifting
    // stray notes: moving one note by an octave would open a gaping leap in the
    // middle of a phrase. Clamping degrees can only make intervals smaller.
    auto lowestFitting = -21;
    auto highestFitting = 21;

    for (int degree = -21; degree <= 28; ++degree)
    {
        if (pitchForDegree (degree, bestOctave, false) >= lowest)
        {
            lowestFitting = degree;
            break;
        }
    }

    for (int degree = 28; degree >= -21; --degree)
    {
        if (pitchForDegree (degree, bestOctave, false) <= highest)
        {
            highestFitting = degree;
            break;
        }
    }

    // Two separate limits apply: what the voice can reach, and how far the level
    // lets the line roam. The voice one is kept separately because the cadence
    // has to be singable even when the compass has no tonic inside it.
    const auto voiceLowDegree  = lowestFitting;
    const auto voiceHighDegree = highestFitting;

    lowestFitting  = juce::jmax (voiceLowDegree,  lowestDegree);
    highestFitting = juce::jmin (voiceHighDegree, highestDegree);

    if (highestFitting < lowestFitting)
    {
        lowestFitting  = voiceLowDegree;
        highestFitting = voiceHighDegree;
    }

    for (auto& degree : degrees)
        degree = juce::jlimit (lowestFitting, highestFitting, degree);

    // Clamping may have moved the cadence, so put it back on a tonic that fits.
    {
        // The tonic has to be one the singer can reach. Staying inside the
        // level's compass as well is preferred but not required - a key whose
        // tonic falls just below the compass would otherwise end the exercise
        // on a note outside the range entirely.
        auto tonicDegree = juce::jlimit (voiceLowDegree, voiceHighDegree, 1);
        auto bestScore = std::numeric_limits<int>::max();

        for (int octaveShift = -3; octaveShift <= 3; ++octaveShift)
        {
            const auto candidate = 1 + octaveShift * 7;

            if (candidate < voiceLowDegree || candidate > voiceHighDegree)
                continue;

            const auto insideCompass = candidate >= lowestFitting && candidate <= highestFitting;
            const auto score = std::abs (candidate - degrees.back()) + (insideCompass ? 0 : 100);

            if (score < bestScore)
            {
                bestScore = score;
                tonicDegree = candidate;
            }
        }

        // Whatever was chosen, the rest of the line must be able to reach it.
        lowestFitting  = juce::jmin (lowestFitting,  tonicDegree - 1);
        highestFitting = juce::jmax (highestFitting, tonicDegree + 1);
        lowestFitting  = juce::jmax (lowestFitting,  voiceLowDegree);
        highestFitting = juce::jmin (highestFitting, voiceHighDegree);

        degrees.back() = tonicDegree;

        if (degrees.size() >= 2)
        {
            const auto before = degrees.size() >= 3 ? degrees[degrees.size() - 3] : tonicDegree;
            auto penultimate = degrees[degrees.size() - 2];

            // Leave the corpus's own approach alone when it is a step or a
            // repeated tonic - tunes really do end that way, and overwriting it
            // would throw away the thing the corpus was consulted for. Only a
            // leap into the cadence gets replaced.
            if (std::abs (penultimate - tonicDegree) > 1)
                penultimate = before >= tonicDegree ? tonicDegree + 1 : tonicDegree - 1;

            // Fold it inside the singable window without letting it land on the
            // tonic's own degree by accident.
            if (penultimate > highestFitting) penultimate = tonicDegree - 1;
            if (penultimate < lowestFitting)  penultimate = tonicDegree + 1;

            degrees[degrees.size() - 2] = penultimate;
        }

        // Tidy the run into the cadence backwards, so the ending stays put and
        // everything leading to it gives way.
        for (int i = (int) degrees.size() - 2; i >= 1; --i)
        {
            const auto interval = degrees[(size_t) i] - degrees[(size_t) i - 1];

            if (std::abs (interval) > maxLeap)
                degrees[(size_t) i - 1] = degrees[(size_t) i]
                                            - (interval > 0 ? maxLeap : -maxLeap);
        }

        // Clamp everything except the cadence, which is already inside the
        // window: clamping it again could collapse the last two notes together.
        for (size_t i = 0; i + 2 < degrees.size(); ++i)
            degrees[i] = juce::jlimit (lowestFitting, highestFitting, degrees[i]);
    }

    // --- the minor seventh, raised or not ------------------------------------
    /** Whether the seventh is written raised at this point in the line. The
        answer depends on the notes either side of it in the melodic form, so it
        is a function of the degrees rather than something decided once: any
        later change to the degrees has to be able to ask again. */
    auto raisesSeventhAt = [&degrees, minor, &settings] (size_t i)
    {
        if (! minor)
            return false;

        switch (settings.minorForm)
        {
            case MinorForm::natural:
                return false;

            case MinorForm::harmonic:
                // Always raised, wherever it falls. It is the harder read of the
                // three - the augmented second above the sixth is the whole
                // point - and it is where graded minor keys go next.
                return true;

            case MinorForm::melodic:
            default:
                break;
        }

        // Raised only where it steps up to the tonic, which is what melodic
        // practice does and avoids leaving an augmented second mid-phrase.
        const auto rising = i + 1 < degrees.size() && (degrees[i + 1] - degrees[i]) == 1
                              && ((degrees[i + 1] - 1) % 7 + 7) % 7 == 0;

        const auto approachedFromSixth = i > 0
                                           && (((degrees[i - 1] - 1) % 7 + 7) % 7 == 5);

        return rising && ! approachedFromSixth;
    };

    std::vector<int> pitches;
    std::vector<char> raised (degrees.size(), 0);      ///< where a raise landed
    std::vector<char> keepNatural (degrees.size(), 0); ///< where one was withdrawn

    auto rebuildPitches = [&]
    {
        pitches.clear();
        raised.assign (degrees.size(), 0);

        for (size_t i = 0; i < degrees.size(); ++i)
        {
            const auto seventh = ((degrees[i] - 1) % 7 + 7) % 7 == 6;
            const auto raise = seventh && ! keepNatural[i] && raisesSeventhAt (i);

            auto note = pitchForDegree (degrees[i], bestOctave, raise);

            // Raising the top note of the range takes it outside the range.
            // Reading a natural seventh there is better than being handed a
            // note you cannot sing.
            if (raise && (note < lowest || note > highest))
                note = pitchForDegree (degrees[i], bestOctave, false);
            else if (raise)
                raised[i] = 1;

            pitches.push_back (juce::jlimit (0, 127, note));
        }
    };

    rebuildPitches();

    // --- take out any tritone leaps ------------------------------------------
    // Four scale degrees is a reasonable leap, except between the fourth and
    // seventh degrees, where it spells a tritone: hard to pitch, and not what a
    // sight-singing exercise should ask for. It only appears once degrees have
    // become pitches - and a raised seventh makes new ones the degrees alone
    // cannot see - so it is repaired here, always by narrowing the leap, and
    // the pitches are rebuilt each pass so a repair cannot leave the two
    // descriptions of the same note disagreeing.
    for (int pass = 0; pass < 6; ++pass)
    {
        auto changed = false;

        for (size_t i = 1; i < pitches.size(); ++i)
        {
            if (std::abs (pitches[i] - pitches[i - 1]) != 6)
                continue;

            // A raised seventh against the fourth is a tritone that exists only
            // in the pitches - the degrees are a fourth apart and look fine.
            // Writing that seventh natural is exactly what the melodic form of
            // the scale does about it, and it is a better answer than shunting
            // the note somewhere it was not going.
            if (raised[i] || raised[i - 1])
            {
                keepNatural[raised[i] ? i : i - 1] = 1;
                changed = true;
                continue;
            }

            const auto firstProtected = pitches.size() >= 2 ? pitches.size() - 2 : pitches.size();
            const auto moveIndex = i >= firstProtected ? i - 1 : i;
            const auto reference = moveIndex == i ? pitches[i - 1] : pitches[i];
            const auto direction = pitches[moveIndex] > reference ? -1 : 1;

            const auto updated = juce::jlimit (lowestFitting, highestFitting,
                                               degrees[moveIndex] + direction);

            if (updated == degrees[moveIndex])
                continue;

            degrees[moveIndex] = updated;
            changed = true;
        }

        if (! changed)
            break;

        rebuildPitches();
    }

    // --- notes from outside the key ------------------------------------------
    // The three chromatic inflections that turn up first in real music and in
    // sight-singing courses: the fourth raised on its way to the fifth, the
    // seventh lowered on its way down to the sixth, the tonic raised on its way
    // up to the second. Each is a semitone leaning where it was already going,
    // so it never opens an interval the level did not ask for.
    auto accidentals = 0;

    if (settings.chromatic && pitches.size() > 4)
    {
        const auto allowance = juce::jmax (1, (int) written.size() / 4);

        for (size_t i = 1; i + 2 < pitches.size() && accidentals < allowance; ++i)
        {
            const auto within = ((degrees[i] - 1) % 7 + 7) % 7 + 1;
            const auto rise = degrees[i + 1] - degrees[i];

            auto shift = 0;

            if (within == 4 && rise == 1)                    shift =  1;
            else if (within == 7 && rise == -1 && ! minor)    shift = -1;
            else if (within == 1 && rise == 1)                shift =  1;

            if (shift == 0 || random.nextInt (3) == 0)
                continue;

            // Only if it does not disturb the step that arrives at it, and
            // only if the singer can still reach it.
            const auto altered = pitches[i] + shift;
            const auto approach = std::abs (altered - pitches[i - 1]);

            if (approach == 0 || approach > 2 || altered < lowest || altered > highest)
                continue;

            pitches[i] += shift;
            ++accidentals;
            i += 2;
        }
    }

    // --- assemble ------------------------------------------------------------
    struct Written { int ticks; bool rest; int pitch; };

    std::vector<Written> events;
    size_t noteIndex = 0;

    for (const auto& bar : written)
    {
        for (const auto& slot : bar.slots)
        {
            if (slot.rest)
            {
                events.push_back ({ slot.ticks, true, 0 });
                continue;
            }

            events.push_back ({ slot.ticks, false,
                                noteIndex < pitches.size() ? pitches[noteIndex] : 60 });
            ++noteIndex;
        }
    }

    // --- notes held past where they started ----------------------------------
    // Both a tie over the barline and a syncopation inside one are the same
    // written thing: one note lasting into where the next would have started.
    // The model splits and ties it for us, so all that is needed here is to
    // give one note the length of two.
    auto merge = [&events, maxLeap] (size_t index)
    {
        if (index + 3 >= events.size())
            return false;      // leave the cadence and its approach alone

        if (events[index].rest || events[index + 1].rest)
            return false;

        // Swallowing a note puts its neighbours next to each other, and two
        // notes that were never adjacent can be any distance apart. The line
        // has already been checked for leaps and tritones by this point, so a
        // merge that would open one has to be refused rather than repaired.
        if (! events[index + 2].rest)
        {
            const auto opened = std::abs (events[index].pitch - events[index + 2].pitch);

            if (opened == 6 || opened > maxLeap * 2 + 1)
                return false;
        }

        events[index].ticks += events[index + 1].ticks;
        events.erase (events.begin() + (long) index + 1);

        return true;
    };

    const auto barTicks = signature.barTicks();

    auto positionsOf = [&events] (std::function<bool (int, int)> wanted)
    {
        std::vector<size_t> found;
        auto position = 0;

        for (size_t i = 0; i < events.size(); ++i)
        {
            if (wanted (position, events[i].ticks))
                found.push_back (i);

            position += events[i].ticks;
        }

        return found;
    };

    if (settings.tiesOverBarlines)
    {
        // A note that starts in one bar and stops in the next.
        const auto candidates = positionsOf ([barTicks] (int position, int length)
        {
            const auto intoBar = position % barTicks;

            return intoBar != 0 && intoBar + length == barTicks;
        });

        for (auto pass = 0; pass < 6 && ! candidates.empty(); ++pass)
            if (merge (candidates[(size_t) random.nextInt ((int) candidates.size())]))
                break;
    }

    if (settings.syncopation)
    {
        // A note that starts off the beat and holds across the next one.
        const auto group = juce::jmax (1, signature.beamGroupTicks());

        const auto candidates = positionsOf ([group] (int position, int length)
        {
            const auto intoGroup = position % group;

            return intoGroup != 0 && intoGroup + length == group;
        });

        for (auto pass = 0; pass < 6 && ! candidates.empty(); ++pass)
            if (merge (candidates[(size_t) random.nextInt ((int) candidates.size())]))
                break;
    }

    auto& melody = result.melody;
    melody = model::Melody();
    melody.setTimeSignature (signature);
    melody.setBarCount ((int) written.size());
    melody.setTempoBpm (settings.tempoBpm);
    melody.setTrebleClef (settings.trebleClef);
    melody.setName ("Exercise");

    auto tick = 0;

    for (const auto& event : events)
    {
        melody.placeEvent (tick, event.ticks, event.pitch, event.rest);
        tick += event.ticks;
    }

    // --- what it is, in words ------------------------------------------------
    // What the exercise actually contains, not what was asked for. Several of
    // these only turn up when the rhythm gives them somewhere to go, and a
    // label promising a tie in a line that has none is worse than no label.
    juce::StringArray features;

    if (upbeat)
        features.add ("upbeat");

    {
        const auto group = juce::jmax (1, signature.beamGroupTicks());
        auto position = 0;
        auto rests = false, ties = false, offbeat = false;

        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto intoBar = position % barTicks;
            const auto intoGroup = position % group;

            if (events[i].rest)
            {
                if (! (upbeat && position < barTicks))
                    rests = true;
            }
            else
            {
                if (intoBar + events[i].ticks > barTicks)
                    ties = true;

                if (intoGroup != 0 && intoGroup + events[i].ticks > group)
                    offbeat = true;
            }

            position += events[i].ticks;
        }

        if (rests)   features.add ("rests");
        if (ties)    features.add ("ties");
        if (offbeat) features.add ("syncopation");
    }

    if (accidentals > 0)
        features.add ("accidentals");

    if (minor)
        features.add (settings.minorForm == MinorForm::harmonic ? "harmonic minor"
                    : settings.minorForm == MinorForm::melodic  ? "melodic minor"
                                                                : "natural minor");

    result.description = result.scale.getName()
                           + "  " + juce::String (signature.numerator)
                           + "/" + juce::String (signature.denominator)
                           + "  " + juce::String ((int) written.size()) + " bars";

    if (! features.isEmpty())
        result.description += "  -  " + features.joinIntoString (", ");

    return result;
}

} // namespace practice
