#pragma once

#include <JuceHeader.h>
#include <vector>

namespace analysis
{

/** One snapshot of what the audio input is doing. */
struct AnalysisResult
{
    bool   hasPitch   = false;
    double frequency  = 0.0;    ///< Hz
    double midiNote   = 0.0;    ///< fractional MIDI note number
    double confidence = 0.0;    ///< 0..1, from the YIN aperiodicity measure
    float  levelDb    = -100.0f;

    /** Notes detected sounding together. Approximate - see the chord notes in
        the README. Empty when chord detection is off or nothing is playing. */
    std::vector<int> chordNotes;
};

//==============================================================================
/** Monophonic YIN pitch tracking plus an optional harmonic-salience chord layer.

    Everything is allocated up front in prepare() so process() can run on a
    background thread without touching the allocator.
*/
class PitchDetector
{
public:
    PitchDetector();

    static constexpr int windowSize = 4096;   ///< ~93 ms at 44.1 kHz
    static constexpr int fftOrder   = 13;     ///< 8192, >= 2 * windowSize
    static constexpr int fftSize    = 1 << fftOrder;

    static constexpr double minFrequency = 40.0;    ///< just below low E on a bass
    static constexpr double maxFrequency = 2000.0;

    void prepare (double sampleRate);

    /** Analyses exactly windowSize samples. */
    void process (const float* samples, AnalysisResult& result);

    void setChordDetectionEnabled (bool shouldBeEnabled) noexcept { chordEnabled = shouldBeEnabled; }
    void setLevelGateDb (float db) noexcept                       { levelGateDb = db; }

private:
    double detectPitchYin (const float* samples, double& confidenceOut);
    void   detectChord (const float* samples, std::vector<int>& notesOut);
    float  magnitudeAt (double frequency) const;

    double currentSampleRate = 44100.0;
    // Off unless asked for. Everything this app is actually for - reading a
    // line, singing it back, checking one note against one target - is
    // monophonic, and the chord layer is a heuristic that can report two notes
    // where a rich single note was played. The honest reading is the default.
    bool   chordEnabled = false;
    float  levelGateDb = -45.0f;

    juce::dsp::FFT fft { fftOrder };

    std::vector<float>  fftBuffer;        ///< 2 * fftSize, reused by both stages
    std::vector<float>  magnitudes;       ///< fftSize / 2 + 1
    std::vector<float>  window;           ///< Hann, windowSize
    std::vector<double> prefixSquares;    ///< windowSize + 1
    std::vector<double> difference;       ///< windowSize / 2
    std::vector<double> cumulativeMeanDifference;
    std::vector<double> salience;         ///< one entry per MIDI note in range

    static constexpr int lowestChordNote  = 33;   ///< A1
    static constexpr int highestChordNote = 96;   ///< C7
    static constexpr int maxChordNotes    = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetector)
};

} // namespace analysis
