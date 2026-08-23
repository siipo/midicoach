#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace audio
{

/** The click, and the transport clock everything in time mode runs on.

    The click is generated in the audio callback rather than scheduled from a
    timer, so it lands on the sample it is supposed to. That matters twice over:
    an audibly wobbly click makes a tune impossible to play to, and the same
    sample counter is what timed rehearsal grades against - deriving the
    timeline from the audio clock rather than a message-thread stopwatch keeps
    the click and the grading from drifting apart.
*/
class Metronome
{
public:
    Metronome() = default;

    void prepare (double sampleRate);

    void setTempo (double beatsPerMinute) noexcept;

    /** `beatLengthInQuarters` is 1.0 for the simple metres and 1.5 for the
        compound ones, where the felt beat is a dotted quarter. */
    void setBeat (int beatsPerBar, double beatLengthInQuarters) noexcept;

    void setClickAudible (bool shouldBeAudible) noexcept { audible.store (shouldBeAudible); }
    bool isClickAudible() const noexcept                 { return audible.load(); }

    void setGain (float newGain) noexcept { gain.store (juce::jlimit (0.0f, 1.5f, newGain)); }
    float getGain() const noexcept        { return gain.load(); }

    /** Starts from zero. Safe to call from the message thread. */
    void start() noexcept;
    void stop() noexcept;
    bool isRunning() const noexcept { return running.load(); }

    /** Audio thread: mixes the click in and advances the transport. */
    void process (juce::AudioBuffer<float>& buffer);

    /** Milliseconds since start, taken from the audio clock. */
    double getTransportMs() const noexcept;

    /** Beats elapsed since start, for a visual flash. */
    int getBeatCount() const noexcept { return beatCount.load(); }

private:
    double currentSampleRate = 44100.0;

    std::atomic<double> tempo { 90.0 };
    std::atomic<double> beatQuarters { 1.0 };
    std::atomic<int>    barBeats { 4 };
    std::atomic<bool>   audible { true };
    std::atomic<bool>   running { false };
    std::atomic<bool>   resetPending { false };
    std::atomic<float>  gain { 0.5f };

    std::atomic<int64_t> samplePosition { 0 };
    std::atomic<int>     beatCount { 0 };

    // Audio-thread only.
    double samplesToNextBeat = 0.0;
    int    beatInBar = 0;
    double clickPhase = 0.0;
    double clickIncrement = 0.0;
    double clickEnvelope = 0.0;
    double clickDecay = 0.0;

    void triggerClick (bool accented);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Metronome)
};

} // namespace audio
