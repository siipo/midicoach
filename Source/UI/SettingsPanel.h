#pragma once

#include <JuceHeader.h>
#include "Palette.h"
#include <vector>

namespace ui
{

/** A group of controls that lives behind a button until it is wanted.

    Most settings are set once and then left alone - which instrument, what
    counts as the right note, how wide the exercise range is - and a row of them
    permanently on screen is a row of noise between the reader and the staff.
    Putting them here costs one click and buys back a line of the window.

    The panel owns its layout: rows are added in order and it works out its own
    height, so a caller never has to keep a list of coordinates in step with a
    list of controls.
*/
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (juce::String panelTitle);

    /** One line: a caption on the left, a control on the right.

        An empty caption gives the control the full width, which is what a
        toggle wants - it carries its own text already.
    */
    void addRow (juce::String caption, juce::Component& control);

    /** Two controls sharing a line, for things that are really one setting -
        the two ends of a range, say. */
    void addRow (juce::String caption, juce::Component& left, juce::Component& right);

    /** A pair of toggles side by side, which is how they read best. */
    void addToggles (juce::Component& left, juce::Component& right);

    /** A blank line, to separate one idea from the next. */
    void addGap();

    int getPreferredWidth() const noexcept  { return 320; }
    int getPreferredHeight() const;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Row
    {
        juce::String caption;
        juce::Component* left  = nullptr;
        juce::Component* right = nullptr;
        bool split = false;      ///< two equal halves rather than caption + control
    };

    static constexpr int rowHeight   = 28;
    static constexpr int gapHeight   = 10;
    static constexpr int titleHeight = 26;
    static constexpr int margin      = 12;
    static constexpr int captionWidth = 104;

    juce::String title;
    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

} // namespace ui
