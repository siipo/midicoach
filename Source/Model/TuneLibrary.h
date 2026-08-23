#pragma once

#include <JuceHeader.h>
#include "Melody.h"

namespace model
{

/** Tunes on disk, one JSON file each, in %APPDATA%\\MidiCoach\\Tunes.

    Plain readable files rather than one library blob, so a tune can be copied,
    backed up or hand-edited without the app's help.
*/
class TuneLibrary
{
public:
    static juce::File getDirectory();

    /** Names of every saved tune, sorted. */
    static juce::StringArray listTuneNames();

    /** Saves under the melody's own name. Returns false if the name is empty
        or the file could not be written. */
    static bool save (const Melody& melody);

    static bool load (const juce::String& name, Melody& result);
    static bool remove (const juce::String& name);

    static bool exists (const juce::String& name);

private:
    static juce::File fileForName (const juce::String& name);
};

} // namespace model
