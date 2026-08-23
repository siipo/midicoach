#include "SettingsPanel.h"

namespace ui
{

SettingsPanel::SettingsPanel (juce::String panelTitle)
    : title (std::move (panelTitle))
{
    // It floats over the rest of the window, so it has to paint its own
    // background or the controls behind it show through.
    setOpaque (false);
    setAlwaysOnTop (true);
}

void SettingsPanel::addRow (juce::String caption, juce::Component& control)
{
    addAndMakeVisible (control);
    rows.push_back ({ std::move (caption), &control, nullptr, false });
}

void SettingsPanel::addRow (juce::String caption, juce::Component& left,
                            juce::Component& right)
{
    addAndMakeVisible (left);
    addAndMakeVisible (right);
    rows.push_back ({ std::move (caption), &left, &right, false });
}

void SettingsPanel::addToggles (juce::Component& left, juce::Component& right)
{
    addAndMakeVisible (left);
    addAndMakeVisible (right);
    rows.push_back ({ {}, &left, &right, true });
}

void SettingsPanel::addGap()
{
    rows.push_back ({ {}, nullptr, nullptr, false });
}

int SettingsPanel::getPreferredHeight() const
{
    auto height = titleHeight + margin * 2;

    for (const auto& row : rows)
        height += row.left == nullptr ? gapHeight : rowHeight;

    return height;
}

void SettingsPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (palette::panel.brighter (0.12f));
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setColour (palette::inkDim.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    g.setColour (palette::inkDim);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText (title, margin, margin, getWidth() - margin * 2, titleHeight - 6,
                juce::Justification::centredLeft, false);

    g.setFont (juce::Font (12.0f));

    auto y = margin + titleHeight;

    for (const auto& row : rows)
    {
        if (row.left == nullptr)
        {
            y += gapHeight;
            continue;
        }

        if (row.caption.isNotEmpty())
            g.drawText (row.caption, margin, y, captionWidth - 6, rowHeight,
                        juce::Justification::centredLeft, false);

        y += rowHeight;
    }
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (margin);
    area.removeFromTop (titleHeight);

    for (const auto& row : rows)
    {
        if (row.left == nullptr)
        {
            area.removeFromTop (gapHeight);
            continue;
        }

        auto line = area.removeFromTop (rowHeight).reduced (0, 2);

        if (row.split)
        {
            const auto half = line.getWidth() / 2;
            row.left->setBounds (line.removeFromLeft (half));
            row.right->setBounds (line);
            continue;
        }

        if (row.caption.isNotEmpty())
            line.removeFromLeft (captionWidth);

        if (row.right != nullptr)
        {
            const auto half = (line.getWidth() - 6) / 2;
            row.left->setBounds (line.removeFromLeft (half));
            line.removeFromLeft (6);
            row.right->setBounds (line);
        }
        else
        {
            row.left->setBounds (line);
        }
    }
}

} // namespace ui
