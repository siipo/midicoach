#include "TuneLibrary.h"

namespace model
{

juce::File TuneLibrary::getDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("MidiCoach")
                       .getChildFile ("Tunes");

    directory.createDirectory();

    return directory;
}

juce::File TuneLibrary::fileForName (const juce::String& name)
{
    // The tune's name is what the user typed, so it has to survive becoming a
    // filename without dragging anything odd into the path.
    const auto safe = juce::File::createLegalFileName (name.trim());

    return getDirectory().getChildFile (safe + ".json");
}

juce::StringArray TuneLibrary::listTuneNames()
{
    juce::StringArray names;

    for (const auto& file : getDirectory().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        bool ok = false;
        const auto melody = Melody::fromJsonString (file.loadFileAsString(), ok);

        // Prefer the name stored inside the file, so renaming on disk doesn't
        // make a tune show up under a name it doesn't answer to.
        names.add (ok && melody.getName().isNotEmpty() ? melody.getName()
                                                       : file.getFileNameWithoutExtension());
    }

    names.sortNatural();

    return names;
}

bool TuneLibrary::save (const Melody& melody)
{
    if (melody.getName().trim().isEmpty())
        return false;

    return fileForName (melody.getName()).replaceWithText (melody.toJsonString());
}

bool TuneLibrary::load (const juce::String& name, Melody& result)
{
    const auto file = fileForName (name);

    if (! file.existsAsFile())
        return false;

    bool ok = false;
    auto loaded = Melody::fromJsonString (file.loadFileAsString(), ok);

    if (! ok)
        return false;

    result = std::move (loaded);

    return true;
}

bool TuneLibrary::remove (const juce::String& name)
{
    const auto file = fileForName (name);

    return file.existsAsFile() && file.deleteFile();
}

bool TuneLibrary::exists (const juce::String& name)
{
    return fileForName (name).existsAsFile();
}

} // namespace model
