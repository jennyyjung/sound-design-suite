#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Measurements shared by the safety tests and the render tool. Plain C++ so
// they can be reused anywhere.
//
// Loudness here is RMS in dB, a stand-in for LUFS until a proper
// ITU-R BS.1770 meter is added.
namespace silo::metrics
{
using Signal = std::vector<float>;

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

inline double rms (const Signal& s, size_t start = 0)
{
    double sum = 0.0;
    for (size_t i = start; i < s.size(); ++i) sum += (double) s[i] * s[i];
    const auto n = s.size() > start ? s.size() - start : 1;
    return std::sqrt (sum / (double) n);
}

// Stereo loudness: RMS of both channels together, in dB.
inline double loudnessDb (const Signal& l, const Signal& r, size_t start = 0)
{
    const double a = rms (l, start), b = rms (r, start);
    return toDb (std::sqrt (0.5 * (a * a + b * b)));
}

// Loudness of the mono fold-down (L+R)/2, in dB.
inline double monoLoudnessDb (const Signal& l, const Signal& r, size_t start = 0)
{
    Signal m (l.size());
    for (size_t i = 0; i < l.size(); ++i) m[i] = 0.5f * (l[i] + r[i]);
    return toDb (rms (m, start));
}

inline double correlation (const Signal& l, const Signal& r, size_t start = 0)
{
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = start; i < l.size(); ++i)
    {
        lr += (double) l[i] * r[i];
        ll += (double) l[i] * l[i];
        rr += (double) r[i] * r[i];
    }
    return (ll > 0 && rr > 0) ? lr / std::sqrt (ll * rr) : 1.0;
}

// Peak absolute difference between two signals, in dB.
inline double nullDb (const Signal& a, const Signal& b)
{
    double peak = 0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        peak = std::max (peak, (double) std::abs (a[i] - b[i]));
    return toDb (peak);
}

// Largest sample-to-sample jump: a cheap click detector.
inline double maxStep (const Signal& s, size_t start = 0)
{
    double peak = 0;
    for (size_t i = std::max<size_t> (start, 1); i < s.size(); ++i)
        peak = std::max (peak, (double) std::abs (s[i] - s[i - 1]));
    return peak;
}

inline bool allFinite (const Signal& s)
{
    return std::all_of (s.begin(), s.end(), [] (float x) { return std::isfinite (x); });
}

// One-pole low-pass, enough to isolate "the lows" for mono checks.
inline Signal lowpass (const Signal& s, double cutoffHz, double sampleRate)
{
    const double a = std::exp (-2.0 * 3.141592653589793 * cutoffHz / sampleRate);
    Signal out (s.size());
    double y1 = 0, y2 = 0;   // two poles, ~12 dB/oct
    for (size_t i = 0; i < s.size(); ++i)
    {
        y1 = (1 - a) * s[i] + a * y1;
        y2 = (1 - a) * y1 + a * y2;
        out[i] = (float) y2;
    }
    return out;
}
} // namespace silo::metrics
