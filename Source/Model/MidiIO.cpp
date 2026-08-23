#include "MidiIO.h"
#include <algorithm>

namespace model
{

namespace
{
    /** Everything is written and read at the model's own resolution, which
        keeps the conversion exact in the export direction. */
    constexpr int fileTicksPerQuarter = ticksPerQuarter;

    constexpr int gridTicks = 240;   // a sixteenth: the finest value we can write

    int snapToGrid (double ticks)
    {
        return (int) std::llround (ticks / gridTicks) * gridTicks;
    }

    /** The longest writable value that fits, so an imported length always lands
        on something the engraver can actually draw. */
    int nearestWritableLength (int ticks)
    {
        if (ticks <= 0)
            return gridTicks;

        const NoteValueInfo* best = nullptr;
        auto bestDistance = std::numeric_limits<int>::max();

        for (const auto& value : getNoteValues())
        {
            const auto distance = std::abs (value.ticks - ticks);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &value;
            }
        }

        return best != nullptr ? best->ticks : gridTicks;
    }

    struct ImportedNote
    {
        int startTick = 0;
        int lengthTicks = 0;
        int midiNote = 60;
    };
}

//==============================================================================
bool MidiIO::exportToFile (const Melody& melody, const juce::File& destination)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (fileTicksPerQuarter);

    const auto signature = melody.getTimeSignature();

    juce::MidiMessageSequence track;

    auto tempo = juce::MidiMessage::tempoMetaEvent (
        (int) std::llround (60000000.0 / juce::jmax (1.0, melody.getTempoBpm())));
    tempo.setTimeStamp (0.0);
    track.addEvent (tempo);

    auto metre = juce::MidiMessage::timeSignatureMetaEvent (signature.numerator,
                                                            signature.denominator);
    metre.setTimeStamp (0.0);
    track.addEvent (metre);

    auto name = juce::MidiMessage::textMetaEvent (3, melody.getName());
    name.setTimeStamp (0.0);
    track.addEvent (name);

    for (const auto& note : melody.getPlaybackNotes())
    {
        const auto pitch = juce::jlimit (0, 127, note.midiNote);

        auto on = juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100);
        on.setTimeStamp ((double) note.startTick);
        track.addEvent (on);

        auto off = juce::MidiMessage::noteOff (1, pitch);
        off.setTimeStamp ((double) (note.startTick + note.lengthTicks));
        track.addEvent (off);
    }

    track.updateMatchedPairs();

    auto endOfTrack = juce::MidiMessage::endOfTrack();
    endOfTrack.setTimeStamp ((double) melody.getTotalTicks());
    track.addEvent (endOfTrack);

    file.addTrack (track);

    juce::TemporaryFile temporary (destination);

    {
        juce::FileOutputStream stream (temporary.getFile());

        if (! stream.openedOk() || ! file.writeTo (stream))
            return false;
    }

    return temporary.overwriteTargetFileWithTemporary();
}

//==============================================================================
bool MidiIO::importFromFile (const juce::File& source, Melody& result, juce::String& report)
{
    juce::FileInputStream stream (source);

    if (! stream.openedOk())
    {
        report = "Could not open " + source.getFileName();
        return false;
    }

    return importFromStream (stream, result, report);
}

