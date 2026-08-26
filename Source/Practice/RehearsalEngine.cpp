#include "RehearsalEngine.h"
#include <cmath>

namespace practice
{

void RehearsalEngine::setNotes (std::vector<int> notesToRehearse)
{
    notes = std::move (notesToRehearse);
    dueMs.clear();
    stop();
}

void RehearsalEngine::setTimedNotes (std::vector<int> notesToRehearse, std::vector<double> dueTimesMs)
{
    notes = std::move (notesToRehearse);
    dueMs = std::move (dueTimesMs);
    dueMs.resize (notes.size(), 0.0);
    stop();
}

void RehearsalEngine::setMode (RehearsalMode newMode)
{
    if (mode == newMode)
        return;

    mode = newMode;
    stop();
}

//==============================================================================
void RehearsalEngine::start()
{
    if (notes.empty())
        return;

    // In-time mode needs somewhere for every note to fall due.
    if (mode == RehearsalMode::inTime && dueMs.size() != notes.size())
        return;

    running          = true;
    completeFired    = false;
    targetIndex      = 0;
    totalWrongNotes  = 0;
    justAcceptedNote = -1;
    awaitingClear    = false;
    candidateSinceMs = -1.0;
    wrongNotesHere   = 0;

    outcomes.clear();
    outcomes.reserve (notes.size());

    for (size_t i = 0; i < notes.size(); ++i)
    {
        NoteOutcome outcome;
        outcome.index      = (int) i;
        outcome.targetNote = notes[i];
        outcomes.push_back (outcome);
    }

    if (onChanged != nullptr)
        onChanged();
}

void RehearsalEngine::stop()
{
    running     = false;
    targetIndex = -1;

    candidateSinceMs = -1.0;
    wrongNotesHere   = 0;
    awaitingClear    = false;

    if (onChanged != nullptr)
        onChanged();
}

bool RehearsalEngine::isComplete() const noexcept
{
    // Deliberately not conditioned on running. A finished run stops itself, and
    // "did that run get to the end?" is a question worth being able to ask
    // afterwards - which is exactly when it is asked. An empty outcome list is
    // the guard that stops this being true before anything has been set up.
    if (outcomes.empty())
        return false;

    for (const auto& outcome : outcomes)
        if (! outcome.resolved)
            return false;

    return true;
}

//==============================================================================
int RehearsalEngine::getTargetIndex() const noexcept
{
    if (! running)
        return -1;

    if (mode == RehearsalMode::stepByStep)
        return (targetIndex >= 0 && targetIndex < (int) notes.size()) ? targetIndex : -1;

    // In time, the target is simply the first note still outstanding.
    for (const auto& outcome : outcomes)
        if (! outcome.resolved)
            return outcome.index;

    return -1;
}

int RehearsalEngine::getTargetNote() const noexcept
{
    const auto index = getTargetIndex();

    return index < 0 ? -1 : notes[(size_t) index];
}

int RehearsalEngine::getCompletedCount() const noexcept
{
    if (mode == RehearsalMode::stepByStep)
        return juce::jmax (0, targetIndex);

    int resolved = 0;

    for (const auto& outcome : outcomes)
        if (outcome.resolved)
            ++resolved;

    return resolved;
}

bool RehearsalEngine::matches (int playedNote, int targetNote) const
{
    // Working on the rhythm: any note is the right note.
    if (ignorePitch)
        return true;

    if (settings.octaveAgnostic)
        return ((playedNote % 12) + 12) % 12 == ((targetNote % 12) + 12) % 12;

    return playedNote == targetNote;
}

void RehearsalEngine::countWrongNote (int playedNote)
{
    ++wrongNotesHere;
    ++totalWrongNotes;

    if (onWrongNote != nullptr)
        onWrongNote (playedNote);

    if (onChanged != nullptr)
        onChanged();
}

void RehearsalEngine::checkComplete()
{
    if (completeFired || ! isComplete())
        return;

    completeFired = true;

    // A finished run stops being a run. Leaving it going meant the engine went
    // on waiting for notes that had all been answered - so the transport, the
    // click and the Stop button all carried on with nothing left to match.
    // Every caller has checkComplete() as its last statement, so clearing this
    // here cannot surprise one of them half way through.
    // Only the running flag: getTargetIndex() already reports -1 once that is
    // clear, while getCompletedCount() still reads targetIndex to know how far
    // the run got. Clearing it here quietly reported nothing completed.
    running = false;

    if (onComplete != nullptr)
        onComplete();
}

//==============================================================================
// Step by step
//==============================================================================
void RehearsalEngine::beginStepTarget()
{
    candidateSinceMs = -1.0;
    wrongNotesHere   = 0;

    // A held note only has to be released when the next target is the same
    // pitch. Otherwise moving to the new note is itself the release.
    awaitingClear = justAcceptedNote >= 0
                     && targetIndex >= 0 && targetIndex < (int) notes.size()
                     && matches (justAcceptedNote, notes[(size_t) targetIndex]);
}

void RehearsalEngine::acceptStep (MatchSource source, double nowMs, double centsError)
{
    auto& outcome = outcomes[(size_t) targetIndex];
    outcome.resolved       = true;
    outcome.hit            = true;
    outcome.firstArrivalMs = candidateSinceMs >= 0.0 ? candidateSinceMs : nowMs;
    outcome.confirmedMs    = nowMs;
    outcome.centsError     = centsError;
    outcome.wrongNotes     = wrongNotesHere;
    outcome.source         = source;

    justAcceptedNote = notes[(size_t) targetIndex];
    ++targetIndex;

    if (targetIndex < (int) notes.size())
        beginStepTarget();

    if (onChanged != nullptr)
        onChanged();

    checkComplete();
}

//==============================================================================
// In time
//==============================================================================
int RehearsalEngine::findTimedCandidate (int playedNote, double atMs) const
{
    int best = -1;
    double bestError = 0.0;

    for (const auto& outcome : outcomes)
    {
        if (outcome.resolved || ! matches (playedNote, outcome.targetNote))
            continue;

        const auto error = atMs - dueMs[(size_t) outcome.index];

        if (std::abs (error) > timing.missMs)
            continue;

        // Claim the note this is nearest to, so an early attack goes to the
        // note it was aiming at rather than the one just gone.
        if (best < 0 || std::abs (error) < std::abs (bestError))
        {
            best = outcome.index;
            bestError = error;
        }
    }

    return best;
}

void RehearsalEngine::registerHit (int noteIndex, double onsetMs, double confirmedMs,
                                   double centsError, MatchSource source)
{
    auto& outcome = outcomes[(size_t) noteIndex];
    outcome.resolved       = true;
    outcome.hit            = true;
    outcome.firstArrivalMs = onsetMs;
    outcome.confirmedMs    = confirmedMs;
    outcome.timingErrorMs  = onsetMs - dueMs[(size_t) noteIndex];
    outcome.centsError     = centsError;
    outcome.source         = source;

    if (onChanged != nullptr)
        onChanged();

    checkComplete();
}

void RehearsalEngine::advanceTransport (double transportMs)
{
    if (! running || mode != RehearsalMode::inTime)
        return;

    // A sung note is only confirmed after its stability window, so a note must
    // not be written off as missed until a late confirmation could no longer
    // still be attributed to it.
    const auto voiceGrace = voiceEnabled ? settings.stabilityMs + settings.latencyMs : 0.0;
    auto changed = false;

    for (auto& outcome : outcomes)
    {
        if (outcome.resolved)
            continue;

        if (transportMs > dueMs[(size_t) outcome.index] + timing.missMs + voiceGrace)
        {
            outcome.resolved = true;
            outcome.hit      = false;
            changed = true;
        }
    }

    if (changed)
    {
        if (onChanged != nullptr)
            onChanged();

        checkComplete();
    }
}

//==============================================================================
void RehearsalEngine::handleMidiNote (int midiNote, double nowMs)
{
    if (! running || ! midiEnabled)
        return;

    if (mode == RehearsalMode::inTime)
    {
        const auto index = findTimedCandidate (midiNote, nowMs);

        if (index < 0)
            countWrongNote (midiNote);
        else
            registerHit (index, nowMs, nowMs, 0.0, MatchSource::midi);

        return;
    }

    if (getTargetIndex() < 0)
        return;

    // A key press is a fresh event, so nothing has to be released first the way
    // a sustained sung note does.
    awaitingClear = false;

    if (matches (midiNote, notes[(size_t) targetIndex]))
    {
        candidateSinceMs = nowMs;
        acceptStep (MatchSource::midi, nowMs, 0.0);
        return;
    }

    countWrongNote (midiNote);
}

void RehearsalEngine::handleVoicePitch (bool hasPitch, int nearestNote, double cents,
                                        double confidence, double nowMs)
{
    if (! running || ! voiceEnabled)
        return;

    const auto believable = hasPitch && confidence >= settings.minConfidence;

    // Intonation is not being judged when only the rhythm is, so a note sung
    // halfway between two semitones still counts as an attack.
    const auto inTune = ignorePitch || std::abs (cents) <= settings.centsTolerance;

    if (mode == RehearsalMode::inTime)
    {
        if (! believable)
        {
            candidateSinceMs = -1.0;
            return;
        }

        // Hold the onset from when the pitch first appeared, not from when the
        // stability window finished, then take off the detector's own delay.
        if (candidateSinceMs < 0.0)
        {
            candidateSinceMs = nowMs;
            justAcceptedNote = nearestNote;
        }
        else if (nearestNote != justAcceptedNote)
        {
            candidateSinceMs = nowMs;
            justAcceptedNote = nearestNote;
        }

        if (nowMs - candidateSinceMs < (double) settings.stabilityMs)
            return;

        const auto onset = candidateSinceMs - settings.latencyMs;
        const auto index = findTimedCandidate (nearestNote, onset);

        if (index >= 0 && inTune)
        {
            registerHit (index, onset, nowMs, cents, MatchSource::voice);

            // Don't let the same sustained note claim anything else.
            candidateSinceMs = -1.0;
            justAcceptedNote = -1;
        }

        return;
    }

    if (getTargetIndex() < 0)
        return;

    const auto target = notes[(size_t) targetIndex];

    const auto onTarget = believable && matches (nearestNote, target) && inTune;

    if (awaitingClear)
    {
        // The previous note was the same pitch, so wait for the singer to stop
        // before this one can count. Silence or a different pitch both clear it.
        if (! onTarget)
            awaitingClear = false;

        candidateSinceMs = -1.0;
        return;
    }

    if (! onTarget)
    {
        candidateSinceMs = -1.0;
        return;
    }

    if (candidateSinceMs < 0.0)
        candidateSinceMs = nowMs;

    if (nowMs - candidateSinceMs >= (double) settings.stabilityMs)
        acceptStep (MatchSource::voice, nowMs, cents);
}

//==============================================================================
void RehearsalEngine::getTimingSummary (int& hits, int& misses, double& averageErrorMs) const
{
    hits = 0;
    misses = 0;
    double total = 0.0;

    for (const auto& outcome : outcomes)
    {
        if (! outcome.resolved)
            continue;

        if (outcome.hit)
        {
            ++hits;
            total += outcome.timingErrorMs;
        }
        else
        {
            ++misses;
        }
    }

    averageErrorMs = hits > 0 ? total / hits : 0.0;
}

} // namespace practice
