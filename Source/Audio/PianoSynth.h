#pragma once

#include <JuceHeader.h>

namespace audio
{

/** Accepts every note on every channel - there is only one instrument here. */
struct PianoSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
/** An additive piano-ish voice.

    A real piano string is not a perfect harmonic series: its partials are
    stretched slightly sharp, the upper ones die away much faster than the
    fundamental, and the whole note rings longer the lower you play. Modelling
    those three things is most of what makes an additive tone read as "piano"
    rather than "organ", and it needs no sample data at all.
*/
class PianoVoice : public juce::SynthesiserVoice
{
public:
    PianoVoice();

    bool canPlaySound (juce::SynthesiserSound* sound) override;

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override;

private:
    static constexpr int numPartials = 10;

    /** Stretch factor of the partial series. Real strings are stiff, so the
        nth partial sits a little above n times the fundamental. */
    static constexpr double inharmonicity = 0.0004;

    std::array<double, numPartials> phase {};
    std::array<double, numPartials> phaseIncrement {};
    std::array<double, numPartials> amplitude {};
    std::array<double, numPartials> decayCoefficient {};

    juce::ADSR envelope;
    juce::ADSR::Parameters envelopeParameters { 0.004f, 0.0f, 1.0f, 0.28f };

    double levelScale = 0.0;
};

//==============================================================================
/** The built-in instrument: a small pool of piano voices with an output trim. */
class PianoSynth
{
public:
    PianoSynth();

    void prepare (double sampleRate, int maximumBlockSize, int numVoices = 16);
    /** Renders the whole buffer. MIDI timestamps are relative to sample 0. */
    void renderNextBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    void allNotesOff();

    void setGain (float newGain) noexcept  { gain = juce::jlimit (0.0f, 2.0f, newGain); }
    float getGain() const noexcept         { return gain; }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
    bool isEnabled() const noexcept                 { return enabled; }

private:
    juce::Synthesiser synth;
    juce::AudioBuffer<float> scratchBuffer;   ///< preallocated - never resized on the audio thread
    std::atomic<float> gain { 0.7f };
    std::atomic<bool> enabled { true };
};

} // namespace audio
