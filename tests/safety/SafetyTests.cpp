// The "musically safe" contract from design/build-test-plan-and-v1-effects.md,
// section 2b, run against every macro through the real plugin processor.
//
// Thresholds are placeholders; tighten them once real renders exist.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginProcessor.h"
#include "analysis/Metrics.h"
#include "AllocationGuard.h"

#include <functional>
#include <random>

using namespace silo::metrics;

namespace
{
struct Stereo { Signal l, r; };

// Every registered macro, straight from the registry.
const std::vector<juce::ParameterID> macroIds = []
{
    std::vector<juce::ParameterID> ids;
    for (std::size_t i = 0; i < silo::numMacros; ++i)
        ids.push_back (silo::macroParameterID (i));
    return ids;
}();

const std::vector<const juce::ParameterID*> allMacros = []
{
    std::vector<const juce::ParameterID*> ptrs;
    for (auto& id : macroIds) ptrs.push_back (&id);
    return ptrs;
}();

const juce::ParameterID& widthPanId = macroIds[silo::macroIndex ("width_pan")];

// 3 s of stereo material: mono bass, a stereo pair of tones, and independent
// noise per side, so width, gain and mono checks all have something to measure.
Stereo testSignal (double sr)
{
    std::mt19937 rng (42);
    std::normal_distribution<float> noise (0.0f, 0.05f);
    const auto n = (size_t) (3.0 * sr);
    Stereo s { Signal (n), Signal (n) };
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const float bass = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t);
        s.l[i] = bass + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t) + noise (rng);
        s.r[i] = bass + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 660.0 * t) + noise (rng);
    }
    return s;
}

// Plain sines: any click stands out against their small sample-to-sample steps.
Stereo toneSignal (double sr)
{
    const auto n = (size_t) (3.0 * sr);
    Stereo s { Signal (n), Signal (n) };
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        s.l[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t);
        s.r[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t);
    }
    return s;
}

struct RenderResult
{
    Stereo      out;
    std::size_t allocations = 0;
};

// Runs the processor over `in`. `knobAt` gives every macro's value at a sample
// position (set once per block, as a host would automate it).
RenderResult render (const Stereo& in, double sr, int blockSize,
                     const std::function<void (SoundSuiteProcessor&, size_t)>& setParams)
{
    SoundSuiteProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, blockSize);
    proc.prepareToPlay (sr, blockSize);

    RenderResult result { in, 0 };
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (size_t pos = 0; pos < in.l.size(); pos += (size_t) blockSize)
    {
        const int n = (int) std::min<size_t> ((size_t) blockSize, in.l.size() - pos);
        buffer.setSize (2, n, false, false, true);
        buffer.copyFrom (0, 0, in.l.data() + pos, n);
        buffer.copyFrom (1, 0, in.r.data() + pos, n);

        setParams (proc, pos);

        {
            silo::test::AllocationGuard guard;
            proc.processBlock (buffer, midi);
            result.allocations += silo::test::AllocationGuard::count();
        }

        std::copy_n (buffer.getReadPointer (0), n, result.out.l.data() + pos);
        std::copy_n (buffer.getReadPointer (1), n, result.out.r.data() + pos);
    }
    return result;
}

void setMacro (SoundSuiteProcessor& p, const juce::ParameterID& id, float value)
{
    p.getState().getParameter (id.getParamID())->setValueNotifyingHost (value);
}

auto fixedMacro (const juce::ParameterID& id, float value)
{
    return [&id, value] (SoundSuiteProcessor& p, size_t) { setMacro (p, id, value); };
}
} // namespace

