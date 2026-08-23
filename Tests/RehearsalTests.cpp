/*  Tests for the rehearsal engine.

    Two things here are easy to get subtly wrong and impossible to see by
    looking at the app: which note an attack gets attributed to when notes are
    close together, and whether a sung note is graded against when it was sung
    rather than when the detector finally admitted to it.

    Build target: RehearsalTests. Returns non-zero if anything fails.
*/

#include <JuceHeader.h>
#include "../Source/Practice/RehearsalEngine.h"

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

    void checkClose (double actual, double expected, double tolerance, const juce::String& what)
    {
        check (std::abs (actual - expected) <= tolerance,
               what + " (expected " + juce::String (expected, 1)
                    + ", got " + juce::String (actual, 1) + ")");
    }

    /** Feeds a steady pitch to the engine, one analysis frame every 25 ms, the
        way the real UI timer does. */
    void feedVoice (practice::RehearsalEngine& engine, int note, double fromMs, double toMs,
                    double confidence = 0.9, double cents = 0.0)
    {
        for (auto t = fromMs; t <= toMs; t += 25.0)
            engine.handleVoicePitch (true, note, cents, confidence, t);
    }
}

//==============================================================================
int main()
{
    std::cout << "RehearsalTests" << std::endl;

    // -- step mode: right note advances, wrong note does not ------------------
    {
        practice::RehearsalEngine engine;
        engine.setNotes ({ 60, 62, 64 });
        engine.start();

        checkEqual (engine.getTargetIndex(), 0, "starts on the first note");

        engine.handleMidiNote (61, 0.0);
        checkEqual (engine.getTargetIndex(), 0, "a wrong note does not advance");
        checkEqual (engine.getTotalWrongNotes(), 1, "the wrong note is counted");

        engine.handleMidiNote (60, 10.0);
        checkEqual (engine.getTargetIndex(), 1, "the right note advances");

        engine.handleMidiNote (62, 20.0);
        engine.handleMidiNote (64, 30.0);
        check (engine.isComplete(), "the tune finishes");
        checkEqual (engine.getCompletedCount(), 3, "all three counted");
    }

    // -- octave agnostic matching --------------------------------------------
    {
        practice::RehearsalEngine engine;
        engine.setNotes ({ 60 });
        engine.start();

        engine.handleMidiNote (72, 0.0);
        checkEqual (engine.getCompletedCount(), 1, "an octave up counts by default");

        practice::RehearsalEngine strict;
        auto settings = strict.getVoiceSettings();
        settings.octaveAgnostic = false;
        strict.setVoiceSettings (settings);
        strict.setNotes ({ 60 });
        strict.start();

        strict.handleMidiNote (72, 0.0);
        checkEqual (strict.getCompletedCount(), 0, "with strict octaves it does not");
    }

    // -- step mode: a sung note must be held before it counts ------------------
    {
        practice::RehearsalEngine engine;
        engine.setNotes ({ 60, 62 });
        engine.start();

        feedVoice (engine, 60, 0.0, 100.0);
        checkEqual (engine.getTargetIndex(), 0, "a brief pitch is not enough");

        feedVoice (engine, 60, 125.0, 300.0);
        checkEqual (engine.getTargetIndex(), 1, "holding it past the stability window counts");
    }

    // -- step mode: passing through a note on the way does not trigger it ------
    {
        practice::RehearsalEngine engine;
        engine.setNotes ({ 64 });
        engine.start();

        // Slide up through D, briefly, then settle on E.
        feedVoice (engine, 62, 0.0, 50.0);
        feedVoice (engine, 64, 75.0, 400.0);

        checkEqual (engine.getCompletedCount(), 1, "the note landed on counts");
        checkEqual (engine.getTotalWrongNotes(), 0, "passing through costs nothing");
    }

    // -- step mode: the same note twice needs a fresh attack -------------------
    {
        practice::RehearsalEngine engine;
        engine.setNotes ({ 60, 60 });
        engine.start();

        feedVoice (engine, 60, 0.0, 400.0);
        checkEqual (engine.getCompletedCount(), 1,
                    "one sustained note cannot satisfy two identical targets");

        engine.handleVoicePitch (false, 0, 0.0, 0.0, 425.0);   // silence
        feedVoice (engine, 60, 450.0, 800.0);
        checkEqual (engine.getCompletedCount(), 2, "after a gap the second one counts");
    }

    // -- in time: a note played on the beat is a hit --------------------------
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);
        engine.setTimedNotes ({ 60, 62 }, { 1000.0, 2000.0 });
        engine.start();

        engine.handleMidiNote (60, 1020.0);

        const auto& outcomes = engine.getOutcomes();
        check (outcomes[0].hit, "played on the beat is a hit");
        checkClose (outcomes[0].timingErrorMs, 20.0, 1.0, "and is graded 20 ms late");
    }

    // -- in time: an early attack is claimed by the note it was aiming at ------
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);
        engine.setTimedNotes ({ 60, 60 }, { 1000.0, 2000.0 });
        engine.start();

        // 80 ms before the second note: much nearer to it than to the first.
        engine.handleMidiNote (60, 1920.0);

        const auto& outcomes = engine.getOutcomes();
        check (! outcomes[0].resolved, "the earlier note is left alone");
        check (outcomes[1].hit, "the nearer note takes the attack");
        checkClose (outcomes[1].timingErrorMs, -80.0, 1.0, "graded 80 ms early");
    }

    // -- in time: a note nobody plays is eventually missed ---------------------
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);
        engine.setVoiceEnabled (false);        // no voice grace to wait out
        engine.setTimedNotes ({ 60 }, { 1000.0 });
        engine.start();

        engine.advanceTransport (1100.0);
        check (! engine.getOutcomes()[0].resolved, "still claimable just after the beat");

        engine.advanceTransport (2000.0);
        check (engine.getOutcomes()[0].resolved, "eventually written off");
        check (! engine.getOutcomes()[0].hit, "and recorded as a miss");
    }

    // -- in time: a wrong pitch is counted, not attributed ---------------------
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);
        engine.setTimedNotes ({ 60 }, { 1000.0 });
        engine.start();

        engine.handleMidiNote (61, 1000.0);
        checkEqual (engine.getTotalWrongNotes(), 1, "wrong pitch counted");
        check (! engine.getOutcomes()[0].resolved, "and claims nothing");
    }

    // -- in time: singing is graded from the onset, not the confirmation -------
    //
    // This is the whole reason first-arrival is recorded separately. The
    // detector only confirms after the stability window, so grading on the
    // confirmation would mark a perfectly timed note hundreds of ms late.
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);

        auto settings = engine.getVoiceSettings();
        settings.stabilityMs = 200;
        settings.latencyMs   = 100.0;
        engine.setVoiceSettings (settings);

        // Due exactly when the detector first sees the pitch, minus its latency.
        engine.setTimedNotes ({ 60 }, { 900.0 });
        engine.start();

        feedVoice (engine, 60, 1000.0, 1300.0);

        const auto& outcomes = engine.getOutcomes();
        check (outcomes[0].hit, "the sung note is a hit");
        checkClose (outcomes[0].timingErrorMs, 0.0, 30.0,
                    "graded from the onset, so dead on rather than ~300 ms late");
        check (outcomes[0].confirmedMs > outcomes[0].firstArrivalMs,
               "confirmation is later than the onset, as it must be");
    }

    // -- summary adds up -------------------------------------------------------
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);
        engine.setVoiceEnabled (false);
        engine.setTimedNotes ({ 60, 62, 64 }, { 1000.0, 2000.0, 3000.0 });
        engine.start();

        engine.handleMidiNote (60, 1050.0);
        engine.handleMidiNote (62, 2050.0);
        engine.advanceTransport (4000.0);

        int hits = 0, misses = 0;
        double average = 0.0;
        engine.getTimingSummary (hits, misses, average);

        checkEqual (hits, 2, "two hits");
        checkEqual (misses, 1, "one miss");
        checkClose (average, 50.0, 1.0, "average error is 50 ms late");
    }

    // -- rhythm only: any pitch counts, but the rhythm still has to be right ---
    //
    // Rhythm before notes is the oldest piece of sight-reading teaching there
    // is, and it has to be a real mode rather than a lax one: the cursor still
    // advances one note at a time, a held note still cannot claim two notes,
    // and in time the timing windows still apply.
    {
        practice::RehearsalEngine engine;
        engine.setIgnorePitch (true);
        engine.setNotes ({ 60, 62, 64 });
        engine.start();

        engine.handleMidiNote (48, 0.0);
        checkEqual (engine.getTargetIndex(), 1, "any pitch advances");
        checkEqual (engine.getTotalWrongNotes(), 0, "and nothing is wrong");

        engine.handleMidiNote (71, 10.0);
        engine.handleMidiNote (35, 20.0);
        check (engine.isComplete(), "three taps finish a three-note tune");
    }

    // -- rhythm only: one held note is still one note --------------------------
    {
        practice::RehearsalEngine engine;
        engine.setIgnorePitch (true);
        engine.setNotes ({ 60, 62 });
        engine.start();

        feedVoice (engine, 55, 0.0, 900.0);
        checkEqual (engine.getCompletedCount(), 1,
                    "holding one note through two does not claim both");

        feedVoice (engine, 55, 900.0, 1000.0, 0.0);      // silence
        feedVoice (engine, 55, 1000.0, 1400.0);
        checkEqual (engine.getCompletedCount(), 2, "a fresh attack takes the second");
    }

    // -- rhythm only: intonation stops being judged ----------------------------
    {
        practice::RehearsalEngine engine;
        auto settings = engine.getVoiceSettings();
        settings.centsTolerance = 10.0;
        engine.setVoiceSettings (settings);
        engine.setNotes ({ 60 });
        engine.start();

        feedVoice (engine, 60, 0.0, 400.0, 0.9, 45.0);
        checkEqual (engine.getCompletedCount(), 0, "45 cents off fails a tight tolerance");

        practice::RehearsalEngine loose;
        loose.setVoiceSettings (settings);
        loose.setIgnorePitch (true);
        loose.setNotes ({ 60 });
        loose.start();

        feedVoice (loose, 60, 0.0, 400.0, 0.9, 45.0);
        checkEqual (loose.getCompletedCount(), 1, "but not when only rhythm is being read");
    }

    // -- rhythm only, in time: the timing windows still apply ------------------
    {
        practice::RehearsalEngine engine;
        engine.setMode (practice::RehearsalMode::inTime);
        engine.setVoiceEnabled (false);
        engine.setIgnorePitch (true);
        engine.setTimedNotes ({ 60, 62 }, { 1000.0, 2000.0 });
        engine.start();

        engine.handleMidiNote (36, 1040.0);      // nowhere near the pitch, on the beat
        engine.advanceTransport (3000.0);

        int hits = 0, misses = 0;
        double average = 0.0;
        engine.getTimingSummary (hits, misses, average);

        checkEqual (hits, 1, "a tap on the beat is a hit whatever it was");
        checkEqual (misses, 1, "and a beat nobody played is still missed");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
