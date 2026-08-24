/*  Structural tests for the melody model.

    The model's job is to keep one invariant true no matter what you write into
    it: every bar is full, nothing crosses a bar line, and every event is a
    legal note value. That is fiddly enough to be worth checking directly
    rather than by squinting at rendered output.

    Build target: MelodyTests. Returns non-zero if anything fails.
*/

#include <JuceHeader.h>
#include "../Source/Model/Melody.h"
#include "../Source/UI/Beaming.h"
#include "../Source/Model/MidiIO.h"

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

    void checkEqual (int actual, int expected, const juce::String& what)
    {
        check (actual == expected,
               what + " (expected " + juce::String (expected)
                    + ", got " + juce::String (actual) + ")");
    }

    juce::String describe (const model::Melody& melody)
    {
        juce::String text;

        for (const auto& event : melody.getEvents())
            text << (event.isRest ? "r" : juce::String (event.midiNote))
                 << ":" << event.startTick << "+" << event.lengthTicks
                 << (event.tiedToNext ? "~" : "") << " ";

        return text.trim();
    }

    /** The invariant, checked from scratch every time. */
    void checkInvariant (const model::Melody& melody, const juce::String& context)
    {
        const auto& events = melody.getEvents();
        const auto barLength = melody.getTimeSignature().barTicks();

        int cursor = 0;

        for (const auto& event : events)
        {
            check (event.startTick == cursor,
                   context + ": events must tile with no gap at tick " + juce::String (cursor));

            check (event.lengthTicks > 0, context + ": zero-length event");

            check (event.startTick / barLength == (event.endTick() - 1) / barLength,
                   context + ": event crosses a bar line at " + juce::String (event.startTick));

            check (model::findNoteValue (event.lengthTicks) != nullptr,
                   context + ": length " + juce::String (event.lengthTicks) + " is not writable");

            if (event.isRest)
                check (! event.tiedToNext, context + ": a rest must never tie");

            cursor = event.endTick();
        }

        checkEqual (cursor, melody.getTotalTicks(), context + ": events must fill the melody");
    }
}