TEST_CASE ("Zero is bypass: every macro at 0 nulls against the input", "[safety]")
{
    const double sr    = GENERATE (44100.0, 48000.0, 96000.0);
    const int    block = GENERATE (1, 64, 441, 1024);
    INFO ("sr " << sr << ", block " << block);

    const auto in  = testSignal (sr);
    const auto res = render (in, sr, block, [] (SoundSuiteProcessor& p, size_t)
    {
        for (auto* id : allMacros) setMacro (p, *id, 0.0f);
    });

    CHECK (nullDb (res.out.l, in.l) < -120.0);
    CHECK (nullDb (res.out.r, in.r) < -120.0);
}

TEST_CASE ("Every macro position is safe", "[safety]")
{
    const double sr    = GENERATE (44100.0, 96000.0);
    const int    block = GENERATE (64, 441);
    const auto   in    = testSignal (sr);
    const auto   settle = (size_t) sr;   // ignore the first second (smoothers, followers)

    const double inLoud     = loudnessDb (in.l, in.r, settle);
    const double inMonoDrop = monoLoudnessDb (in.l, in.r, settle) - inLoud;

    for (auto* id : allMacros)
    {
        for (int step = 0; step <= 10; ++step)
        {
            const float knob = (float) step / 10.0f;
            INFO (id->getParamID() << " = " << knob << ", sr " << sr << ", block " << block);

            const auto res = render (in, sr, block, fixedMacro (*id, knob));
            const auto& out = res.out;

            REQUIRE (allFinite (out.l));
            REQUIRE (allFinite (out.r));
            CHECK (res.allocations == 0);

            // Gain matched within ±1 dB (RMS stand-in for ±1 LU).
            CHECK (std::abs (loudnessDb (out.l, out.r, settle) - inLoud) < 1.0);

            // Lows stay mono.
            CHECK (correlation (lowpass (out.l, 80.0, sr), lowpass (out.r, 80.0, sr), settle) > 0.98);

            // Mono fold-down loses at most 3 dB more than the input already did.
            const double monoDrop = monoLoudnessDb (out.l, out.r, settle) - loudnessDb (out.l, out.r, settle);
            CHECK (monoDrop > inMonoDrop - 3.0);
        }
    }
}

TEST_CASE ("Sweeping a macro never clicks", "[safety]")
{
    const double sr = 48000.0;
    const int block = GENERATE (64, 441);
    const auto in   = toneSignal (sr);
    const double inStep = std::max (maxStep (in.l), maxStep (in.r));

    for (auto* id : allMacros)
    {
        INFO (id->getParamID() << ", block " << block);

        // Up and back down over 3 s, as automation would.
        const auto res = render (in, sr, block, [id, total = in.l.size()] (SoundSuiteProcessor& p, size_t pos)
        {
            const float x = (float) pos / (float) total;
            setMacro (p, *id, x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x);
        });

        const double outStep = std::max (maxStep (res.out.l), maxStep (res.out.r));
        CHECK (toDb (outStep) - toDb (inStep) < 6.0);
    }
}

TEST_CASE ("Results don't depend on block size", "[safety]")
{
    const double sr = 48000.0;
    const auto in   = testSignal (sr);

    for (auto* id : allMacros)
    {
        INFO (id->getParamID());
        const auto small = render (in, sr, 1,    fixedMacro (*id, 0.6f));
        const auto large = render (in, sr, 1024, fixedMacro (*id, 0.6f));
        CHECK (nullDb (small.out.l, large.out.l) < -90.0);
        CHECK (nullDb (small.out.r, large.out.r) < -90.0);
    }
}

TEST_CASE ("Advanced-view narrowing works and stays level", "[safety]")
{
    const double sr = 48000.0;
    const auto in   = testSignal (sr);
    const auto settle = (size_t) sr;

    const auto res = render (in, sr, 256, [] (SoundSuiteProcessor& p, size_t)
    {
        p.getState().getParameter (silo::ids::widthManual.getParamID())->setValueNotifyingHost (0.0f);   // 0% = mono
    });

    CHECK (correlation (res.out.l, res.out.r, settle) > 0.99);
    CHECK (std::abs (loudnessDb (res.out.l, res.out.r, settle) - loudnessDb (in.l, in.r, settle)) < 1.0);
}

