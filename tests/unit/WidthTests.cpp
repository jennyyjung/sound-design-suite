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

TEST_CASE ("Phase-offset stereo bass comes out mono when width is active", "[unit][width]")
{
    // Bass 90 degrees out of phase between the sides (correlation 0 going in).
    Signal l (96000), r (96000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double ph = 2.0 * 3.141592653589793 * 50.0 * (double) i / sr;
        const float tone = 0.1f * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * (double) i / sr);
        l[i] = 0.4f * (float) std::sin (ph) + tone;
        r[i] = 0.4f * (float) std::cos (ph) + tone;
    }
    REQUIRE (std::abs (correlation (lowpass (l, 60.0, sr), lowpass (r, 60.0, sr), 48000)) < 0.1);

    for (float w : { 0.0f, 0.5f, 1.4f })
    {
        INFO ("width " << w);
        auto out = run (w, { l, r });
        CHECK (correlation (lowpass (out.first, 60.0, sr), lowpass (out.second, 60.0, sr), 48000) > 0.98);
    }
}

TEST_CASE ("Automating the crossover while width is active doesn't click", "[unit][width]")
{
    Signal l (96000), r (96000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 110.0 * (double) i / sr);
        r[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 165.0 * (double) i / sr);
    }
    const double inStep = std::max (maxStep (l), maxStep (r));

    silo::Width w;
    w.prepare (sr, 256);
    w.setWidth (1.5f);
    for (size_t pos = 0, block = 0; pos < l.size(); pos += 256, ++block)
    {
        if (pos > 24000)   // after width has faded in, jump the crossover every block
            w.setCrossover (block % 2 == 0 ? 60.0f : 250.0f);
        const int n = (int) std::min<size_t> (256, l.size() - pos);
        w.process (l.data() + pos, r.data() + pos, n);
    }

    const double outStep = std::max (maxStep (l, 24000), maxStep (r, 24000));
    CHECK (toDb (outStep) - toDb (inStep) < 6.0);
}
