#include "ScorePdf.h"
#include "../UI/MelodyStaffComponent.h"

namespace exporter
{

namespace
{
    // A4 landscape in PostScript points, which is what PDF measures in.
    constexpr int pageWidthPoints  = 842;
    constexpr int pageHeightPoints = 595;

    /** Rendered at roughly 200 dpi, then scaled down onto the page. */
    constexpr int renderScale = 3;

    constexpr int marginPoints = 48;
    constexpr int titleHeightPoints = 46;
}

//==============================================================================
juce::Image ScorePdf::renderPage (const model::Melody& melody,
                                  const theory::Scale& scale,
                                  const juce::String& title)
{
    const auto pageWidth  = pageWidthPoints * renderScale;
    const auto pageHeight = pageHeightPoints * renderScale;
    const auto margin     = marginPoints * renderScale;
    const auto titleHeight = titleHeightPoints * renderScale;

    juce::Image page (juce::Image::RGB, pageWidth, pageHeight, true);

    {
        juce::Graphics g (page);
        g.fillAll (juce::Colours::white);

        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (22.0f * renderScale, juce::Font::bold));
        g.drawText (title, margin, margin / 2, pageWidth - margin * 2, titleHeight,
                    juce::Justification::topLeft, false);
    }

    // The staff draws itself into the page through the normal renderer, just
    // sized for paper and told to use ink rather than the screen palette.
    ui::MelodyStaffComponent staff;
    staff.setPrintMode (true);
    staff.setScale (scale);
    staff.setMelody (melody);
    staff.setSize (pageWidth - margin * 2, pageHeight - margin * 2 - titleHeight);

    const auto staffImage = staff.createComponentSnapshot (staff.getLocalBounds(), false, 1.0f);

    {
        juce::Graphics g (page);
        g.drawImageAt (staffImage, margin, margin + titleHeight);
    }

    return page;
}

//==============================================================================
bool ScorePdf::write (const model::Melody& melody,
                      const theory::Scale& scale,
                      const juce::String& title,
                      const juce::File& destination)
{
    const auto page = renderPage (melody, scale, title);

    if (! page.isValid())
        return false;

    juce::MemoryBlock jpegData;

    {
        juce::MemoryOutputStream jpegStream (jpegData, false);
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.92f);

        if (! jpeg.writeImageToStream (page, jpegStream))
            return false;
    }

    juce::MemoryOutputStream pdf;
    std::array<juce::int64, 6> offsets {};

    auto beginObject = [&pdf, &offsets] (int number)
    {
        offsets[(size_t) number] = pdf.getPosition();
        pdf << juce::String (number) << " 0 obj\n";
    };

    pdf << "%PDF-1.4\n";

    beginObject (1);
    pdf << "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    beginObject (2);
    pdf << "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";

    beginObject (3);
    pdf << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
        << juce::String (pageWidthPoints) << " " << juce::String (pageHeightPoints) << "] "
        << "/Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>\nendobj\n";

    beginObject (4);
    pdf << "<< /Type /XObject /Subtype /Image /Width " << juce::String (page.getWidth())
        << " /Height " << juce::String (page.getHeight())
        << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length "
        << juce::String ((int) jpegData.getSize()) << " >>\nstream\n";
    pdf.write (jpegData.getData(), jpegData.getSize());
    pdf << "\nendstream\nendobj\n";

    // The image is placed to fill the whole page: it was rendered with the
    // margins already in it.
    juce::String content;
    content << "q\n"
            << juce::String (pageWidthPoints) << " 0 0 " << juce::String (pageHeightPoints)
            << " 0 0 cm\n/Im0 Do\nQ\n";

    beginObject (5);
    pdf << "<< /Length " << juce::String (content.getNumBytesAsUTF8()) << " >>\nstream\n"
        << content << "endstream\nendobj\n";

    const auto xrefPosition = pdf.getPosition();

    pdf << "xref\n0 6\n0000000000 65535 f \n";

    for (int i = 1; i <= 5; ++i)
        pdf << juce::String (offsets[(size_t) i]).paddedLeft ('0', 10) << " 00000 n \n";

    pdf << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"
        << juce::String (xrefPosition) << "\n%%EOF\n";

    juce::TemporaryFile temporary (destination);

    {
        juce::FileOutputStream stream (temporary.getFile());

        if (! stream.openedOk())
            return false;

        stream.write (pdf.getData(), pdf.getDataSize());
    }

    return temporary.overwriteTargetFileWithTemporary();
}

} // namespace exporter
