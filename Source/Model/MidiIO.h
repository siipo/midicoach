#pragma once

#include <JuceHeader.h>
#include "Melody.h"

namespace model
{

/** Standard MIDI file in and out.

    Export is straightforward: the model already counts in ticks per quarter,
    so it maps onto a MIDI file almost directly.

    Import is the awkward direction. A MIDI file can hold anything, while a
    tune here is a single monophonic line, so importing has to make choices:
    which track, what to do about overlapping notes, and how to fit arbitrary
    timing onto a grid of writable note values. Those choices are made
    explicitly below and reported back to the caller.
*/
class MidiIO
{
public:
    static bool exportToFile (const Melody& melody, const juce::File& destination);

    /** Reads the most melodic-looking track and flattens it to one line.
        `report` describes anything that had to be changed to fit. */
    static bool importFromFile (const juce::File& source, Melody& result, juce::String& report);

    /** Same, from data already in memory - this is what the tests drive. */
    static bool importFromStream (juce::InputStream& source, Melody& result, juce::String& report);
};

} // namespace model
