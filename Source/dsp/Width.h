#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace silo
{

// Stereo width with a mono-safe low end.
//
// The signal is split by a Linkwitz-Riley crossover. Lows are summed to mono;
// the highs go through mid/side, where the side is scaled by `width`
// (0 = mono, 1 = unchanged, 2 = twice as wide). A slow loudness follower trims
// the highs so widening or narrowing doesn't change perceived level.
//
// At width 1 the module is a true bypass (dry signal, bit-exact). Moving away
// from 1 crossfades into the processed path over ~20 ms, because the crossover
// shifts phase and a hard switch would click.
class Width
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };

        lowpass.prepare (spec);
        highpass.prepare (spec);
        lowpass.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        highpass.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        crossover.reset (sampleRate, 0.05);
        crossover.setCurrentAndTargetValue (crossoverHz);
        applyCutoff (crossoverHz);

        width.reset (sampleRate, 0.05);
        width.setCurrentAndTargetValue (1.0f);
        active.reset (sampleRate, 0.02);
        active.setCurrentAndTargetValue (0.0f);
        makeup.reset (sampleRate, 0.1);
        makeup.setCurrentAndTargetValue (1.0f);

        // ~300 ms energy followers for gain matching
        followerCoeff = (float) std::exp (-1.0 / (0.3 * sampleRate));
        midEnergy = sideEnergy = 0.0f;
    }

    void reset()
    {
        lowpass.reset();
        highpass.reset();
        midEnergy = sideEnergy = 0.0f;
    }

    void setWidth (float newWidth)
    {
        newWidth = juce::jlimit (0.0f, 2.0f, newWidth);
        width.setTargetValue (newWidth);
        active.setTargetValue (std::abs (newWidth - 1.0f) > 1.0e-4f ? 1.0f : 0.0f);
    }

    // Glides to the new frequency over ~50 ms; jumping the filter coefficients
    // while audio runs through them would click.
    void setCrossover (float hz)
    {
        crossoverHz = hz;
        crossover.setTargetValue (hz);
    }

    // True when the module is fully idle and leaves the signal untouched.
    bool isBypassed() const
    {
        return ! active.isSmoothing() && juce::exactlyEqual (active.getTargetValue(), 0.0f);
    }

    void process (float* left, float* right, int numSamples)
    {
        if (isBypassed())
        {
            // Keep the filters warm so re-engaging doesn't start from silence.
            for (int i = 0; i < numSamples; ++i)
                split (left[i], right[i]);

            width.skip (numSamples);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i], dryR = right[i];
            const auto  bands = split (dryL, dryR);

            const float w    = width.getNextValue();
            const float mid  = 0.5f * (bands.highL + bands.highR);
            const float side = 0.5f * (bands.highL - bands.highR);

            // Gain match: keep (mid² + side²) the same as before widening.
            midEnergy  = followerCoeff * midEnergy  + (1.0f - followerCoeff) * mid * mid;
            sideEnergy = followerCoeff * sideEnergy + (1.0f - followerCoeff) * side * side;

            const float before = midEnergy + sideEnergy;
            const float after  = midEnergy + w * w * sideEnergy;
            makeup.setTargetValue (after > 1.0e-12f ? juce::jlimit (0.5f, 2.0f, std::sqrt (before / after)) : 1.0f);
            const float g = makeup.getNextValue();

            const float low  = 0.5f * (bands.lowL + bands.lowR);
            const float wetL = low + g * (mid + w * side);
            const float wetR = low + g * (mid - w * side);

            const float a = active.getNextValue();
            left[i]  = dryL + a * (wetL - dryL);
            right[i] = dryR + a * (wetR - dryR);
        }
    }

private:
    struct Bands { float lowL, lowR, highL, highR; };

    void applyCutoff (float hz)
    {
        lowpass.setCutoffFrequency (hz);
        highpass.setCutoffFrequency (hz);
    }

    Bands split (float l, float r)
    {
        if (crossover.isSmoothing())
            applyCutoff (crossover.getNextValue());

        return { lowpass.processSample (0, l),  lowpass.processSample (1, r),
                 highpass.processSample (0, l), highpass.processSample (1, r) };
    }

    juce::dsp::LinkwitzRileyFilter<float> lowpass, highpass;
    juce::SmoothedValue<float> width, active, makeup;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> crossover;

    float crossoverHz   = 120.0f;
    float followerCoeff = 0.0f;
    float midEnergy     = 0.0f;
    float sideEnergy    = 0.0f;
};

} // namespace silo
