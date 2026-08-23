#pragma once

#include <JuceHeader.h>

namespace ui
{

/** A palette button showing the note value it writes.

    Drawing the actual notehead, stem and flag is worth the few lines: a row of
    real note symbols is readable at a glance in a way that "1/8" is not, and
    the font is already embedded.
*/
class NoteValueButton : public juce::Button
{
public:
    /** `baseTicks` is the undotted length, which is what picks the glyph. */
    NoteValueButton (int baseTicks, const juce::String& tooltip);

    int getBaseTicks() const noexcept { return baseTicks; }

    void paintButton (juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    int baseTicks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteValueButton)
};

} // namespace ui
