#include "PianoSynth.h"

namespace audio
{

PianoVoice::PianoVoice()
{
    envelope.setParameters (envelopeParameters);
}

bool PianoVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<PianoSound*> (sound) != nullptr;
}

void PianoVoice::startNote (int midiNoteNumber, float velocity,
                            juce::SynthesiserSound*, int)
{
    const auto sampleRate = getSampleRate();

    if (sampleRate <= 0.0)
        return;

    const auto fundamental = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);

    // Low strings ring for many seconds, the top octave for well under one.
    const auto baseDecaySeconds = 12.0 * std::pow (0.5, (midiNoteNumber - 21) / 22.0);

    // Harder playing excites the upper partials more, which is what actually
    // reads as "louder" on a piano rather than just more gain.
    const auto brightness = 0.35 + 0.65 * (double) velocity;

    envelope.setSampleRate (sampleRate);
    envelope.setParameters (envelopeParameters);
    envelope.noteOn();

    for (int p = 0; p < numPartials; ++p)
    {
        const auto harmonic = p + 1;
        const auto stretched = fundamental * harmonic
                                 * std::sqrt (1.0 + inharmonicity * harmonic * harmonic);

        phase[(size_t) p] = 0.0;

        // Anything past Nyquist is silenced rather than allowed to alias back
        // down into the audible range.
        phaseIncrement[(size_t) p] = stretched < sampleRate * 0.48
            ? juce::MathConstants<double>::twoPi * stretched / sampleRate
            : 0.0;

        amplitude[(size_t) p] = phaseIncrement[(size_t) p] > 0.0
            ? std::pow (1.0 / harmonic, 1.3) * std::pow (brightness, harmonic - 1)
            : 0.0;

        const auto partialDecay = baseDecaySeconds / (1.0 + 0.55 * p);
        decayCoefficient[(size_t) p] = std::exp (-1.0 / (partialDecay * sampleRate));
    }

    levelScale = 0.22 * (0.25 + 0.75 * (double) velocity);
}

void PianoVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        envelope.noteOff();
    }
    else
    {
        envelope.reset();
        clearCurrentNote();
    }
}

void PianoVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                  int startSample, int numSamples)
{
    if (! envelope.isActive())
        return;

    const auto numChannels = outputBuffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        double sample = 0.0;

        for (int p = 0; p < numPartials; ++p)
        {
            if (amplitude[(size_t) p] <= 1.0e-6)
                continue;

            sample += std::sin (phase[(size_t) p]) * amplitude[(size_t) p];

            phase[(size_t) p] += phaseIncrement[(size_t) p];

            if (phase[(size_t) p] >= juce::MathConstants<double>::twoPi)
                phase[(size_t) p] -= juce::MathConstants<double>::twoPi;

            amplitude[(size_t) p] *= decayCoefficient[(size_t) p];
        }

        const auto value = (float) (sample * levelScale * envelope.getNextSample());

        for (int channel = 0; channel < numChannels; ++channel)
            outputBuffer.addSample (channel, startSample + i, value);
    }

    if (! envelope.isActive())
    {
        envelope.reset();
        clearCurrentNote();
    }
}

//==============================================================================
PianoSynth::PianoSynth()
{
    synth.addSound (new PianoSound());
}

void PianoSynth::prepare (double sampleRate, int maximumBlockSize, int numVoices)
{
    synth.clearVoices();

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new PianoVoice());

    synth.setCurrentPlaybackSampleRate (sampleRate);
    scratchBuffer.setSize (2, juce::jmax (1, maximumBlockSize), false, true, true);
}

void PianoSynth::renderNextBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    if (numSamples > scratchBuffer.getNumSamples())
        return;   // block bigger than we were prepared for - drop it rather than allocate

    scratchBuffer.clear (0, numSamples);
    synth.renderNextBlock (scratchBuffer, midi, 0, numSamples);

    // Even when muted the voices still have to run, so releases land and notes
    // don't hang when the synth is switched back on.
    if (! enabled.load())
        return;

    const auto currentGain = gain.load();
    const auto channelsToMix = juce::jmin (buffer.getNumChannels(), scratchBuffer.getNumChannels());

    for (int channel = 0; channel < channelsToMix; ++channel)
        buffer.addFrom (channel, 0, scratchBuffer, channel, 0, numSamples, currentGain);
}

void PianoSynth::allNotesOff()
{
    synth.allNotesOff (0, false);
}

} // namespace audio