//==============================================================================
int main()
{
    std::cout << "MelodyTests" << std::endl;

    // -- an empty tune is full of rests ---------------------------------------
    {
        model::Melody melody;
        melody.setBarCount (4);

        checkInvariant (melody, "empty");
        checkEqual ((int) melody.getEvents().size(), 4, "empty 4/4 gives one whole rest per bar");
        checkEqual (melody.getTotalTicks(), 4 * 3840, "four bars of 4/4");
        check (melody.getRehearsalNotes().empty(), "an empty tune has nothing to rehearse");
    }

    // -- a single note, with the rest of the bar filled ------------------------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (0, 960, 60, false);

        checkInvariant (melody, "single note");
        check (! melody.getEvents().empty(), "note was written");
        checkEqual (melody.getEvents()[0].midiNote, 60, "pitch survives");
        checkEqual (melody.getEvents()[0].lengthTicks, 960, "quarter note");
        check (! melody.getEvents()[0].isRest, "first event is the note");
        std::cout << "  single note: " << describe (melody) << std::endl;
    }

    // -- a note running over a bar line is split and tied ----------------------
    {
        model::Melody melody;
        melody.setBarCount (2);
        melody.placeEvent (2880, 1920, 62, false);   // half note starting on beat 4

        checkInvariant (melody, "over bar line");

        int pieces = 0;
        bool firstPieceTies = false;
        bool lastPieceTies  = true;

        for (const auto& event : melody.getEvents())
        {
            if (event.isRest || event.midiNote != 62)
                continue;

            if (pieces == 0)
                firstPieceTies = event.tiedToNext;

            lastPieceTies = event.tiedToNext;
            ++pieces;
        }

        checkEqual (pieces, 2, "half note across a bar line becomes two pieces");
        check (firstPieceTies, "the first piece ties into the second");
        check (! lastPieceTies, "the last piece does not tie onwards");

        const auto rehearsal = melody.getRehearsalNotes();
        checkEqual ((int) rehearsal.size(), 1, "a tied note is rehearsed as one note");
        std::cout << "  over bar line: " << describe (melody) << std::endl;
    }

    // -- syncopation is broken at the beat ------------------------------------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (480, 960, 64, false);     // quarter starting off the beat

        checkInvariant (melody, "syncopated");

        const auto rehearsal = melody.getRehearsalNotes();
        checkEqual ((int) rehearsal.size(), 1, "split syncopation is still one note to sing");
        std::cout << "  syncopated: " << describe (melody) << std::endl;
    }

    // -- writing over an existing note shortens it ----------------------------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (0, 1920, 60, false);      // half note
        melody.placeEvent (960, 960, 67, false);     // quarter over its second half

        checkInvariant (melody, "overwrite");

        checkEqual (melody.getEvents()[0].midiNote, 60, "the earlier note survives");
        checkEqual (melody.getEvents()[0].lengthTicks, 960, "shortened to a quarter");
        check (! melody.getEvents()[0].tiedToNext, "the overwrite breaks the tie");
        std::cout << "  overwrite: " << describe (melody) << std::endl;
    }

    // -- erasing leaves silence ----------------------------------------------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (0, 960, 60, false);
        melody.eraseRange (0, 960);

        checkInvariant (melody, "erased");
        check (melody.getRehearsalNotes().empty(), "nothing left to rehearse");
    }

    // -- 3/4 and 6/8 keep the invariant --------------------------------------
    {
        model::Melody melody;
        melody.setTimeSignature ({ 3, 4 });
        melody.setBarCount (2);
        melody.placeEvent (1920, 1920, 65, false);   // runs over the bar line
        checkInvariant (melody, "3/4");
        std::cout << "  3/4: " << describe (melody) << std::endl;

        model::Melody compound;
        compound.setTimeSignature ({ 6, 8 });
        compound.setBarCount (2);
        compound.placeEvent (480, 1440, 67, false);
        checkInvariant (compound, "6/8");
        checkEqual (compound.getTimeSignature().beamGroupTicks(), 1440, "6/8 groups in threes");
        std::cout << "  6/8: " << describe (compound) << std::endl;
    }

    // -- transposition applies to rehearsal, not to the written notes ---------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (0, 960, 60, false);
        melody.setTranspose (-12);

        checkEqual (melody.getRehearsalNotes()[0], 48, "rehearsal pitches are transposed");
        checkEqual (melody.getEvents()[0].midiNote, 60, "the written note is unchanged");
    }

    // -- playback merges tied notes back into one sound -----------------------
    {
        model::Melody melody;
        melody.setBarCount (2);
        melody.placeEvent (2880, 1920, 62, false);   // half note across the bar line

        const auto playback = melody.getPlaybackNotes();
        checkEqual ((int) playback.size(), 1, "a tied note plays as one note");
        checkEqual (playback[0].lengthTicks, 1920, "the tied pieces add back up");
        checkEqual (playback[0].startTick, 2880, "it starts where it was written");
        checkEqual (playback[0].midiNote, 62, "pitch survives");

        // The engraver colours by these indices, so they must line up with the
        // events one for one.
        const auto perEvent = melody.getRehearsalIndexPerEvent();
        checkEqual ((int) perEvent.size(), (int) melody.getEvents().size(),
                    "one rehearsal index per written event");

        int numbered = 0;

        for (auto index : perEvent)
            if (index >= 0)
                ++numbered;

        checkEqual (numbered, (int) playback.size(),
                    "exactly one event per sounding note carries an index");
    }

    // -- repeated notes are separate, tied ones are not -----------------------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (0, 960, 60, false);
        melody.placeEvent (960, 960, 60, false);     // same pitch again, not tied

        const auto playback = melody.getPlaybackNotes();
        checkEqual ((int) playback.size(), 2, "a repeated note is two notes to rehearse");
    }

    // -- playback pitches are transposed, indices are not disturbed -----------
    {
        model::Melody melody;
        melody.setBarCount (1);
        melody.placeEvent (0, 960, 60, false);
        melody.placeEvent (960, 960, 64, false);
        melody.setTranspose (7);

        const auto playback = melody.getPlaybackNotes();
        checkEqual ((int) playback.size(), 2, "two notes");
        checkEqual (playback[0].midiNote, 67, "first note transposed");
        checkEqual (playback[1].midiNote, 71, "second note transposed");
        checkEqual (playback[0].rehearsalIndex, 0, "indices start at zero");
        checkEqual (playback[1].rehearsalIndex, 1, "and run in order");
        check (melody.getRehearsalNotes() == std::vector<int> { 67, 71 },
               "rehearsal pitches agree with playback");
    }

    // -- JSON round trip ------------------------------------------------------
    {
        model::Melody melody;
        melody.setName ("Round trip");
        melody.setTimeSignature ({ 3, 4 });
        melody.setBarCount (3);
        melody.setTempoBpm (72.0);
        melody.setTranspose (2);
        melody.placeEvent (0, 960, 60, false);
        melody.placeEvent (960, 480, 64, false);
        melody.placeEvent (2400, 1920, 67, false);

        bool ok = false;
        const auto restored = model::Melody::fromJsonString (melody.toJsonString(), ok);

        check (ok, "JSON parses");
        checkInvariant (restored, "restored");
        check (restored.getName() == "Round trip", "name survives");
        checkEqual (restored.getBarCount(), melody.getBarCount(), "bar count survives");
        checkEqual (restored.getTranspose(), 2, "transpose survives");
        check (restored.getTimeSignature() == melody.getTimeSignature(), "time signature survives");
        check (restored.getRehearsalNotes() == melody.getRehearsalNotes(), "the tune itself survives");
        check (describe (restored) == describe (melody), "the written events match exactly");
    }

    // -- MIDI round trip -------------------------------------------------------
    {
        model::Melody melody;
        melody.setName ("Round trip");
        melody.setTimeSignature ({ 3, 4 });
        melody.setBarCount (2);
        melody.setTempoBpm (108.0);
        melody.placeEvent (0, 960, 60, false);
        melody.placeEvent (960, 480, 64, false);
        melody.placeEvent (1440, 1440, 67, false);

        const auto file = juce::File::createTempFile (".mid");
        check (model::MidiIO::exportToFile (melody, file), "MIDI export writes a file");

        model::Melody restored;
        juce::String report;
        const auto ok = model::MidiIO::importFromFile (file, restored, report);

        check (ok, "MIDI import reads it back");

        if (ok)
        {
            check (restored.getRehearsalNotes() == melody.getRehearsalNotes(),
                   "the notes survive a MIDI round trip");
            check (restored.getTimeSignature() == melody.getTimeSignature(),
                   "the time signature survives");
            check (std::abs (restored.getTempoBpm() - melody.getTempoBpm()) < 1.0,
                   "the tempo survives");
            checkInvariant (restored, "midi round trip");
        }

        file.deleteFile();
    }

    // -- importing a chord keeps the top line ---------------------------------
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (model::ticksPerQuarter);

        juce::MidiMessageSequence track;

        for (auto note : { 60, 64, 67 })
        {
            auto on = juce::MidiMessage::noteOn (1, note, (juce::uint8) 100);
            on.setTimeStamp (0.0);
            track.addEvent (on);

            auto off = juce::MidiMessage::noteOff (1, note);
            off.setTimeStamp ((double) model::ticksPerQuarter);
            track.addEvent (off);
        }

        track.updateMatchedPairs();
        file.addTrack (track);

        juce::MemoryOutputStream written;
        file.writeTo (written);

        juce::MemoryInputStream reading (written.getData(), written.getDataSize(), false);

        model::Melody restored;
        juce::String report;

        if (model::MidiIO::importFromStream (reading, restored, report))
        {
            const auto notes = restored.getRehearsalNotes();
            checkEqual ((int) notes.size(), 1, "a chord imports as one note");

            if (! notes.empty())
                checkEqual (notes[0], 67, "and it is the top one");
        }
        else
        {
            check (false, "a chord-only file still imports");
        }
    }

    // -- beaming ---------------------------------------------------------------
    //
    // Which notes share a beam is a rule about the music, not about the drawing,
    // so it is worth checking here rather than by squinting at the staff. The
    // cases that matter are the ones that break a beam: a rest, a barline, and a
    // note that outlasts its own beat.
    {
        const auto eighth    = model::ticksPerQuarter / 2;
        const auto sixteenth = model::ticksPerQuarter / 4;

        auto note = [] (int start, int length, int flags, bool rest = false)
        {
            ui::beaming::Candidate candidate;
            candidate.startTick   = start;
            candidate.lengthTicks = length;
            candidate.flags       = flags;
            candidate.isRest      = rest;
            return candidate;
        };

        const model::TimeSignature common { 4, 4 };
        const model::TimeSignature compound { 6, 8 };

        // Four eighths in 4/4 beam in twos, one to a quarter-note beat, not as
        // one long run of four.
        {
            std::vector<ui::beaming::Candidate> bar;

            for (int i = 0; i < 4; ++i)
                bar.push_back (note (i * eighth, eighth, 1));

            const auto groups = ui::beaming::group (bar, common);

            checkEqual ((int) groups.size(), 2, "4/4 beams eighths by the beat");

            if (groups.size() == 2)
            {
                checkEqual ((int) groups[0].size(), 2, "two under the first beam");
                checkEqual (groups[1][0], 2, "and the third note starts the second");
            }
        }

        // Six eighths in 6/8 beam in threes, because the beat is a dotted
        // quarter. Getting this wrong is what makes generated 6/8 look wrong.
        {
            std::vector<ui::beaming::Candidate> bar;

            for (int i = 0; i < 6; ++i)
                bar.push_back (note (i * eighth, eighth, 1));

            const auto groups = ui::beaming::group (bar, compound);

            checkEqual ((int) groups.size(), 2, "6/8 beams eighths in threes");

            if (groups.size() == 2)
            {
                checkEqual ((int) groups[0].size(), 3, "three under the first beam");
                checkEqual ((int) groups[1].size(), 3, "and three under the second");
            }
        }

        // A rest breaks a beam even mid-beat.
        {
            const std::vector<ui::beaming::Candidate> bar
            {
                note (0, sixteenth, 2),
                note (sixteenth, sixteenth, 2, true),
                note (2 * sixteenth, sixteenth, 2),
                note (3 * sixteenth, sixteenth, 2),
            };

            const auto groups = ui::beaming::group (bar, common);

            checkEqual ((int) groups.size(), 1, "a rest breaks the beam");

            if (groups.size() == 1)
            {
                checkEqual ((int) groups[0].size(), 2, "only what follows it is joined");
                checkEqual (groups[0][0], 2, "starting after the rest");
            }
        }

        // A quarter in the middle breaks it too, and the notes after it start a
        // group of their own rather than being skipped with it.
        {
            const std::vector<ui::beaming::Candidate> bar
            {
                note (0, eighth, 1),
                note (eighth, model::ticksPerQuarter, 0),
                note (eighth + model::ticksPerQuarter, eighth, 1),
                note (2 * model::ticksPerQuarter, eighth, 1),
                note (2 * model::ticksPerQuarter + eighth, eighth, 1),
            };

            const auto groups = ui::beaming::group (bar, common);

            checkEqual ((int) groups.size(), 1, "an unbeamable note breaks the run");

            if (groups.size() == 1)
                checkEqual (groups[0][0], 3, "and the beat after it beams normally");
        }

        // Nothing beams across a barline.
        {
            const std::vector<ui::beaming::Candidate> bar
            {
                note (4 * model::ticksPerQuarter - eighth, eighth, 1),
                note (4 * model::ticksPerQuarter, eighth, 1),
            };

            checkEqual ((int) ui::beaming::group (bar, common).size(), 0,
                        "no beam crosses a barline");
        }

        // Nor across a line break, even inside one beat.
        {
            auto first  = note (0, eighth, 1);
            auto second = note (eighth, eighth, 1);
            second.systemIndex = 1;

            checkEqual ((int) ui::beaming::group ({ first, second }, common).size(), 0,
                        "no beam crosses a system break");
        }

        // A lone eighth keeps its flag rather than becoming a beam of one.
        {
            const std::vector<ui::beaming::Candidate> bar
            {
                note (0, model::ticksPerQuarter, 0),
                note (model::ticksPerQuarter, eighth, 1),
                note (model::ticksPerQuarter + eighth, model::ticksPerQuarter, 0),
            };

            checkEqual ((int) ui::beaming::group (bar, common).size(), 0,
                        "a single eighth is flagged, not beamed");
        }

        // Mixed values inside one beat still share the primary beam.
        {
            const std::vector<ui::beaming::Candidate> bar
            {
                note (0, eighth, 1),
                note (eighth, sixteenth, 2),
                note (eighth + sixteenth, sixteenth, 2),
            };

            const auto groups = ui::beaming::group (bar, common);

            checkEqual ((int) groups.size(), 1, "an eighth and two sixteenths beam together");

            if (groups.size() == 1)
                checkEqual ((int) groups[0].size(), 3, "all three of them");
        }

        // Every generated exercise should beam into groups that stay inside one
        // beat - which is the property the whole rule exists to guarantee.
        {
            auto allInsideTheBeat = true;

            for (const auto& signature : model::getWritableMetres())
            {
                const auto beat = signature.beamGroupTicks();

                std::vector<ui::beaming::Candidate> bar;

                for (int tick = 0; tick < signature.barTicks() * 2; tick += sixteenth)
                    bar.push_back (note (tick, sixteenth, 2));

                for (const auto& grouped : ui::beaming::group (bar, signature))
                    for (auto index : grouped)
                        if (bar[(size_t) index].startTick / beat
                             != bar[(size_t) grouped.front()].startTick / beat)
                            allInsideTheBeat = false;
            }

            check (allInsideTheBeat, "every beam stays inside one beat, in every metre");
        }
    }

    // -- transposing a span ----------------------------------------------------
    //
    // What the selection tool does when you press an arrow. The interesting
    // cases are the edges: a note that would leave the MIDI range must stop the
    // whole move rather than being clamped, because clamping one note silently
    // rewrites the intervals around it.
    {
        model::Melody tune;
        tune.setBarCount (2);
        tune.placeEvent (0, model::ticksPerQuarter, 60, false);
        tune.placeEvent (model::ticksPerQuarter, model::ticksPerQuarter, 64, false);
        tune.placeEvent (2 * model::ticksPerQuarter, model::ticksPerQuarter, 67, false);

        check (tune.transposeRange (0, model::ticksPerQuarter * 2, 2),
               "a transpose inside the range succeeds");

        auto notes = tune.getRehearsalNotes();
        checkEqual ((int) notes.size(), 3, "still three notes");

        if (notes.size() == 3)
        {
            checkEqual (notes[0], 62, "the first note moved up a tone");
            checkEqual (notes[1], 66, "and so did the second");
            checkEqual (notes[2], 67, "the note outside the span did not");
        }

        // Rests are not notes and must come through untouched.
        model::Melody withRest;
        withRest.setBarCount (1);
        withRest.placeEvent (0, model::ticksPerQuarter, 60, false);
        withRest.placeEvent (model::ticksPerQuarter, model::ticksPerQuarter, 0, true);
        withRest.transposeRange (0, withRest.getTotalTicks(), 3);

        auto rests = 0;

        for (const auto& event : withRest.getEvents())
            if (event.isRest)
                ++rests;

        check (rests > 0, "rests survive a transpose");

        // All or nothing at the ceiling.
        model::Melody high;
        high.setBarCount (1);
        high.placeEvent (0, model::ticksPerQuarter, 60, false);
        high.placeEvent (model::ticksPerQuarter, model::ticksPerQuarter, 126, false);

        check (! high.transposeRange (0, high.getTotalTicks(), 6),
               "a transpose that would run past MIDI 127 is refused");

        const auto after = high.getRehearsalNotes();

        if (after.size() >= 2)
        {
            checkEqual (after[0], 60, "and nothing moved");
            checkEqual (after[1], 126, "not even the note that had room");
        }
    }

    // -- the bar ceiling -------------------------------------------------------
    {
        model::Melody tune;
        tune.setBarCount (model::maxBars + 50);
        checkEqual (tune.getBarCount(), model::maxBars, "the bar count is capped");

        tune.setBarCount (0);
        checkEqual (tune.getBarCount(), 1, "and never goes below one");

        // Writing past the end grows the tune rather than truncating the note.
        model::Melody growing;
        growing.setBarCount (2);
        const auto barTicks = growing.getTimeSignature().barTicks();
        growing.placeEvent (barTicks * 4, model::ticksPerQuarter, 60, false);

        check (growing.getBarCount() >= 5, "writing past the end adds bars");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
