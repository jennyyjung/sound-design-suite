#include <catch2/catch_test_macros.hpp>

#include "dsp/Width.h"
#include "analysis/Metrics.h"

#include <random>

using namespace silo::metrics;

namespace
{
constexpr double sr = 48000.0;

// 2 s of stereo test signal: a mono 60 Hz sine (the "bass") plus independent
// noise on each side (the "wide" content).
std::pair<Signal, Signal> testSignal()
{
    std::mt19937 rng (1234);
    std::normal_distribution<float> noise (0.0f, 0.1f);
    Signal l (96000), r (96000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const float bass = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 60.0 * (double) i / sr);
        l[i] = bass + noise (rng);
        r[i] = bass + noise (rng);
    }
    return { l, r };
}

std::pair<Signal, Signal> run (float width, std::pair<Signal, Signal> in)
{
    silo::Width w;
    w.prepare (sr, 512);
    w.setWidth (width);
    for (size_t pos = 0; pos < in.first.size(); pos += 512)
    {
        const int n = (int) std::min<size_t> (512, in.first.size() - pos);
        w.process (in.first.data() + pos, in.second.data() + pos, n);
    }
    return in;
}
}

TEST_CASE ("Width 100% is a bit-exact bypass", "[unit][width]")
{
    auto in  = testSignal();
    auto out = run (1.0f, in);
    CHECK (out.first == in.first);
    CHECK (out.second == in.second);
}

TEST_CASE ("Width 0% folds the highs to mono", "[unit][width]")
{
    auto out = run (0.0f, testSignal());
    CHECK (correlation (out.first, out.second, 48000) > 0.99);
}

TEST_CASE ("Widening keeps the lows mono and the level matched", "[unit][width]")
{
    auto in = testSignal();

    for (float w : { 0.5f, 1.4f, 2.0f })
    {
        INFO ("width " << w);
        auto out = run (w, in);

        const auto lowL = lowpass (out.first, 80.0, sr);
        const auto lowR = lowpass (out.second, 80.0, sr);
        CHECK (correlation (lowL, lowR, 48000) > 0.98);

        CHECK (std::abs (loudnessDb (out.first, out.second, 48000) - loudnessDb (in.first, in.second, 48000)) < 1.0);
    }
}
