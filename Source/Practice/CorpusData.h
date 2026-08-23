#pragma once

#include <vector>

namespace practice
{
namespace corpus
{

/** One bar's worth of rhythm, as it occurs in real tunes.

    `level` classifies how hard it is to read - by the shortest note in it and
    whether it dots anything - so the rhythm dial can filter the corpus rather
    than choose from a hand-written list. `weight` is how often it turned up,
    so common patterns stay common. */
struct RhythmCell
{
    int numerator;
    int denominator;
    int level;
    int weight;
    std::vector<int> ticks;
};

/** How often one scale degree moved by a given interval. `fromDegree` is
    0 to 6 with 0 the tonic; `interval` is in scale steps. */
struct Transition
{
    int fromDegree;
    int interval;
    int weight;
};

/** The two degrees before a tune's final tonic, relative to it. */
struct Cadence
{
    int thirdLast;
    int secondLast;
    int weight;
};

/** Which degree tunes start on, relative to the tonic. */
struct Opening
{
    int degree;
    int weight;
};

const std::vector<RhythmCell>& getRhythmCells();
const std::vector<Transition>& getTransitions();
const std::vector<Cadence>&    getCadences();
const std::vector<Opening>&    getOpenings();

} // namespace corpus
} // namespace practice
