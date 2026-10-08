#pragma once

#include <cmath>
#include <optional>

namespace silo
{

// Where we are in the bar, in steps. Produced per sample by Clock::tick().
struct StepInfo
{
    int    step  = 0;     // 0..stepsPerBar-1
    double phase = 0.0;   // 0..1 progress through the current step
    double ppq   = 0.0;   // beats since song start
};

// Turns host tempo + transport into a per-sample step position. When the host
// is stopped it keeps running from its own position so the plugin still previews.
class Clock
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        updateIncrement();
    }

    // Once per block. Pass std::nullopt for ppq when the host is stopped or
    // gives no position.
    void update (double newBpm, std::optional<double> hostPpq, bool isPlaying)
    {
        bpm = newBpm > 0.0 ? newBpm : 120.0;
        updateIncrement();

        if (isPlaying && hostPpq.has_value())
            ppq = *hostPpq;   // follow the host exactly, including loop jumps
    }

    void setStepsPerBeat (double s) { stepsPerBeat = s; }   // 4 = 1/16, 2 = 1/8, 6 = 1/16T
    void setBeatsPerBar (int b)     { beatsPerBar = b; }

    StepInfo tick()
    {
        StepInfo info;
        info.ppq = ppq;

        const double stepPos     = ppq * stepsPerBeat;
        const double stepsPerBar = stepsPerBeat * beatsPerBar;
        const double inBar       = std::fmod (stepPos, stepsPerBar);
        const double wrapped     = inBar < 0.0 ? inBar + stepsPerBar : inBar;

        info.step  = (int) std::floor (wrapped);
        info.phase = wrapped - std::floor (wrapped);

        ppq += beatsPerSample;
        return info;
    }

    double getBpm() const            { return bpm; }
    double getBeatsPerSample() const { return beatsPerSample; }

private:
    void updateIncrement() { beatsPerSample = bpm / 60.0 / sampleRate; }

    double sampleRate     = 44100.0;
    double bpm            = 120.0;
    double ppq            = 0.0;
    double beatsPerSample = 120.0 / 60.0 / 44100.0;
    double stepsPerBeat   = 4.0;
    int    beatsPerBar    = 4;
};

} // namespace silo
