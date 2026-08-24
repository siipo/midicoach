#include "Melody.h"
#include <algorithm>

namespace model
{

/** Everything is quantised to this, and every writable note value is a whole
    number of them. That guarantees the decomposition below always terminates:
    any leftover length is at least one grid step, and a sixteenth always fits. */
static constexpr int gridTicks = 240;   // a sixteenth

static int snapToGrid (int ticks) noexcept
{
    return ((ticks + gridTicks / 2) / gridTicks) * gridTicks;
}

//==============================================================================
const std::vector<NoteValueInfo>& getNoteValues()
{
    // Longest first - the decomposition below relies on this ordering.
    // Dotted sixteenths are deliberately absent: including them would force a
    // finer grid for no real benefit in the kind of tunes this is for.
    static const std::vector<NoteValueInfo> values
    {
        { 3840, 3840, 0, "whole" },
        { 2880, 1920, 1, "dotted half" },
        { 1920, 1920, 0, "half" },
        { 1440,  960, 1, "dotted quarter" },
        {  960,  960, 0, "quarter" },
        {  720,  480, 1, "dotted eighth" },
        {  480,  480, 0, "eighth" },
        {  240,  240, 0, "sixteenth" },
    };

    return values;
}

const NoteValueInfo* findNoteValue (int ticks)
{
    for (const auto& value : getNoteValues())
        if (value.ticks == ticks)
            return &value;

    return nullptr;
}

int flagCountForBaseTicks (int baseTicks)
{
    if (baseTicks <= 240) return 2;
    if (baseTicks <= 480) return 1;

    return 0;
}

//==============================================================================
const std::vector<TimeSignature>& getWritableMetres()
{
    // Simple metres first, then compound, in the order reading meets them.
    static const std::vector<TimeSignature> metres
    {
        { 4, 4 }, { 3, 4 }, { 2, 4 }, { 2, 2 }, { 6, 8 }, { 9, 8 }, { 12, 8 },
    };

    return metres;
}

int TimeSignature::barTicks() const noexcept
{
    const auto safeDenominator = juce::jmax (1, denominator);

    return juce::jmax (1, numerator) * (4 * ticksPerQuarter) / safeDenominator;
}

int TimeSignature::beamGroupTicks() const noexcept
{
    // Compound metres (6/8, 9/8, 12/8) group in threes; everything else groups
    // by its own beat.
    if (denominator == 8 && numerator % 3 == 0 && numerator > 3)
        return 3 * (4 * ticksPerQuarter) / 8;

    return juce::jmax (gridTicks, (4 * ticksPerQuarter) / juce::jmax (1, denominator));
}

//==============================================================================
Melody::Melody()
{
    clear();
}

void Melody::setTimeSignature (TimeSignature newTimeSignature)
{
    if (newTimeSignature.numerator < 1 || newTimeSignature.denominator < 1)
        return;

    timeSignature = newTimeSignature;
    normalise();
}

void Melody::setBarCount (int newBarCount)
{
    barCount = juce::jlimit (1, maxBars, newBarCount);
    normalise();
}

void Melody::setTempoBpm (double bpm)
{
    tempoBpm = juce::jlimit (20.0, 300.0, bpm);
}

void Melody::setTrebleClef (bool shouldUseTreble)
{
    trebleClef = shouldUseTreble;
}

void Melody::setName (const juce::String& newName)
{
    name = newName;
}

void Melody::setTranspose (int semitones)
{
    transpose = juce::jlimit (-24, 24, semitones);
}

bool Melody::transposeRange (int startTick, int lengthTicks, int semitones)
{
    const auto endTick = startTick + lengthTicks;

    // Check the whole span first. Half a transposed phrase is worse than none,
    // and clamping at the edges would quietly change the intervals.
    for (const auto& event : events)
        if (! event.isRest && event.startTick < endTick && event.endTick() > startTick)
            if (event.midiNote + semitones < 0 || event.midiNote + semitones > 127)
                return false;

    for (auto& event : events)
        if (! event.isRest && event.startTick < endTick && event.endTick() > startTick)
            event.midiNote += semitones;

    return true;
}

void Melody::clear()
{
    events.clear();
    normalise();
}

//==============================================================================
void Melody::placeEvent (int startTick, int lengthTicks, int midiNote, bool isRest)
{
    startTick   = juce::jmax (0, snapToGrid (startTick));
    lengthTicks = juce::jmax (gridTicks, snapToGrid (lengthTicks));

    // Grow the tune if the new event runs past the end, so writing near the
    // last bar just adds another one rather than silently truncating.
    const auto barLength = timeSignature.barTicks();
    const auto barsNeeded = (startTick + lengthTicks + barLength - 1) / barLength;

    if (barsNeeded > barCount)
        barCount = juce::jlimit (1, maxBars, barsNeeded);

    const auto endTick = startTick + lengthTicks;

    std::vector<MelodyEvent> rebuilt;
    rebuilt.reserve (events.size() + 3);

    for (const auto& existing : events)
    {
        if (existing.endTick() <= startTick || existing.startTick >= endTick)
        {
            rebuilt.push_back (existing);
            continue;
        }

        // A note that started before the new one keeps sounding, just shorter.
        if (existing.startTick < startTick)
        {
            auto left = existing;
            left.lengthTicks = startTick - existing.startTick;
            left.tiedToNext  = false;      // the overwrite breaks the tie
            rebuilt.push_back (left);
        }

        // Whatever was going to sound after the new event is cut off, so it
        // becomes silence rather than a stray fragment of the old note.
        if (existing.endTick() > endTick)
        {
            auto right = existing;
            right.startTick   = endTick;
            right.lengthTicks = existing.endTick() - endTick;
            right.isRest      = true;
            right.tiedToNext  = false;
            rebuilt.push_back (right);
        }
    }

    MelodyEvent placed;
    placed.startTick   = startTick;
    placed.lengthTicks = lengthTicks;
    placed.midiNote    = midiNote;
    placed.isRest      = isRest;
    rebuilt.push_back (placed);

    events = std::move (rebuilt);
    normalise();
}

void Melody::eraseRange (int startTick, int lengthTicks)
{
    placeEvent (startTick, lengthTicks, 0, true);
}

//==============================================================================
void Melody::normalise()
{
    const auto total     = getTotalTicks();
    const auto barLength = timeSignature.barTicks();

    std::stable_sort (events.begin(), events.end(),
                      [] (const MelodyEvent& a, const MelodyEvent& b)
                      { return a.startTick < b.startTick; });

    // Pass one: produce spans that tile [0, total) exactly, filling any hole
    // with a rest and clipping anything that overlaps or overruns.
    std::vector<MelodyEvent> spans;
    int cursor = 0;

    auto appendRest = [&spans] (int start, int length)
    {
        if (length <= 0)
            return;

        MelodyEvent rest;
        rest.startTick   = start;
        rest.lengthTicks = length;
        rest.isRest      = true;
        spans.push_back (rest);
    };

    for (const auto& event : events)
    {
        if (event.lengthTicks <= 0)
            continue;

        const auto start = juce::jmax (cursor, event.startTick);
        const auto end   = juce::jmin (total, event.endTick());

        if (end <= start)
            continue;

        appendRest (cursor, start - cursor);

        auto clipped = event;
        clipped.startTick   = start;
        clipped.lengthTicks = end - start;
        spans.push_back (clipped);

        cursor = end;
    }

    appendRest (cursor, total - cursor);

    // Pass two: merge neighbouring rests, so a long silence can be written as
    // one big rest instead of a row of small ones.
    std::vector<MelodyEvent> merged;

    for (const auto& span : spans)
    {
        if (! merged.empty() && merged.back().isRest && span.isRest
             && merged.back().endTick() == span.startTick)
        {
            merged.back().lengthTicks += span.lengthTicks;
            continue;
        }

        merged.push_back (span);
    }

    // Pass three: nothing may cross a bar line, and everything must be a legal
    // note value. Notes broken up this way are tied back together.
    events.clear();

    for (const auto& span : merged)
    {
        auto position  = span.startTick;
        auto remaining = span.lengthTicks;

        while (remaining > 0)
        {
            const auto barEnd = (position / barLength + 1) * barLength;
            const auto piece  = juce::jmin (remaining, barEnd - position);
            const auto isLastPiece = (remaining - piece) <= 0;

            appendDecomposed (events, position, piece, span.midiNote, span.isRest,
                              isLastPiece ? span.tiedToNext : true);

            position  += piece;
            remaining -= piece;
        }
    }
}

void Melody::appendDecomposed (std::vector<MelodyEvent>& out, int startTick, int lengthTicks,
                               int midiNote, bool isRest, bool tieIntoNext) const
{
    const auto barLength = timeSignature.barTicks();
    const auto group     = timeSignature.beamGroupTicks();

    std::vector<MelodyEvent> pieces;

    auto position  = startTick;
    auto remaining = lengthTicks;

    while (remaining > 0)
    {
        const auto positionInBar = position % barLength;
        auto longestAllowed = remaining;

        // Starting off the beat, don't run past the next beat - keeping the
        // pulse visible is what makes syncopation readable.
        if (group > 0 && (positionInBar % group) != 0)
            longestAllowed = juce::jmin (remaining, group - (positionInBar % group));

        const NoteValueInfo* chosen = nullptr;

        for (const auto& value : getNoteValues())
        {
            if (value.ticks <= longestAllowed)
            {
                chosen = &value;
                break;
            }
        }

        // Unreachable on the sixteenth grid, but never spin forever if it is.
        const auto length = chosen != nullptr ? juce::jmin (chosen->ticks, remaining)
                                              : remaining;

        MelodyEvent piece;
        piece.startTick   = position;
        piece.lengthTicks = length;
        piece.midiNote    = midiNote;
        piece.isRest      = isRest;
        pieces.push_back (piece);

        position  += length;
        remaining -= length;
    }

    for (size_t i = 0; i < pieces.size(); ++i)
        pieces[i].tiedToNext = isRest ? false
                                      : (i + 1 < pieces.size() ? true : tieIntoNext);

    out.insert (out.end(), pieces.begin(), pieces.end());
}

//==============================================================================
std::vector<Melody::PlaybackNote> Melody::getPlaybackNotes() const
{
    std::vector<PlaybackNote> result;

    bool previousWasTied = false;
    int  previousNote    = -1;

    for (const auto& event : events)
    {
        if (event.isRest)
        {
            previousWasTied = false;
            previousNote    = -1;
            continue;
        }

        // The tail of a tied note is the same sound continuing, so it extends
        // the note already running rather than starting a new one.
        const auto isContinuation = previousWasTied && previousNote == event.midiNote;

        if (isContinuation && ! result.empty())
        {
            result.back().lengthTicks += event.lengthTicks;
        }
        else
        {
            PlaybackNote note;
            note.midiNote       = event.midiNote + transpose;
            note.startTick      = event.startTick;
            note.lengthTicks    = event.lengthTicks;
            note.rehearsalIndex = (int) result.size();
            result.push_back (note);
        }

        previousWasTied = event.tiedToNext;
        previousNote    = event.midiNote;
    }

    return result;
}

std::vector<int> Melody::getRehearsalIndexPerEvent() const
{
    std::vector<int> result;
    result.reserve (events.size());

    bool previousWasTied = false;
    int  previousNote    = -1;
    int  next            = 0;

    for (const auto& event : events)
    {
        if (event.isRest)
        {
            result.push_back (-1);
            previousWasTied = false;
            previousNote    = -1;
            continue;
        }

        const auto isContinuation = previousWasTied && previousNote == event.midiNote;

        result.push_back (isContinuation ? -1 : next++);

        previousWasTied = event.tiedToNext;
        previousNote    = event.midiNote;
    }

    return result;
}

std::vector<int> Melody::getRehearsalNotes() const
{
    std::vector<int> result;

    for (const auto& note : getPlaybackNotes())
        result.push_back (note.midiNote);

    return result;
}

void Melody::getPitchRange (int& lowest, int& highest) const
{
    lowest  = -1;
    highest = -1;

    for (const auto& event : events)
    {
        if (event.isRest)
            continue;

        lowest  = lowest  < 0 ? event.midiNote : juce::jmin (lowest,  event.midiNote);
        highest = highest < 0 ? event.midiNote : juce::jmax (highest, event.midiNote);
    }
}

//==============================================================================
juce::var Melody::toVar() const
{
    auto* object = new juce::DynamicObject();

    object->setProperty ("name", name);
    object->setProperty ("numerator", timeSignature.numerator);
    object->setProperty ("denominator", timeSignature.denominator);
    object->setProperty ("bars", barCount);
    object->setProperty ("tempo", tempoBpm);
    object->setProperty ("treble", trebleClef);
    object->setProperty ("transpose", transpose);

    juce::Array<juce::var> eventArray;

    for (const auto& event : events)
    {
        // Tied continuations and rest padding are rebuilt by normalise(), so
        // only the notes themselves need storing.
        if (event.isRest)
            continue;

        auto* entry = new juce::DynamicObject();
        entry->setProperty ("start", event.startTick);
        entry->setProperty ("length", event.lengthTicks);
        entry->setProperty ("note", event.midiNote);
        entry->setProperty ("tied", event.tiedToNext);
        eventArray.add (juce::var (entry));
    }

    object->setProperty ("events", eventArray);

    return juce::var (object);
}

Melody Melody::fromVar (const juce::var& source)
{
    Melody melody;

    if (auto* object = source.getDynamicObject())
    {
        melody.name       = object->getProperty ("name").toString();
        melody.tempoBpm   = (double) object->getProperty ("tempo");
        melody.trebleClef = (bool) object->getProperty ("treble");
        melody.transpose  = (int) object->getProperty ("transpose");

        TimeSignature signature;
        signature.numerator   = juce::jmax (1, (int) object->getProperty ("numerator"));
        signature.denominator = juce::jmax (1, (int) object->getProperty ("denominator"));
        melody.timeSignature  = signature;

        melody.barCount = juce::jlimit (1, maxBars, (int) object->getProperty ("bars"));

        // The default constructor already filled the melody with rests for its
        // own time signature - drop those before loading, or they survive
        // normalisation and clip the tune being restored.
        melody.events.clear();

        if (auto* array = object->getProperty ("events").getArray())
        {
            for (const auto& entry : *array)
            {
                if (auto* entryObject = entry.getDynamicObject())
                {
                    MelodyEvent event;
                    event.startTick   = (int) entryObject->getProperty ("start");
                    event.lengthTicks = (int) entryObject->getProperty ("length");
                    event.midiNote    = (int) entryObject->getProperty ("note");
                    event.tiedToNext  = (bool) entryObject->getProperty ("tied");
                    melody.events.push_back (event);
                }
            }
        }
    }

    if (melody.tempoBpm < 20.0)
        melody.tempoBpm = 90.0;

    melody.normalise();

    return melody;
}

juce::String Melody::toJsonString() const
{
    return juce::JSON::toString (toVar());
}

Melody Melody::fromJsonString (const juce::String& json, bool& ok)
{
    juce::var parsed;
    ok = juce::JSON::parse (json, parsed).wasOk() && parsed.getDynamicObject() != nullptr;

    return ok ? fromVar (parsed) : Melody();
}

} // namespace model