bool MidiIO::importFromStream (juce::InputStream& source, Melody& result, juce::String& report)
{
    juce::StringArray notes;

    juce::MidiFile file;

    if (! file.readFrom (source))
    {
        report = "That does not look like a MIDI file.";
        return false;
    }

    const auto timeFormat = file.getTimeFormat();

    if (timeFormat <= 0)
    {
        // SMPTE timing has no notion of a beat, so there is nothing sensible to
        // put on a staff.
        report = "The file uses SMPTE timing, which has no musical beat to read.";
        return false;
    }

    const auto tickScale = (double) ticksPerQuarter / (double) timeFormat;

    // --- pick the track with the most notes: on a typical file that is the
    //     melody, and on a single-track file it is the only choice ------------
    const juce::MidiMessageSequence* bestTrack = nullptr;
    auto bestCount = 0;

    for (int i = 0; i < file.getNumTracks(); ++i)
    {
        const auto* track = file.getTrack (i);
        auto count = 0;

        for (int e = 0; e < track->getNumEvents(); ++e)
            if (track->getEventPointer (e)->message.isNoteOn())
                ++count;

        if (count > bestCount)
        {
            bestCount = count;
            bestTrack = track;
        }
    }

    if (bestTrack == nullptr || bestCount == 0)
    {
        report = "The file contains no notes.";
        return false;
    }

    if (file.getNumTracks() > 1)
        notes.add ("used the busiest of " + juce::String (file.getNumTracks()) + " tracks");

    // --- tempo and time signature, from wherever they appear ------------------
    auto tempoBpm = 90.0;
    auto numerator = 4, denominator = 4;

    for (int i = 0; i < file.getNumTracks(); ++i)
    {
        const auto* track = file.getTrack (i);

        for (int e = 0; e < track->getNumEvents(); ++e)
        {
            const auto& message = track->getEventPointer (e)->message;

            if (message.isTempoMetaEvent())
            {
                const auto secondsPerQuarter = message.getTempoSecondsPerQuarterNote();

                if (secondsPerQuarter > 0.0)
                    tempoBpm = juce::jlimit (20.0, 300.0, 60.0 / secondsPerQuarter);
            }
            else if (message.isTimeSignatureMetaEvent())
            {
                message.getTimeSignatureInfo (numerator, denominator);
            }
        }
    }

    // --- collect the notes ---------------------------------------------------
    juce::MidiMessageSequence sequence (*bestTrack);
    sequence.updateMatchedPairs();

    std::vector<ImportedNote> imported;

    for (int e = 0; e < sequence.getNumEvents(); ++e)
    {
        const auto* event = sequence.getEventPointer (e);

        if (! event->message.isNoteOn())
            continue;

        const auto start = event->message.getTimeStamp() * tickScale;
        const auto end = event->noteOffObject != nullptr
                           ? event->noteOffObject->message.getTimeStamp() * tickScale
                           : start + ticksPerQuarter;

        ImportedNote note;
        note.startTick   = juce::jmax (0, snapToGrid (start));
        note.lengthTicks = juce::jmax (gridTicks, snapToGrid (end - start));
        note.midiNote    = event->message.getNoteNumber();
        imported.push_back (note);
    }

    if (imported.empty())
    {
        report = "The file contains no playable notes.";
        return false;
    }

    std::stable_sort (imported.begin(), imported.end(),
                      [] (const ImportedNote& a, const ImportedNote& b)
                      { return a.startTick < b.startTick; });

    // --- flatten to one line -------------------------------------------------
    // Where notes sound together only the highest is kept, which is the usual
    // convention for reducing a texture to a melody. Where they merely overlap,
    // the earlier one is cut short.
    std::vector<ImportedNote> line;
    auto droppedChordNotes = 0;

    for (const auto& note : imported)
    {
        if (! line.empty() && line.back().startTick == note.startTick)
        {
            ++droppedChordNotes;

            if (note.midiNote > line.back().midiNote)
                line.back() = note;

            continue;
        }

        if (! line.empty() && line.back().startTick + line.back().lengthTicks > note.startTick)
            line.back().lengthTicks = note.startTick - line.back().startTick;

        if (! line.empty() && line.back().lengthTicks <= 0)
            line.pop_back();

        line.push_back (note);
    }

    if (droppedChordNotes > 0)
        notes.add ("kept the top note of " + juce::String (droppedChordNotes) + " chords");

    // --- shift a leading gap away, then fit to writable values ---------------
    const auto firstStart = line.front().startTick;

    if (firstStart > 0)
    {
        for (auto& note : line)
            note.startTick -= firstStart;

        notes.add ("trimmed " + juce::String (firstStart / ticksPerQuarter) + " beats of silence");
    }

    auto lengthsChanged = 0;

    for (auto& note : line)
    {
        const auto writable = nearestWritableLength (note.lengthTicks);

        if (writable != note.lengthTicks)
            ++lengthsChanged;

        note.lengthTicks = writable;
    }

    if (lengthsChanged > 0)
        notes.add ("rounded " + juce::String (lengthsChanged) + " note lengths to fit the staff");

    // --- build the melody ----------------------------------------------------
    result = Melody();
    result.setTimeSignature ({ juce::jmax (1, numerator), juce::jmax (1, denominator) });
    result.setTempoBpm (tempoBpm);
    result.setTrebleClef (true);

    const auto barTicks = result.getTimeSignature().barTicks();
    const auto lastEnd = line.back().startTick + line.back().lengthTicks;
    result.setBarCount (juce::jlimit (1, 128, (lastEnd + barTicks - 1) / barTicks));

    int lowest = 127, highest = 0;

    for (const auto& note : line)
    {
        result.placeEvent (note.startTick, note.lengthTicks, note.midiNote, false);
        lowest  = juce::jmin (lowest, note.midiNote);
        highest = juce::jmax (highest, note.midiNote);
    }

    // Bass clef if the line really sits down there, so it reads without a
    // thicket of ledger lines.
    if (highest < 60)
    {
        result.setTrebleClef (false);
        notes.add ("used bass clef for the range");
    }

    if (result.getBarCount() > 32)
        notes.add ("this is a long file - only the first bars will be comfortable to read");

    report = notes.isEmpty() ? "Imported cleanly." : "Imported: " + notes.joinIntoString ("; ") + ".";

    return true;
}

} // namespace model