TEST_CASE ("State survives save and reload", "[safety]")
{
    SoundSuiteProcessor a;
    setMacro (a, widthPanId, 0.42f);
    a.getState().getParameter (silo::ids::widthManual.getParamID())->setValueNotifyingHost (0.3f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    SoundSuiteProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (std::abs (b.getState().getParameter (widthPanId.getParamID())->getValue() - 0.42f) < 1e-4f);
    CHECK (std::abs (b.getState().getParameter (silo::ids::widthManual.getParamID())->getValue() - 0.3f) < 1e-4f);
}

TEST_CASE ("Saved state records each macro's tuning version and dice seed", "[safety][state]")
{
    SoundSuiteProcessor a;
    a.setDiceSeed (silo::macroIndex ("space"), 123456789);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("state_version") == SoundSuiteProcessor::stateVersion);

    auto* tuning = xml->getChildByName ("Tuning");
    REQUIRE (tuning != nullptr);
    for (std::size_t i = 0; i < silo::numMacros; ++i)
    {
        auto* m = tuning->getChildByAttribute ("id", silo::toString (silo::macros[i].id));
        REQUIRE (m != nullptr);
        CHECK (m->getIntAttribute ("version") == a.getZones (i).get().getVersion());
        CHECK (m->hasAttribute ("dice_seed"));
    }

    SoundSuiteProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    CHECK (b.getDiceSeed (silo::macroIndex ("space")) == 123456789);
    CHECK (b.getLastLoadMismatches().empty());
}

TEST_CASE ("Loading a session saved with a different tuning version is reported", "[safety][state]")
{
    SoundSuiteProcessor a;
    setMacro (a, widthPanId, 0.42f);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    const int current = a.getZones (silo::macroIndex ("width_pan")).get().getVersion();
    xml->getChildByName ("Tuning")->getChildByAttribute ("id", "width_pan")->setAttribute ("version", current + 5);

    juce::MemoryBlock edited;
    juce::AudioProcessor::copyXmlToBinary (*xml, edited);

    SoundSuiteProcessor b;
    b.setStateInformation (edited.getData(), (int) edited.getSize());

    REQUIRE (b.getLastLoadMismatches().size() == 1);
    CHECK (b.getLastLoadMismatches()[0].macro == "width_pan");
    CHECK (b.getLastLoadMismatches()[0].savedVersion == current + 5);
    CHECK (b.getLastLoadMismatches()[0].currentVersion == current);
    // No migration case yet, so the knob position is kept.
    CHECK (std::abs (b.getState().getParameter ("width_pan")->getValue() - 0.42f) < 1e-4f);
}

TEST_CASE ("Sessions from before tuning versions load as version 1", "[safety][state]")
{
    // What PR #1 builds saved: parameters only, no <Tuning>.
    juce::XmlElement old ("SoundSuite");
    old.setAttribute ("version", 1);
    auto* p = old.createNewChildElement ("PARAM");
    p->setAttribute ("id", "width_pan");
    p->setAttribute ("value", 0.3);

    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary (old, blob);

    SoundSuiteProcessor proc;
    proc.setStateInformation (blob.getData(), (int) blob.getSize());

    for (const auto& mismatch : proc.getLastLoadMismatches())
        CHECK (mismatch.savedVersion == 1);
    CHECK (std::abs (proc.getState().getParameter ("width_pan")->getValue() - 0.3f) < 1e-4f);
    CHECK (proc.getDiceSeed (0) == 0);
}

TEST_CASE ("Tail and latency come from the modules", "[safety]")
{
    SoundSuiteProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    // Width has no memory beyond its smoothing, so the chain reports none yet.
    // Space (build step 4) adds its decay here.
    CHECK (proc.getTailLengthSeconds() >= 0.0);
    CHECK (proc.getLatencySamples() == proc.getChainLatencySamples());
}
