#include "PitchDetector.h"
#include "../Theory/MusicTheory.h"
#include <cmath>

namespace analysis
{

PitchDetector::PitchDetector()
{
    fftBuffer.resize ((size_t) fftSize * 2);
    magnitudes.resize ((size_t) fftSize / 2 + 1);
    window.resize ((size_t) windowSize);
    prefixSquares.resize ((size_t) windowSize + 1);
    difference.resize ((size_t) windowSize / 2);
    cumulativeMeanDifference.resize ((size_t) windowSize / 2);
    salience.resize ((size_t) (highestChordNote - lowestChordNote + 1));

    for (int i = 0; i < windowSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) i / (float) (windowSize - 1));
}

void PitchDetector::prepare (double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

//==============================================================================
void PitchDetector::process (const float* samples, AnalysisResult& result)
{
    result = {};

    double sumSquares = 0.0;

    for (int i = 0; i < windowSize; ++i)
        sumSquares += (double) samples[i] * samples[i];

    const auto rms = std::sqrt (sumSquares / windowSize);
    result.levelDb = (float) juce::Decibels::gainToDecibels (rms, -100.0);

    if (result.levelDb < levelGateDb)
        return;

    double confidence = 0.0;
    const auto frequency = detectPitchYin (samples, confidence);

    if (frequency > 0.0)
    {
        result.hasPitch   = true;
        result.frequency  = frequency;
        result.midiNote   = theory::frequencyToMidiNote (frequency);
        result.confidence = confidence;
    }

    if (chordEnabled)
        detectChord (samples, result.chordNotes);
}

//==============================================================================
/** YIN (de Cheveigne and Kawahara, 2002), with the squared-difference function
    evaluated through the FFT so the cost is O(N log N) rather than O(N^2).
*/
double PitchDetector::detectPitchYin (const float* samples, double& confidenceOut)
{
    const auto maxTau = windowSize / 2;

    // Step 1: linear autocorrelation via the power spectrum of the zero-padded frame.
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);

    for (int i = 0; i < windowSize; ++i)
        fftBuffer[(size_t) i] = samples[i];

    fft.performRealOnlyForwardTransform (fftBuffer.data(), false);

    for (int bin = 0; bin < fftSize; ++bin)
    {
        const auto re = fftBuffer[(size_t) bin * 2];
        const auto im = fftBuffer[(size_t) bin * 2 + 1];

        fftBuffer[(size_t) bin * 2]     = re * re + im * im;
        fftBuffer[(size_t) bin * 2 + 1] = 0.0f;
    }

    fft.performRealOnlyInverseTransform (fftBuffer.data());

    prefixSquares[0] = 0.0;

    for (int i = 0; i < windowSize; ++i)
        prefixSquares[(size_t) i + 1] = prefixSquares[(size_t) i]
                                          + (double) samples[i] * samples[i];

    const auto energy = prefixSquares[(size_t) windowSize];

    if (energy <= 0.0 || fftBuffer[0] <= 0.0f)
        return 0.0;

    // Rescale so r(0) matches the true energy - that makes the result
    // independent of whatever normalisation the FFT applied.
    const auto scale = energy / (double) fftBuffer[0];

    // Step 2: the difference function, expanded so it only needs r(tau) and
    // prefix sums of x^2.
    for (int tau = 0; tau < maxTau; ++tau)
    {
        const auto r = (double) fftBuffer[(size_t) tau] * scale;

        difference[(size_t) tau] = prefixSquares[(size_t) (windowSize - tau)]
                                     + (energy - prefixSquares[(size_t) tau])
                                     - 2.0 * r;
    }

    // Step 3: cumulative mean normalised difference.
    cumulativeMeanDifference[0] = 1.0;
    double runningSum = 0.0;

    for (int tau = 1; tau < maxTau; ++tau)
    {
        runningSum += difference[(size_t) tau];

        cumulativeMeanDifference[(size_t) tau] = runningSum > 0.0
            ? difference[(size_t) tau] * (double) tau / runningSum
            : 1.0;
    }

    // Step 4: absolute threshold, searched only over the tau range that maps
    // onto a plausible musical pitch.
    const auto minTau   = juce::jmax (2, (int) std::floor (currentSampleRate / maxFrequency));
    const auto tauLimit = juce::jmin (maxTau - 1, (int) std::ceil (currentSampleRate / minFrequency));

    if (minTau >= tauLimit)
        return 0.0;

    constexpr double threshold = 0.15;
    int bestTau = -1;

    for (int tau = minTau; tau <= tauLimit; ++tau)
    {
        if (cumulativeMeanDifference[(size_t) tau] < threshold)
        {
            // Walk to the bottom of this dip rather than stopping on its edge.
            while (tau + 1 <= tauLimit
                    && cumulativeMeanDifference[(size_t) (tau + 1)] < cumulativeMeanDifference[(size_t) tau])
                ++tau;

            bestTau = tau;
            break;
        }
    }

    if (bestTau < 0)
    {
        // Nothing crossed the threshold - fall back to the global minimum, but
        // report the low confidence that comes with it.
        auto lowest = cumulativeMeanDifference[(size_t) minTau];
        bestTau = minTau;

        for (int tau = minTau + 1; tau <= tauLimit; ++tau)
        {
            if (cumulativeMeanDifference[(size_t) tau] < lowest)
            {
                lowest  = cumulativeMeanDifference[(size_t) tau];
                bestTau = tau;
            }
        }

        if (lowest > 0.6)
            return 0.0;
    }

    // Step 5: parabolic interpolation around the chosen dip for sub-sample
    // period resolution - this is what makes the cents readout usable.
    auto refinedTau = (double) bestTau;

    if (bestTau > 0 && bestTau < maxTau - 1)
    {
        const auto a = cumulativeMeanDifference[(size_t) (bestTau - 1)];
        const auto b = cumulativeMeanDifference[(size_t) bestTau];
        const auto c = cumulativeMeanDifference[(size_t) (bestTau + 1)];
        const auto denominator = 2.0 * (2.0 * b - a - c);

        if (std::abs (denominator) > 1.0e-12)
            refinedTau += (c - a) / denominator;
    }

    confidenceOut = juce::jlimit (0.0, 1.0, 1.0 - cumulativeMeanDifference[(size_t) bestTau]);

    return refinedTau > 0.0 ? currentSampleRate / refinedTau : 0.0;
}

