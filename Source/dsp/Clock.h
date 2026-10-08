#pragma once

#include <cmath>
#include <optional>
#include <string_view>

namespace silo
{

// Stateless: position 0..1 within a cycle of `beatsPerCycle` beats.
inline double phase (double beats, double beatsPerCycle)
{
    if (beatsPerCycle <= 0.0)
        return 0.0;
    const double x = beats / beatsPerCycle;
    return x - std::floor (x);
}

// One block's view of musical time, shared read-only by every module.
//
// Nothing here advances time: the processor owns the single Clock and moves it
// forward once per block. Each module asks for its own rate with phase(), so a
// 1/16 gate, a 1-bar auto-pan and a 1/128 pre-delay all read the same timeline.
struct Transport
{
    double ppq            = 0.0;     // quarter-note beats since song start, at the block's first sample
    double beatsPerSample = 0.0;
    double bpm            = 120.0;
    double beatsPerBar    = 4.0;     // in quarter notes: 4/4 = 4, 3/4 = 3, 6/8 = 3, 7/8 = 3.5
    double barStartPpq    = 0.0;     // where the current bar began
    bool   playing        = false;

    double ppqAt (int sampleInBlock) const { return ppq + beatsPerSample * sampleInBlock; }

    // 0..1 position within a cycle `beatsPerCycle` long, aligned to the bar so
    // patterns restart on the downbeat in any meter.
    double phase (double atPpq, double beatsPerCycle) const
    {
        return silo::phase (atPpq - barStartPpq, beatsPerCycle);
    }

    double phaseAt (int sampleInBlock, double beatsPerCycle) const
    {
        return phase (ppqAt (sampleInBlock), beatsPerCycle);
    }

    // Which cycle (step) we're in since the bar start, e.g. the 16th-note index.
    long long cycleIndex (double atPpq, double beatsPerCycle) const
    {
        return (long long) std::floor ((atPpq - barStartPpq) / beatsPerCycle);
    }
};

// Note value -> length in quarter-note beats. Accepts "1/16", "1/8T" (triplet),
// "1/4." (dotted), "1 bar", "2 bars". Returns nullopt for anything else.
inline std::optional<double> beatsForNote (std::string_view note, double beatsPerBar)
{
    auto trim = [] (std::string_view s)
    {
        while (! s.empty() && s.front() == ' ') s.remove_prefix (1);
        while (! s.empty() && s.back()  == ' ') s.remove_suffix (1);
        return s;
    };
    note = trim (note);

    auto parseNumber = [] (std::string_view s, double& out)
    {
        if (s.empty()) return false;
        double v = 0.0;
        for (char c : s)
        {
            if (c < '0' || c > '9') return false;
            v = v * 10.0 + (c - '0');
        }
        out = v;
        return true;
    };

    if (auto pos = note.find ("bar"); pos != std::string_view::npos)
    {
        double count = 1.0;
        auto num = trim (note.substr (0, pos));
        if (! num.empty() && ! parseNumber (num, count)) return std::nullopt;
        return count * beatsPerBar;
    }

    double factor = 1.0;
    if (! note.empty() && (note.back() == 'T' || note.back() == 't')) { factor = 2.0 / 3.0; note.remove_suffix (1); }
    else if (! note.empty() && note.back() == '.')                   { factor = 1.5;       note.remove_suffix (1); }

    const auto slash = note.find ('/');
    if (slash == std::string_view::npos) return std::nullopt;

    double num = 0, den = 0;
    if (! parseNumber (note.substr (0, slash), num) || ! parseNumber (note.substr (slash + 1), den) || den <= 0.0)
        return std::nullopt;

    return 4.0 * num / den * factor;   // a whole note is 4 quarter-note beats
}

// The plugin's single time source. The processor calls update() at the start of
// each block and advance() at the end; modules only ever see the Transport.
// When the host is stopped it keeps running from its own position so the plugin
// still previews.
class Clock
{
public:
    void prepare (double newSampleRate) { sampleRate = newSampleRate; }

    // Host values for this block. Missing ones (stopped transport, hosts that
    // don't report them) fall back to the free-running clock and 4/4.
    const Transport& update (double bpm,
                             std::optional<double> hostPpq,
                             std::optional<double> hostBarStartPpq,
                             int timeSigNumerator,
                             int timeSigDenominator,
                             bool isPlaying)
    {
        transport.bpm            = bpm > 0.0 ? bpm : 120.0;
        transport.beatsPerSample = transport.bpm / 60.0 / sampleRate;
        transport.beatsPerBar    = (timeSigNumerator > 0 && timeSigDenominator > 0)
                                     ? timeSigNumerator * 4.0 / timeSigDenominator
                                     : 4.0;
        transport.playing        = isPlaying;

        if (isPlaying && hostPpq.has_value())
            freePpq = *hostPpq;   // follow the host exactly, including loop jumps

        transport.ppq = freePpq;
        transport.barStartPpq = (isPlaying && hostBarStartPpq.has_value())
                                  ? *hostBarStartPpq
                                  : std::floor (freePpq / transport.beatsPerBar) * transport.beatsPerBar;
        return transport;
    }

    // The only place time moves forward.
    void advance (int numSamples) { freePpq += transport.beatsPerSample * numSamples; }

    const Transport& getTransport() const { return transport; }

private:
    double    sampleRate = 44100.0;
    double    freePpq    = 0.0;
    Transport transport;
};

} // namespace silo
