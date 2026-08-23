#pragma once

#include <JuceHeader.h>
#include "../Model/Melody.h"
#include <functional>
#include <vector>

namespace audio
{

/** Plays a tune back through the shared keyboard state.

    Going through `MidiKeyboardState` rather than straight to the synth means
    playback drives whatever the rest of the app is driving - the built-in
    piano, a hosted VST3, and the on-screen keys light up too. A message-thread
    timer is plenty accurate for hearing a practice tune; nothing here is
    sample-locked.
*/
class MelodyPlayer : private juce::Timer
{
public:
    explicit MelodyPlayer (juce::MidiKeyboardState& stateToDrive);
    ~MelodyPlayer() override;

    void play (const model::Melody& melody);

    /** Sounds one note, for cueing the note you are about to sing. */
    void playSingleNote (int midiNote, int durationMs = 700);

    /** Sounds a short sequence of chords, then holds the last note on its own.

        This is how a lesson starts a piece of sight-singing: a few chords to
        put the key in your ear, then the note you begin on. Guessing the first
        note out of nowhere tests something other than reading.
    */
    void playChords (const std::vector<std::vector<int>>& chords, int msPerChord = 550);

    void stop();
    bool isPlaying() const noexcept { return playing; }

    std::function<void (int rehearsalIndex)> onNoteStarted;
    std::function<void()> onFinished;

private:
    void timerCallback() override;
    void releaseSoundingNotes();

    struct ScheduledNote
    {
        int    midiNote       = 60;
        double startMs        = 0.0;
        double endMs          = 0.0;
        int    rehearsalIndex = -1;
        bool   started        = false;
        bool   finished       = false;
    };

    juce::MidiKeyboardState& keyboardState;
    std::vector<ScheduledNote> schedule;

    double startedAtMs = 0.0;
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyPlayer)
};

} // namespace audio
