#include "NoteValueButton.h"
#include "MusicFont.h"
#include "Palette.h"
#include "../Model/Melody.h"

namespace ui
{

NoteValueButton::NoteValueButton (int baseTicksToDraw, const juce::String& tooltip)
    : juce::Button (tooltip), baseTicks (baseTicksToDraw)
{
    setTooltip (tooltip);
    setClickingTogglesState (false);
}

void NoteValueButton::paintButton (juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto selected = getToggleState();

    auto background = selected ? palette::midiNote.withAlpha (0.35f)
                               : palette::panel.brighter (0.08f);

    if (shouldDrawDown)
        background = background.brighter (0.2f);
    else if (shouldDrawHighlighted)
        background = background.brighter (0.1f);

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (selected ? palette::midiNote : palette::staffLine.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Size the glyph from the button, then draw a stem and flag by hand so the
    // symbol reads the same way it does on the staff.
    const auto staffSpace = bounds.getHeight() / 5.5f;
    const auto font = musicFont::fontForStaffSpace (staffSpace);

    const auto glyph = baseTicks >= 3840 ? musicFont::noteheadWhole
                     : baseTicks >= 1920 ? musicFont::noteheadHalf
                                         : musicFont::noteheadBlack;

    const auto noteheadWidth = musicFont::getGlyphWidth (font, glyph);
    const auto flagCount = model::flagCountForBaseTicks (baseTicks);

    const auto centreX = bounds.getCentreX() - noteheadWidth * 0.5f;
    const auto baseline = bounds.getCentreY() + staffSpace * 1.5f;

    g.setColour (selected ? palette::ink : palette::inkDim);

    if (baseTicks < 3840)
    {
        const auto thickness = juce::jmax (1.0f, staffSpace * 0.14f);
        const auto length = staffSpace * 3.2f;

        g.fillRect (juce::Rectangle<float> (centreX + noteheadWidth - thickness,
                                            baseline - length, thickness, length));

        if (flagCount > 0)
            musicFont::drawGlyph (g, font,
                                  flagCount >= 2 ? musicFont::flag16thUp : musicFont::flag8thUp,
                                  centreX + noteheadWidth, baseline - length);
    }

    musicFont::drawGlyph (g, font, glyph, centreX, baseline);
}

} // namespace ui
