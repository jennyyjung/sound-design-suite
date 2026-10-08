#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Clock.h"

using Catch::Matchers::WithinAbs;

TEST_CASE ("Clock maps PPQ to 16th-note steps", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);

    clock.update (120.0, 0.0, true);
    auto first = clock.tick();
    CHECK (first.step == 0);
    CHECK_THAT (first.phase, WithinAbs (0.0, 1e-9));

    clock.update (120.0, 1.25, true);   // beat 1.25 = 16th #5
    auto s = clock.tick();
    CHECK (s.step == 5);
    CHECK_THAT (s.phase, WithinAbs (0.0, 1e-9));

    clock.update (120.0, 3.875, true);  // halfway through 16th #15
    s = clock.tick();
    CHECK (s.step == 15);
    CHECK_THAT (s.phase, WithinAbs (0.5, 1e-9));
}

TEST_CASE ("Clock wraps at the bar and follows loop jumps", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (44100.0);

    clock.update (90.0, 4.0 + 0.5, true);  // bar 2, 16th #2
    CHECK (clock.tick().step == 2);

    clock.update (90.0, 0.25, true);       // host looped back
    CHECK (clock.tick().step == 1);
}

TEST_CASE ("Clock advances one beat per beat-length of samples", "[unit][clock]")
{
    for (double bpm : { 60.0, 120.0, 174.0 })
    {
        silo::Clock clock;
        clock.prepare (48000.0);
        clock.update (bpm, 0.0, true);

        const int samplesPerBeat = (int) std::round (48000.0 * 60.0 / bpm);
        silo::StepInfo info;
        for (int i = 0; i <= samplesPerBeat; ++i)
            info = clock.tick();

        CHECK_THAT (info.ppq, WithinAbs (1.0, 1e-3));
        CHECK (info.step == 4);
    }
}

TEST_CASE ("Clock keeps running when the transport is stopped", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);
    clock.update (120.0, std::nullopt, false);

    double last = -1.0;
    for (int i = 0; i < 1000; ++i)
    {
        auto info = clock.tick();
        CHECK (info.ppq > last);
        last = info.ppq;
    }
}

TEST_CASE ("Clock supports triplet divisions", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);
    clock.setStepsPerBeat (6.0);           // 1/16 triplets: 24 steps per 4/4 bar
    clock.update (120.0, 3.5, true);
    CHECK (clock.tick().step == 21);
}