//==============================================================================
float PitchDetector::magnitudeAt (double frequency) const
{
    const auto bin = frequency * fftSize / currentSampleRate;

    if (bin < 0.0 || bin >= (double) (magnitudes.size() - 1))
        return 0.0f;

    const auto lower = (int) bin;
    const auto frac  = (float) (bin - lower);

    return magnitudes[(size_t) lower] * (1.0f - frac)
             + magnitudes[(size_t) lower + 1] * frac;
}

/** Harmonic-sum salience with greedy cancellation.

    For every candidate note we add up the spectrum at its first few harmonics;
    the strongest candidates win, and once a note is accepted anything sitting
    on one of its harmonics is suppressed so a single rich note doesn't get
    reported as a chord. This is a heuristic - it handles clean triads well and
    gets less reliable as the voicing gets denser.
*/
void PitchDetector::detectChord (const float* samples, std::vector<int>& notesOut)
{
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);

    for (int i = 0; i < windowSize; ++i)
        fftBuffer[(size_t) i] = samples[i] * window[(size_t) i];

    fft.performRealOnlyForwardTransform (fftBuffer.data(), true);

    for (size_t bin = 0; bin < magnitudes.size(); ++bin)
    {
        const auto re = fftBuffer[bin * 2];
        const auto im = fftBuffer[bin * 2 + 1];

        // The fourth root compresses the dynamic range so quiet upper voices
        // still register next to a loud bass note.
        magnitudes[bin] = std::sqrt (std::sqrt (re * re + im * im));
    }

    constexpr int numHarmonics = 8;
    double strongest = 0.0;

    for (int note = lowestChordNote; note <= highestChordNote; ++note)
    {
        const auto fundamental = theory::midiNoteToFrequency (note);
        double sum = 0.0;

        for (int h = 1; h <= numHarmonics; ++h)
        {
            const auto harmonicFreq = fundamental * h;

            if (harmonicFreq >= currentSampleRate * 0.5)
                break;

            sum += magnitudeAt (harmonicFreq) * std::pow (0.8, h - 1);
        }

        salience[(size_t) (note - lowestChordNote)] = sum;
        strongest = juce::jmax (strongest, sum);
    }

    if (strongest <= 0.0)
        return;

    // Keep only local maxima, so a single pitch doesn't produce a cluster of
    // neighbouring semitones.
    std::vector<std::pair<double, int>> candidates;

    for (int note = lowestChordNote + 1; note < highestChordNote; ++note)
    {
        const auto index = (size_t) (note - lowestChordNote);

        if (salience[index] > salience[index - 1] && salience[index] >= salience[index + 1]
             && salience[index] > 0.3 * strongest)
            candidates.emplace_back (salience[index], note);
    }

    std::sort (candidates.begin(), candidates.end(),
               [] (const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& candidate : candidates)
    {
        if ((int) notesOut.size() >= maxChordNotes)
            break;

        const auto note = candidate.second;
        bool isHarmonicOfAccepted = false;

        for (auto accepted : notesOut)
            for (int h = 2; h <= 6; ++h)
                if (std::abs ((double) note - (accepted + 12.0 * std::log2 ((double) h))) < 0.6)
                    isHarmonicOfAccepted = true;

        if (! isHarmonicOfAccepted)
            notesOut.push_back (note);
    }

    std::sort (notesOut.begin(), notesOut.end());
}

} // namespace analysis
