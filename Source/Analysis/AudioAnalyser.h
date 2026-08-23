#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"

namespace analysis
{

/** What the UI reads: a detection that has already been smoothed and gated. */
struct AnalysisSnapshot
{
    bool   hasPitch    = false;
    double frequency   = 0.0;
    double midiNote    = 0.0;   ///< fractional
    int    nearestNote = -1;    ///< -1 when nothing is detected
    double cents       = 0.0;   ///< deviation from nearestNote, -50..+50
    double confidence  = 0.0;
    float  levelDb     = -100.0f;
    std::vector<int> chordNotes;
};

//==============================================================================
/** Bridges the audio thread and the pitch detector.

    The audio callback only ever writes into a ring buffer, so it never blocks
    and never allocates. A background thread pulls the most recent window out
    of that buffer and runs the (comparatively expensive) detection, then
    publishes a smoothed snapshot for the UI to pick up on its timer.
*/
class AudioAnalyser : private juce::Thread
{
public:
    AudioAnalyser();
    ~AudioAnalyser() override;

    void prepare (double sampleRate);
    void reset();

    /** Called from the audio thread. Sums the input channels to mono. */
    void pushAudio (const float* const* inputChannels, int numChannels, int numSamples) noexcept;

    /** Called from the message thread. */
    AnalysisSnapshot getSnapshot() const;

    void setChordDetectionEnabled (bool shouldBeEnabled) { detector.setChordDetectionEnabled (shouldBeEnabled); }
    void setLevelGateDb (float db)                       { detector.setLevelGateDb (db); }

private:
    void run() override;
    void publish (const AnalysisResult& raw);

    static constexpr int ringSize     = 1 << 16;   ///< ~1.5 s at 44.1 kHz
    static constexpr int ringMask     = ringSize - 1;
    static constexpr int analysisRate = 25;        ///< ms between detections
    static constexpr int holdFrames   = 8;         ///< keep showing a note this long after it stops

    PitchDetector detector;

    std::vector<float> ring;
    std::atomic<int64_t> writePosition { 0 };

    std::vector<float> analysisWindow;

    // Smoothing state, only ever touched by the analysis thread.
    std::array<double, 3> recentNotes { { 0.0, 0.0, 0.0 } };
    int  numRecentNotes = 0;
    int  framesSincePitch = 0;
    int  stableNote = -1;

    juce::CriticalSection snapshotLock;
    AnalysisSnapshot snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyser)
};

} // namespace analysis
