#pragma once

#include <JuceHeader.h>
#include "../Theory/MusicTheory.h"
#include <vector>

namespace ui
{

/** A piano keyboard that knows about the selected scale and the audio input.

    On top of the normal played/not-played state it shades the keys belonging
    to the current scale, and marks the note the pitch detector is hearing so
    you can compare what you played with what came out.
*/
class ScaleKeyboardComponent : public juce::MidiKeyboardComponent
{
public:
    ScaleKeyboardComponent (juce::MidiKeyboardState& state);

    void setScale (const theory::Scale& newScale);
    void setDetectedNote (int midiNote);              ///< -1 for none
    void setChordNotes (const std::vector<int>& notes);
    void setScaleHighlightEnabled (bool shouldBeEnabled);

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;

    void drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    /** The colour a key should be tinted with, or nothing if it is idle. */
    juce::Colour getHighlightFor (int midiNoteNumber, bool isDown, bool& hasHighlight) const;

    theory::Scale scale { 0, 0 };
    int detectedNote = -1;
    std::vector<int> chordNotes;
    bool scaleHighlightEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaleKeyboardComponent)
};

} // namespace ui
