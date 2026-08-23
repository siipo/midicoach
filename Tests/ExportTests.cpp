/*  Tests for the PDF export.

    The interesting question is not whether a file appears - it is whether the
    page has anything on it. Print mode swaps the palette to ink on paper, and
    getting that wrong produces a perfectly valid PDF of a blank sheet, which no
    amount of checking the header would catch. So this counts dark pixels.

    Build target: ExportTests. Returns non-zero if anything fails.
*/

#include <JuceHeader.h>
#include "../Source/Export/ScorePdf.h"

#include <cstring>
#include <iostream>

namespace
{
    int failures = 0;
    int checks   = 0;

    void check (bool condition, const juce::String& what)
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::cout << "  FAIL: " << what << std::endl;
        }
    }

    model::Melody makeTune()
    {
        model::Melody melody;
        melody.setName ("Export test");
        melody.setTimeSignature ({ 4, 4 });
        melody.setBarCount (2);

        const auto quarter = model::ticksPerQuarter;
        melody.placeEvent (0,           quarter, 60, false);
        melody.placeEvent (quarter,     quarter, 62, false);
        melody.placeEvent (quarter * 2, quarter, 64, false);
        melody.placeEvent (quarter * 3, quarter, 65, false);
        melody.placeEvent (quarter * 4, quarter * 2, 67, false);
        melody.placeEvent (quarter * 6, quarter * 2, 60, false);

        return melody;
    }
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "ExportTests" << std::endl;

    const auto melody = makeTune();
    const theory::Scale scale (2, 0);   // D major, so there is a key signature to draw

    // -- the page actually has music on it ------------------------------------
    {
        const auto page = exporter::ScorePdf::renderPage (melody, scale, "Export test");

        check (page.isValid(), "a page is rendered");
        check (page.getWidth() > 1000 && page.getHeight() > 700, "at a printable size");

        auto darkPixels = 0;
        auto whitePixels = 0;

        for (int y = 0; y < page.getHeight(); y += 3)
        {
            for (int x = 0; x < page.getWidth(); x += 3)
            {
                const auto colour = page.getPixelAt (x, y);

                if (colour.getBrightness() < 0.4f)
                    ++darkPixels;
                else if (colour.getBrightness() > 0.9f)
                    ++whitePixels;
            }
        }

        // Staff lines, clef, key signature, notes and a title: not a blank sheet,
        // but nowhere near a black one either.
        check (darkPixels > 500, "the page has ink on it (" + juce::String (darkPixels) + " dark samples)");
        check (whitePixels > darkPixels * 5, "on a mostly white background");
    }

    // -- the PDF itself is well formed ----------------------------------------
    {
        const auto file = juce::File::createTempFile (".pdf");

        check (exporter::ScorePdf::write (melody, scale, "Export test", file),
               "the PDF is written");

        juce::MemoryBlock data;
        file.loadFileAsData (data);

        check (data.getSize() > 4000, "and is a plausible size ("
                                        + juce::String ((int) data.getSize()) + " bytes)");

        // A PDF is not text: the JPEG stream in the middle contains nulls, so
        // anything that reads it as a string stops partway. Search the bytes.
        const auto* bytes = (const char*) data.getData();
        const auto size = (int) data.getSize();

        auto containsBytes = [bytes, size] (const char* needle)
        {
            const auto needleLength = (int) std::strlen (needle);

            for (int i = 0; i + needleLength <= size; ++i)
                if (std::memcmp (bytes + i, needle, (size_t) needleLength) == 0)
                    return true;

            return false;
        };

        check (std::memcmp (bytes, "%PDF-1.", 7) == 0, "starts with a PDF header");
        check (containsBytes ("/Type /Catalog"), "has a document catalogue");
        check (containsBytes ("/Subtype /Image"), "embeds the page image");
        check (containsBytes ("startxref"), "has a cross-reference pointer");
        check (containsBytes ("/Root 1 0 R"), "the trailer points at the catalogue");
        check (containsBytes ("%%EOF"), "ends with the EOF marker");

    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
