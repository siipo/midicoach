#include "Metronome.h"

namespace audio
{

namespace
{
    constexpr double accentFrequency = 1600.0;
    constexpr double normalFrequency = 1050.0;
    constexpr double clickSeconds    = 0.035;
}

void Metronome::prepare (double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    clickDecay = std::exp (-1.0 / (clickSeconds * currentSampleRate));
    resetPending.store (true);
}

void Metronome::setTempo (double beatsPerMinute) noexcept
{
    tempo.store (juce::jlimit (20.0, 300.0, beatsPerMinute));
}

void Metronome::setBeat (int beatsPerBar, double beatLengthInQuarters) noexcept
{
    barBeats.store (juce::jmax (1, beatsPerBar));
    beatQuarters.store (juce::jmax (0.25, beatLengthInQuarters));
}

void Metronome::start() noexcept
{
    resetPending.store (true);
    running.store (true);
}

void Metronome::stop() noexcept
{
    running.store (false);
}

double Metronome::getTransportMs() const noexcept
{
    return (double) samplePosition.load() * 1000.0 / currentSampleRate;
}

void Metronome::triggerClick (bool accented)
{
    clickPhase     = 0.0;
    clickEnvelope  = 1.0;
    clickIncrement = juce::MathConstants<double>::twoPi
                       * (accented ? accentFrequency : normalFrequency) / currentSampleRate;
}

//==============================================================================
void Metronome::process (juce::AudioBuffer<float>& buffer)
{
    if (resetPending.exchange (false))
    {
        samplePosition.store (0);
        beatCount.store (0);
        samplesToNextBeat = 0.0;
        beatInBar = 0;
        clickEnvelope = 0.0;
    }

    if (! running.load())
        return;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    const auto samplesPerBeat = juce::jmax (1.0,
        currentSampleRate * 60.0 / tempo.load() * beatQuarters.load());

    const auto beatsPerBar = barBeats.load();
    const auto shouldSound = audible.load();
    const auto currentGain = (double) gain.load();

    auto position = samplePosition.load();

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesToNextBeat <= 0.0)
        {
            triggerClick (beatInBar == 0);

            samplesToNextBeat += samplesPerBeat;
            beatInBar = (beatInBar + 1) % beatsPerBar;
            beatCount.fetch_add (1);
        }

        if (shouldSound && clickEnvelope > 1.0e-4)
        {
            const auto value = (float) (std::sin (clickPhase) * clickEnvelope * currentGain);

            for (int channel = 0; channel < numChannels; ++channel)
                buffer.addSample (channel, i, value);
        }

        clickPhase += clickIncrement;

        if (clickPhase >= juce::MathConstants<double>::twoPi)
            clickPhase -= juce::MathConstants<double>::twoPi;

        clickEnvelope *= clickDecay;
        samplesToNextBeat -= 1.0;
    }

    samplePosition.store (position + numSamples);
}

} // namespace audio
