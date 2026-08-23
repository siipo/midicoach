#pragma once

#include "../Model/Melody.h"
#include <vector>

namespace ui
{
namespace beaming
{

/** One written note, as the beaming rule sees it.

    Deliberately not the engraver's own event type: which notes share a beam is
    a fact about the music, decided before anything knows where it will be
    drawn, and keeping it separate is what lets it be tested without a window.
*/
struct Candidate
{
    int  startTick   = 0;
    int  lengthTicks = 0;
    int  flags       = 0;     ///< 0 for a quarter or longer, 1 eighth, 2 sixteenth
    bool isRest      = false;
    int  systemIndex = 0;     ///< a beam cannot cross a line break
};

/** Which notes are joined under one beam, as runs of indices.

    Eighths and shorter are beamed rather than flagged individually, because a
    beam is what shows the beat: a page of separate flags is harder to read even
    though it says the same thing.

    The grouping unit is the beat the metre is felt in - a quarter in the simple
    metres, a dotted quarter in the compound ones - which is the same unit the
    model already uses to decide where a long note has to be split, so the two
    can never disagree about where a beat begins.

    A beam is broken by a rest, a barline, a line break, and by a note that runs
    past the end of its own beat. A run of one is not returned at all: a single
    note keeps its flag.
*/
std::vector<std::vector<int>> group (const std::vector<Candidate>& notes,
                                     model::TimeSignature signature);

} // namespace beaming
} // namespace ui
