#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace practice
{

enum class MatchSource { none, midi, voice };

/** Step by step waits for each note however long you take. In time runs against
    the metronome and judges when each note arrived. Step by step is the
    default, and stays useful whatever else gets added. */
enum class RehearsalMode { stepByStep, inTime };

//==============================================================================
/** How forgiving the voice matcher should be.

    Singing is not MIDI: people sing in their own octave, never land exactly on
    the note, and hold it rather than triggering an instant. Each of those needs
    its own allowance, and different users want them set differently.
*/
struct VoiceMatchSettings
{
    /** Match by note name, ignoring which octave it was sung in. On by default:
        without it, a tune above someone's range fails constantly. */
    bool octaveAgnostic = true;

    /** How far off still counts, in cents. 50 accepts anything rounding to the
        right semitone; tighten it for intonation practice. */
    double centsTolerance = 50.0;

    /** How long the pitch must hold before it is accepted. This is what stops a
        slide from C to E registering the D it passes through. */
    int stabilityMs = 200;

    /** Below this the detector is not sure enough to be believed. */
    double minConfidence = 0.5;

    /** How far behind the singing the detector's answer arrives: the analysis
        window, the smoothing, and the hop between frames all add delay. Timed
        mode subtracts this from the onset, because otherwise every sung note
        would be graded late by a fixed amount that has nothing to do with the
        singer. */
    double latencyMs = 100.0;
};

/** Timing windows for in-time mode. */
struct TimingSettings
{
    double goodMs = 90.0;    ///< inside this counts as on the beat
    double missMs = 260.0;   ///< outside this the note cannot be claimed at all
};

//==============================================================================
/** What happened on one note.

    Both timestamps are kept on purpose. The voice matcher only confirms after
    its stability window, so `confirmedMs` always lags the singing by a variable
    amount; `firstArrivalMs` is the honest onset, and it is what timing is
    graded from.
*/
struct NoteOutcome
{
    int    index          = 0;
    int    targetNote     = 0;
    bool   resolved       = false;
    bool   hit            = false;   ///< resolved and not hit means missed
    double firstArrivalMs = 0.0;
    double confirmedMs    = 0.0;
    double timingErrorMs  = 0.0;     ///< positive is late, negative early
    double centsError     = 0.0;
    int    wrongNotes     = 0;
    MatchSource source    = MatchSource::none;
};

//==============================================================================
/** Drives a tune one note at a time, or against the clock.

    The pitch test is shared by both modes and by both input sources; what
    differs is only when a note is considered due and whether the tune moves on
    without you.
*/
class RehearsalEngine
{
public:
    RehearsalEngine() = default;

    /** Step mode: just the pitches. */
    void setNotes (std::vector<int> notesToRehearse);

    /** In-time mode: the same pitches with the moment each falls due, in
        milliseconds on the transport clock. */
    void setTimedNotes (std::vector<int> notesToRehearse, std::vector<double> dueTimesMs);

    const std::vector<int>& getNotes() const noexcept { return notes; }

    void setMode (RehearsalMode newMode);
    RehearsalMode getMode() const noexcept { return mode; }

    void start();
    void stop();

    bool isRunning() const noexcept { return running; }
    bool isComplete() const noexcept;

    /** The note being waited for, or -1. */
    int getTargetIndex() const noexcept;
    int getTargetNote() const noexcept;
    int getCompletedCount() const noexcept;
    int getTotalWrongNotes() const noexcept { return totalWrongNotes; }

    void setVoiceSettings (VoiceMatchSettings newSettings) { settings = newSettings; }
    VoiceMatchSettings getVoiceSettings() const noexcept   { return settings; }

    void setTimingSettings (TimingSettings newSettings) { timing = newSettings; }
    TimingSettings getTimingSettings() const noexcept    { return timing; }

    /** Accept any pitch, and judge only whether each note arrived.

        Rhythm before notes is the oldest piece of sight-reading teaching there
        is: you cannot read a line whose rhythm you have not already worked out,
        and trying to do both at once is what makes a reader stall. With this on
        the tune can be tapped, played on one key, or sung on any comfortable
        note, and everything else - the cursor, the timing windows, the
        scoring - behaves exactly as it does normally.
    */
    void setIgnorePitch (bool shouldIgnore) noexcept     { ignorePitch = shouldIgnore; }
    bool isIgnoringPitch() const noexcept                { return ignorePitch; }

    void setMidiEnabled (bool shouldBeEnabled) noexcept  { midiEnabled = shouldBeEnabled; }
    void setVoiceEnabled (bool shouldBeEnabled) noexcept { voiceEnabled = shouldBeEnabled; }

    /** A key press. Exact, so it is judged straight away. */
    void handleMidiNote (int midiNote, double nowMs);

    /** The detector's latest reading. Deliberately plain values rather than the
        analyser's struct, so this class has no dependency on the audio side and
        can be tested on its own. */
    void handleVoicePitch (bool hasPitch, int nearestNote, double cents,
                           double confidence, double nowMs);

    /** In-time mode only: move the clock on, retiring anything now too late. */
    void advanceTransport (double transportMs);

    const std::vector<NoteOutcome>& getOutcomes() const noexcept { return outcomes; }

    /** Hits, misses, and the average signed timing error over the hits. */
    void getTimingSummary (int& hits, int& misses, double& averageErrorMs) const;

    std::function<void()> onChanged;
    std::function<void()> onComplete;
    std::function<void (int playedNote)> onWrongNote;

private:
    bool matches (int playedNote, int targetNote) const;
    void registerHit (int noteIndex, double onsetMs, double confirmedMs,
                      double centsError, MatchSource source);
    void countWrongNote (int playedNote);
    void acceptStep (MatchSource source, double nowMs, double centsError);
    void beginStepTarget();
    int  findTimedCandidate (int playedNote, double atMs) const;
    void checkComplete();

    std::vector<int>    notes;
    std::vector<double> dueMs;
    std::vector<NoteOutcome> outcomes;

    VoiceMatchSettings settings;
    TimingSettings     timing;
    RehearsalMode      mode = RehearsalMode::stepByStep;

    bool running      = false;
    bool ignorePitch  = false;
    bool midiEnabled  = true;
    bool voiceEnabled = true;
    bool completeFired = false;

    int targetIndex     = -1;    ///< step mode cursor
    int totalWrongNotes = 0;

    // Voice state for the target currently being waited on.
    double candidateSinceMs = -1.0;
    int    wrongNotesHere   = 0;
    bool   awaitingClear    = false;
    int    justAcceptedNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RehearsalEngine)
};

} // namespace practice
