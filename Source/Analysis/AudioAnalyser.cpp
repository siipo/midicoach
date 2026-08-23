#include "AudioAnalyser.h"
#include "../Theory/MusicTheory.h"

namespace analysis
{

AudioAnalyser::AudioAnalyser()
    : juce::Thread ("MidiCoach Pitch Analysis")
{
    ring.assign ((size_t) ringSize, 0.0f);
    analysisWindow.assign ((size_t) PitchDetector::windowSize, 0.0f);

    startThread (juce::Thread::Priority::normal);
}

AudioAnalyser::~AudioAnalyser()
{
    stopThread (2000);
}

void AudioAnalyser::prepare (double sampleRate)
{
    detector.prepare (sampleRate);
    reset();
}

void AudioAnalyser::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePosition.store (0, std::memory_order_release);

    const juce::ScopedLock sl (snapshotLock);
    snapshot = {};
}

//==============================================================================
void AudioAnalyser::pushAudio (const float* const* inputChannels, int numChannels, int numSamples) noexcept
{
    if (inputChannels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    auto position = writePosition.load (std::memory_order_relaxed);
    const auto gain = 1.0f / (float) numChannels;

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
            if (inputChannels[channel] != nullptr)
                sum += inputChannels[channel][i];

        ring[(size_t) ((position + i) & ringMask)] = sum * gain;
    }

    writePosition.store (position + numSamples, std::memory_order_release);
}

AnalysisSnapshot AudioAnalyser::getSnapshot() const
{
    const juce::ScopedLock sl (snapshotLock);
    return snapshot;
}

//==============================================================================
void AudioAnalyser::run()
{
    AnalysisResult raw;
    int64_t lastAnalysed = -1;

    while (! threadShouldExit())
    {
        wait (analysisRate);

        if (threadShouldExit())
            break;

        const auto position = writePosition.load (std::memory_order_acquire);

        if (position < PitchDetector::windowSize || position == lastAnalysed)
            continue;

        lastAnalysed = position;

        // Copy the most recent window out of the ring. The writer is more than
        // a second of audio behind catching up with us here, so this needs no
        // lock.
        const auto start = position - PitchDetector::windowSize;

        for (int i = 0; i < PitchDetector::windowSize; ++i)
            analysisWindow[(size_t) i] = ring[(size_t) ((start + i) & ringMask)];

        detector.process (analysisWindow.data(), raw);
        publish (raw);
    }
}

/** Turns a stream of raw per-frame detections into something stable enough to
    read: a median over the last three frames to reject octave-jump outliers,
    a confidence gate, and a short hold so the display doesn't blink out
    between note attacks.
*/
void AudioAnalyser::publish (const AnalysisResult& raw)
{
    AnalysisSnapshot next;
    next.levelDb    = raw.levelDb;
    next.confidence = raw.confidence;
    next.chordNotes = raw.chordNotes;

    const auto accepted = raw.hasPitch && raw.confidence > 0.35;

    if (accepted)
    {
        recentNotes[2] = recentNotes[1];
        recentNotes[1] = recentNotes[0];
        recentNotes[0] = raw.midiNote;
        numRecentNotes = juce::jmin (3, numRecentNotes + 1);
        framesSincePitch = 0;
    }
    else
    {
        ++framesSincePitch;

        if (framesSincePitch > holdFrames)
        {
            numRecentNotes = 0;
            stableNote = -1;
        }
    }

    if (numRecentNotes > 0)
    {
        double median;

        if (numRecentNotes >= 3)
        {
            auto sorted = recentNotes;
            std::sort (sorted.begin(), sorted.end());
            median = sorted[1];
        }
        else
        {
            median = recentNotes[0];
        }

        int nearest = 0;
        double cents = 0.0;
        theory::splitMidiNote (median, nearest, cents);

        // Hysteresis: once a note is showing, hold onto it until the pitch has
        // moved a comfortable distance past the halfway point.
        if (stableNote >= 0 && std::abs (median - stableNote) < 0.65)
            nearest = stableNote;

        stableNote = nearest;

        next.hasPitch    = true;
        next.midiNote    = median;
        next.nearestNote = nearest;
        next.cents       = (median - nearest) * 100.0;
        next.frequency   = theory::midiNoteToFrequency (median);
    }

    const juce::ScopedLock sl (snapshotLock);
    snapshot = std::move (next);
}

} // namespace analysis
