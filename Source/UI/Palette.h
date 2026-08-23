#pragma once

#include <JuceHeader.h>

namespace ui
{

/** One colour vocabulary shared by the keyboard, the staff and the readouts,
    so a note means the same thing wherever you happen to be looking. */
namespace palette
{
    const juce::Colour background      { 0xff1c1f26 };
    const juce::Colour panel           { 0xff252932 };
    const juce::Colour ink             { 0xffe8eaf0 };
    const juce::Colour inkDim          { 0xff9aa1b1 };
    const juce::Colour staffLine       { 0xff8b93a5 };

    /** Notes arriving from the MIDI keyboard or clicked on screen. */
    const juce::Colour midiNote        { 0xff4c8dff };

    /** The pitch detected in the audio input. */
    const juce::Colour detectedNote    { 0xffff9d3d };

    /** Extra notes from the chord layer, which is less certain than the
        monophonic tracker - drawn weaker on purpose. */
    const juce::Colour chordNote       { 0xffb0742b };

    /** Keys belonging to the selected scale. */
    const juce::Colour inScaleWhiteKey { 0xffd8ecd9 };
    const juce::Colour inScaleBlackKey { 0xff2f4a34 };

    const juce::Colour inTune          { 0xff4ad98a };
    const juce::Colour outOfTune       { 0xffe0574c };
}

} // namespace ui
