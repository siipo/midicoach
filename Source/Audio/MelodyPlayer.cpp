#include "MelodyPlayer.h"

namespace audio
{

namespace
{
    /** Notes stop a little early so a repeated pitch is audibly two notes
        rather than one long one. */
    constexpr double detachFactor = 0.9;
    constexpr int    tickRateHz   = 60;
}

MelodyPlayer::MelodyPlayer (juce::MidiKeyboardState& stateToDrive)
    : keyboardState (stateToDrive)
{
}

MelodyPlayer::~MelodyPlayer()
{
    stop();
}

//==============================================================================
void MelodyPlayer::play (const model::Melody& melody)
{
    stop();

    const auto tempo = juce::jmax (20.0, melody.getTempoBpm());
    const auto msPerTick = 60000.0 / (tempo * (double) model::ticksPerQuarter);

    for (const auto& note : melody.getPlaybackNotes())
    {
        ScheduledNote scheduled;
        scheduled.midiNote       = juce::jlimit (0, 127, note.midiNote);
        scheduled.startMs        = note.startTick * msPerTick;
        scheduled.endMs          = scheduled.startMs + note.lengthTicks * msPerTick * detachFactor;
        scheduled.rehearsalIndex = note.rehearsalIndex;
        schedule.push_back (scheduled);
    }

    if (schedule.empty())
        return;

    startedAtMs = juce::Time::getMillisecondCounterHiRes();
    playing = true;
    startTimerHz (tickRateHz);
}

void MelodyPlayer::playSingleNote (int midiNote, int durationMs)
{
    stop();

    ScheduledNote scheduled;
    scheduled.midiNote = juce::jlimit (0, 127, midiNote);
    scheduled.startMs  = 0.0;
    scheduled.endMs    = (double) juce::jmax (50, durationMs);
    schedule.push_back (scheduled);

    startedAtMs = juce::Time::getMillisecondCounterHiRes();
    playing = true;
    startTimerHz (tickRateHz);
}

void MelodyPlayer::playChords (const std::vector<std::vector<int>>& chords, int msPerChord)
{
    stop();

    const auto step = (double) juce::jmax (120, msPerChord);
    auto position = 0.0;

    for (const auto& chord : chords)
    {
        for (auto note : chord)
        {
            ScheduledNote scheduled;
            scheduled.midiNote = juce::jlimit (0, 127, note);
            scheduled.startMs  = position;
            scheduled.endMs    = position + step * detachFactor;
            schedule.push_back (scheduled);
        }

        position += step;
    }

    if (schedule.empty())
        return;

    startedAtMs = juce::Time::getMillisecondCounterHiRes();
    playing = true;
    startTimerHz (tickRateHz);
}

void MelodyPlayer::stop()
{
    stopTimer();
    releaseSoundingNotes();

    schedule.clear();
    playing = false;
}

void MelodyPlayer::releaseSoundingNotes()
{
    for (auto& scheduled : schedule)
    {
        if (scheduled.started && ! scheduled.finished)
        {
            keyboardState.noteOff (1, scheduled.midiNote, 0.0f);
            scheduled.finished = true;
        }
    }
}

//==============================================================================
void MelodyPlayer::timerCallback()
{
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startedAtMs;
    auto allDone = true;

    for (auto& scheduled : schedule)
    {
        if (! scheduled.started && elapsed >= scheduled.startMs)
        {
            scheduled.started = true;
            keyboardState.noteOn (1, scheduled.midiNote, 0.8f);

            if (onNoteStarted != nullptr && scheduled.rehearsalIndex >= 0)
                onNoteStarted (scheduled.rehearsalIndex);
        }

        if (scheduled.started && ! scheduled.finished && elapsed >= scheduled.endMs)
        {
            scheduled.finished = true;
            keyboardState.noteOff (1, scheduled.midiNote, 0.0f);
        }

        if (! scheduled.finished)
            allDone = false;
    }

    if (! allDone)
        return;

    stopTimer();
    schedule.clear();
    playing = false;

    if (onFinished != nullptr)
        onFinished();
}

} // namespace audio
