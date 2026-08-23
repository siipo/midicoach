#pragma once

#include <JuceHeader.h>
#include "../Model/Melody.h"
#include "../Theory/MusicTheory.h"

namespace exporter
{

/** Writes a tune out as a printable page.

    JUCE has no PDF writer, so this builds one directly. The page is a single
    high-resolution rendering of the score rather than vector drawing: writing
    the glyphs as vectors would mean embedding Bravura as a CID font, which is a
    great deal of machinery for a practice sheet. At 200 dpi on A4 the result
    prints indistinguishably, and the PDF itself is a handful of objects.
*/
class ScorePdf
{
public:
    static bool write (const model::Melody& melody,
                       const theory::Scale& scale,
                       const juce::String& title,
                       const juce::File& destination);

    /** The same page as an image, which is also what the PDF embeds. */
    static juce::Image renderPage (const model::Melody& melody,
                                   const theory::Scale& scale,
                                   const juce::String& title);
};

} // namespace exporter
