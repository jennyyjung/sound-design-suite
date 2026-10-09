#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Clock.h"

using Catch::Matchers::WithinAbs;

namespace
{
const silo::Transport& playingAt (silo::Clock& clock, double ppq, int num = 4, int den = 4, std::optional<double> barStart = std::nullopt)
{
    return clock.update (120.0, ppq, barStart, num, den, true);
}
}

TEST_CASE ("Each module reads its own rate from one shared timeline", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);
    const auto& t = playingAt (clock, 3.875);   // halfway through the 16th #15 of bar 1

    const double sixteenth = *silo::beatsForNote ("1/16", t.beatsPerBar);
    const double bar       = *silo::beatsForNote ("1 bar", t.beatsPerBar);
    const double quarter   = *silo::beatsForNote ("1/4", t.beatsPerBar);

    CHECK (t.cycleIndex (t.ppq, sixteenth) == 15);
    CHECK_THAT (t.phase (t.ppq, sixteenth), WithinAbs (0.5,     1e-9));
    CHECK_THAT (t.phase (t.ppq, quarter),   WithinAbs (0.875,   1e-9));
    CHECK_THAT (t.phase (t.ppq, bar),       WithinAbs (0.96875, 1e-9));
}

TEST_CASE ("Reading the timeline doesn't move it; only advance() does", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);
    clock.update (120.0, std::nullopt, std::nullopt, 4, 4, false);
    const double start = clock.getTransport().ppq;

    // Several modules reading the same block
    for (int module = 0; module < 3; ++module)
        for (int i = 0; i < 512; ++i)
            (void) clock.getTransport().phaseAt (i, 0.25);

    CHECK_THAT (clock.getTransport().ppq, WithinAbs (start, 1e-12));

    clock.advance (24000);   // half a second at 120 BPM = 1 beat
    clock.update (120.0, std::nullopt, std::nullopt, 4, 4, false);
    CHECK_THAT (clock.getTransport().ppq, WithinAbs (start + 1.0, 1e-9));
}

TEST_CASE ("Bar length follows the host's time signature", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);

    CHECK_THAT (playingAt (clock, 0.0, 4, 4).beatsPerBar, WithinAbs (4.0, 1e-12));
    CHECK_THAT (playingAt (clock, 0.0, 3, 4).beatsPerBar, WithinAbs (3.0, 1e-12));
    CHECK_THAT (playingAt (clock, 0.0, 6, 8).beatsPerBar, WithinAbs (3.0, 1e-12));
    CHECK_THAT (playingAt (clock, 0.0, 7, 8).beatsPerBar, WithinAbs (3.5, 1e-12));

    // In 3/4, beat 3.0 is the downbeat of bar 2: a 1-bar cycle restarts there.
    const auto& t = playingAt (clock, 3.0, 3, 4);
    CHECK_THAT (t.phase (t.ppq, *silo::beatsForNote ("1 bar", t.beatsPerBar)), WithinAbs (0.0, 1e-9));
    CHECK (t.cycleIndex (t.ppq, 0.25) == 0);
}

TEST_CASE ("Patterns align to the host's bar start after a meter change", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);

    // Song: one bar of 4/4, then 3/4. Host says bar 2 starts at beat 4.
    const auto& t = playingAt (clock, 5.0, 3, 4, 4.0);
    CHECK (t.cycleIndex (t.ppq, 0.25) == 4);   // 16th #4 of the 3/4 bar
    CHECK_THAT (t.phase (t.ppq, 3.0), WithinAbs (1.0 / 3.0, 1e-9));
}

TEST_CASE ("Clock follows loop jumps and keeps running when stopped", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (44100.0);

    CHECK (playingAt (clock, 4.5).cycleIndex (4.5, 0.25) == 2);
    CHECK_THAT (playingAt (clock, 0.25).ppq, WithinAbs (0.25, 1e-12));   // host looped back

    clock.advance (441);
    const double before = clock.update (120.0, std::nullopt, std::nullopt, 4, 4, false).ppq;
    clock.advance (441);
    CHECK (clock.update (120.0, std::nullopt, std::nullopt, 4, 4, false).ppq > before);
}

