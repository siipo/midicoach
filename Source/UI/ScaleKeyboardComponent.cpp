#include "ScaleKeyboardComponent.h"
#include "Palette.h"
#include <algorithm>

namespace ui
{

ScaleKeyboardComponent::ScaleKeyboardComponent (juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent (state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
}

void ScaleKeyboardComponent::setScale (const theory::Scale& newScale)
{
    scale = newScale;
    repaint();
}

void ScaleKeyboardComponent::setDetectedNote (int midiNote)
{
    if (detectedNote != midiNote)
    {
        detectedNote = midiNote;
        repaint();
    }
}

void ScaleKeyboardComponent::setChordNotes (const std::vector<int>& notes)
{
    if (chordNotes != notes)
    {
        chordNotes = notes;
        repaint();
    }
}

void ScaleKeyboardComponent::setScaleHighlightEnabled (bool shouldBeEnabled)
{
    if (scaleHighlightEnabled != shouldBeEnabled)
    {
        scaleHighlightEnabled = shouldBeEnabled;
        repaint();
    }
}

//==============================================================================
juce::Colour ScaleKeyboardComponent::getHighlightFor (int midiNoteNumber, bool isDown,
                                                      bool& hasHighlight) const
{
    hasHighlight = true;

    // A key you are pressing wins over one the detector merely heard, and the
    // confident monophonic reading wins over the chord layer's guesses.
    if (isDown)
        return palette::midiNote;

    if (midiNoteNumber == detectedNote)
        return palette::detectedNote;

    if (std::find (chordNotes.begin(), chordNotes.end(), midiNoteNumber) != chordNotes.end())
        return palette::chordNote;

    hasHighlight = false;
    return {};
}

void ScaleKeyboardComponent::drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                                            juce::Rectangle<float> area, bool isDown, bool isOver,
                                            juce::Colour lineColour, juce::Colour textColour)
{
    auto fill = juce::Colours::white;

    if (scaleHighlightEnabled && scale.containsNote (midiNoteNumber))
        fill = palette::inScaleWhiteKey;

    bool highlighted = false;
    const auto highlight = getHighlightFor (midiNoteNumber, isDown, highlighted);

    if (highlighted)
        fill = highlight;
    else if (isOver)
        fill = fill.darker (0.08f);

    g.setColour (fill);
    g.fillRect (area);

    g.setColour (lineColour);
    g.fillRect (area.withWidth (1.0f));

    if (midiNoteNumber % 12 == 0)   // label every C
    {
        g.setColour (highlighted ? juce::Colours::white.withAlpha (0.9f) : textColour);
        g.setFont (juce::Font (juce::jmin (12.0f, area.getWidth() * 0.7f)));
        g.drawText ("C" + juce::String (midiNoteNumber / 12 - 1),
                    area.reduced (1.0f, 2.0f), juce::Justification::centredBottom, false);
    }
}

void ScaleKeyboardComponent::drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                                            juce::Rectangle<float> area, bool isDown, bool isOver,
                                            juce::Colour noteFillColour)
{
    auto fill = noteFillColour;

    if (scaleHighlightEnabled && scale.containsNote (midiNoteNumber))
        fill = palette::inScaleBlackKey;

    bool highlighted = false;
    const auto highlight = getHighlightFor (midiNoteNumber, isDown, highlighted);

    if (highlighted)
        fill = highlight.darker (0.2f);
    else if (isOver)
        fill = fill.brighter (0.15f);

    g.setColour (fill);
    g.fillRect (area);

    if (! highlighted)
    {
        g.setColour (fill.brighter (0.4f));
        g.fillRect (area.withHeight (area.getHeight() * 0.12f));
    }
}

} // namespace ui
