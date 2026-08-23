#include "Beaming.h"
#include <algorithm>

namespace ui
{
namespace beaming
{

std::vector<std::vector<int>> group (const std::vector<Candidate>& notes,
                                     model::TimeSignature signature)
{
    std::vector<std::vector<int>> groups;

    const auto beatTicks = std::max (1, signature.beamGroupTicks());

    auto beamable = [&notes, beatTicks] (size_t index, int beat, int systemIndex)
    {
        const auto& note = notes[index];

        return ! note.isRest
                 && note.flags > 0
                 && note.systemIndex == systemIndex
                 && note.startTick / beatTicks == beat
                 && note.startTick + note.lengthTicks <= (beat + 1) * beatTicks;
    };

    size_t i = 0;

    while (i < notes.size())
    {
        if (notes[i].isRest || notes[i].flags == 0)
        {
            ++i;
            continue;
        }

        const auto beat        = notes[i].startTick / beatTicks;
        const auto systemIndex = notes[i].systemIndex;

        std::vector<int> run;

        for (auto j = i; j < notes.size() && beamable (j, beat, systemIndex); ++j)
            run.push_back ((int) j);

        // A group of one would have to be drawn as a flag anyway, so it is not
        // a group. Advancing by one rather than past the run matters here: the
        // note after an unbeamable one may still start a group of its own.
        if (run.size() < 2)
        {
            ++i;
            continue;
        }

        i = (size_t) run.back() + 1;
        groups.push_back (std::move (run));
    }

    return groups;
}

} // namespace beaming
} // namespace ui