TEST_CASE ("Transport gives per-sample positions within a block", "[unit][clock]")
{
    for (double bpm : { 60.0, 120.0, 174.0 })
    {
        silo::Clock clock;
        clock.prepare (48000.0);
        const auto& t = clock.update (bpm, 0.0, std::nullopt, 4, 4, true);

        const int samplesPerBeat = (int) std::round (48000.0 * 60.0 / bpm);
        CHECK_THAT (t.ppqAt (samplesPerBeat), WithinAbs (1.0, 1e-3));
    }
}

TEST_CASE ("Note values convert to beats", "[unit][clock]")
{
    CHECK_THAT (*silo::beatsForNote ("1/4", 4),    WithinAbs (1.0,       1e-12));
    CHECK_THAT (*silo::beatsForNote ("1/16", 4),   WithinAbs (0.25,      1e-12));
    CHECK_THAT (*silo::beatsForNote ("1/128", 4),  WithinAbs (0.03125,   1e-12));
    CHECK_THAT (*silo::beatsForNote ("1/8T", 4),   WithinAbs (1.0 / 3.0, 1e-12));
    CHECK_THAT (*silo::beatsForNote ("1/8.", 4),   WithinAbs (0.75,      1e-12));
    CHECK_THAT (*silo::beatsForNote ("1 bar", 3),  WithinAbs (3.0,       1e-12));
    CHECK_THAT (*silo::beatsForNote ("2 bars", 4), WithinAbs (8.0,       1e-12));
    CHECK_FALSE (silo::beatsForNote ("off", 4).has_value());
    CHECK_FALSE (silo::beatsForNote ("1/0", 4).has_value());
}

TEST_CASE ("Cycles longer than a bar run across bar lines", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);
    const double twoBars = *silo::beatsForNote ("2 bars", 4);

    // Beat 6 of 4/4 is halfway into bar 2: three quarters through a 2-bar cycle.
    const auto& t = playingAt (clock, 6.0, 4, 4, 4.0);
    CHECK_THAT (t.phase (t.ppq, twoBars), WithinAbs (0.75, 1e-9));
    CHECK (t.cycleIndex (t.ppq, twoBars) == 0);

    // The cycle completes at beat 8, not at every downbeat.
    const auto& u = playingAt (clock, 9.0, 4, 4, 8.0);
    CHECK_THAT (u.phase (u.ppq, twoBars), WithinAbs (0.125, 1e-9));
    CHECK (u.cycleIndex (u.ppq, twoBars) == 1);
}

TEST_CASE ("Cycles that don't fit the bar evenly don't jump at the downbeat", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);
    const double dottedQuarter = *silo::beatsForNote ("1/4.", 4);   // 1.5 beats

    // Phase moves smoothly from just before the bar line to just after it.
    const auto& before = playingAt (clock, 3.999, 4, 4, 0.0);
    const double a = before.phase (before.ppq, dottedQuarter);
    const auto& after = playingAt (clock, 4.001, 4, 4, 4.0);
    const double b = after.phase (after.ppq, dottedQuarter);

    CHECK_THAT (a, WithinAbs (2.499 / 1.5 - 1.0, 1e-9));
    CHECK_THAT (b, WithinAbs (a + 0.002 / 1.5, 1e-9));
}

TEST_CASE ("Cycles that fit the bar still restart on the host's downbeat", "[unit][clock]")
{
    silo::Clock clock;
    clock.prepare (48000.0);

    // 7/8 bar (3.5 beats) starting at beat 4 after a 4/4 bar: an 1/8 grid
    // restarts at 4, though 4 isn't a multiple of 1/8-in-7/8 from song start.
    const auto& t = playingAt (clock, 4.0, 7, 8, 4.0);
    CHECK (t.cycleIndex (t.ppq, *silo::beatsForNote ("1/8", t.beatsPerBar)) == 0);
    CHECK_THAT (t.phase (t.ppq, *silo::beatsForNote ("1 bar", t.beatsPerBar)), WithinAbs (0.0, 1e-9));
}
