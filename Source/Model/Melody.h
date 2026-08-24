#pragma once

#include <JuceHeader.h>
#include <vector>

namespace model
{

/** Ticks per quarter note. 960 divides cleanly by 2 and 3, so every note value
    we offer - including dotted ones - lands on a whole number of ticks. */
constexpr int ticksPerQuarter = 960;

/** The longest tune the editor will write. Not a technical limit - the model
    would take far more - but a number past which a single unbroken line stops
    being a practice exercise and starts being a piece. */
constexpr int maxBars = 256;

//==============================================================================
/** A writable note value: an undotted length plus zero or one dot.

    `baseTicks` is what picks the glyph (a dotted quarter still draws a quarter
    notehead), while `ticks` is the actual sounding length.
*/
struct NoteValueInfo
{
    int  ticks;        ///< total length including the dot
    int  baseTicks;    ///< undotted length, selects notehead / flags
    int  dots;         ///< 0 or 1
    const char* name;
};

/** Every value the editor can write, longest first. */
const std::vector<NoteValueInfo>& getNoteValues();

/** The value with exactly this tick length, or nullptr if it isn't writable
    as a single note. */
const NoteValueInfo* findNoteValue (int ticks);

/** Number of flags (0 = quarter or longer, 1 = eighth, 2 = sixteenth). */
int flagCountForBaseTicks (int baseTicks);

//==============================================================================
struct TimeSignature
{
    int numerator   = 4;
    int denominator = 4;

    int barTicks() const noexcept;

    /** Length of one beaming group - a quarter in 4/4, a dotted quarter in the
        compound metres. Also used to decide where a long note should be split
        so the beat stays visible. */
    int beamGroupTicks() const noexcept;

    bool operator== (const TimeSignature& other) const noexcept
    {
        return numerator == other.numerator && denominator == other.denominator;
    }
};

/** Every metre the engraver can write, and the only ones anything should
    offer. One list, so the note editor and the exercise generator cannot end up
    disagreeing about what is writable - which they did: generating a 9/8
    exercise left the editor's own metre box still saying 4/4, and touching it
    would have silently rebarred the tune. */
const std::vector<TimeSignature>& getWritableMetres();

//==============================================================================
/** One written element. After normalisation every event is a legal note value,
    sits entirely inside one bar, and the events tile the melody with no gaps. */
struct MelodyEvent
{
    int  startTick   = 0;
    int  lengthTicks = ticksPerQuarter;
    int  midiNote    = 60;      ///< meaningless when isRest
    bool isRest      = false;
    bool tiedToNext  = false;   ///< this note continues into the next event

    int endTick() const noexcept { return startTick + lengthTicks; }
};

//==============================================================================
/** A short monophonic tune.

    The central invariant is that **every bar is always full**: the events tile
    [0, totalTicks) with no gaps and no overlaps, rests filling anything not
    occupied by a note. Editing goes through placeEvent(), which restores that
    invariant, so no other code ever has to check or repair it.
*/
class Melody
{
public:
    Melody();

    //==============================================================================
    void setTimeSignature (TimeSignature newTimeSignature);
    TimeSignature getTimeSignature() const noexcept   { return timeSignature; }

    void setBarCount (int newBarCount);
    int  getBarCount() const noexcept                 { return barCount; }
    int  getTotalTicks() const noexcept               { return barCount * timeSignature.barTicks(); }

    void setTempoBpm (double bpm);
    double getTempoBpm() const noexcept               { return tempoBpm; }

    /** Melodies are monophonic, so one staff with a chosen clef rather than a
        grand staff. */
    void setTrebleClef (bool shouldUseTreble);
    bool isTrebleClef() const noexcept                { return trebleClef; }

    void setName (const juce::String& newName);
    juce::String getName() const                      { return name; }

    /** Semitone offset applied when playing or rehearsing - lets a tune be
        moved into a comfortable singing range without rewriting it. */
    void setTranspose (int semitones);
    int  getTranspose() const noexcept                { return transpose; }

    //==============================================================================
    const std::vector<MelodyEvent>& getEvents() const noexcept { return events; }

    /** Overwrites [startTick, startTick + lengthTicks) with a note or a rest,
        then restores the every-bar-is-full invariant. Anything the new event
        partly covers is trimmed back to a rest, which is what step-time entry
        in a notation editor does. */
    void placeEvent (int startTick, int lengthTicks, int midiNote, bool isRest);

    /** Replaces the contents of a span with rests. */
    void eraseRange (int startTick, int lengthTicks);

    void clear();

    /** One sounding note: tied pieces merged back into a single note, which is
        what both playback and rehearsal care about. */
    struct PlaybackNote
    {
        int midiNote       = 60;   ///< transposed
        int startTick      = 0;
        int lengthTicks    = 0;    ///< the whole tied chain
        int rehearsalIndex = 0;
    };

    /** The sounding notes, in order. Everything that needs to agree on what
        counts as "a note" derives from this one walk, so the staff's
        highlighting and the rehearsal engine can never drift apart. */
    std::vector<PlaybackNote> getPlaybackNotes() const;

    /** One entry per written event: its rehearsal index, or -1 for rests and
        for the tail of a tie. Lets the engraver colour notes by the same
        numbering the engine uses. */
    std::vector<int> getRehearsalIndexPerEvent() const;

    /** The pitches to rehearse, in order, with tied groups collapsed to one
        note and rests dropped. Transposition is applied. */
    std::vector<int> getRehearsalNotes() const;

    /** Lowest and highest sounding notes, or {-1,-1} when the tune is empty. */
    void getPitchRange (int& lowest, int& highest) const;

    //==============================================================================
    juce::var toVar() const;
    static Melody fromVar (const juce::var& source);

    juce::String toJsonString() const;
    static Melody fromJsonString (const juce::String& json, bool& ok);

private:
    /** Rebuilds the event list so it tiles the melody with no gaps, no event
        crosses a bar line, and every event is a legal note value. */
    void normalise();

    /** Splits a span into legal note values. Called per bar, so it never has to
        think about bar lines itself. */
    void appendDecomposed (std::vector<MelodyEvent>& out, int startTick, int lengthTicks,
                           int midiNote, bool isRest, bool tieIntoNext) const;

    TimeSignature timeSignature;
    int    barCount  = 4;
    double tempoBpm  = 90.0;
    bool   trebleClef = true;
    int    transpose = 0;
    juce::String name { "Untitled" };

    std::vector<MelodyEvent> events;
};

} // namespace model
