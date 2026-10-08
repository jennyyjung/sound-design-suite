#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "macros/ZoneTable.h"
#include "Parameters.h"
#include <BinaryData.h>

using Catch::Matchers::WithinAbs;

namespace
{
const char* table = R"({
  "macro": "test",
  "version": 1,
  "defaults": { "mix": 0.0, "rate": "1/4" },
  "anchors": [
    { "at": 0.0, "name": "Off",  "params": {} },
    { "at": 0.5, "name": "Mid",  "params": { "mix": 0.5, "rate": "1/8" } },
    { "at": 1.0, "name": "Full", "params": { "mix": 0.2, "rate": "1/16" } }
  ]
})";
}

TEST_CASE ("ZoneTable morphs numbers linearly between anchors", "[unit][zones]")
{
    auto t = silo::ZoneTable::fromJson (table);
    REQUIRE (t.has_value());

    const juce::Identifier mix ("mix");
    CHECK_THAT (t->getNumber (mix, 0.0f,  -1.0), WithinAbs (0.0,  1e-6));   // from defaults
    CHECK_THAT (t->getNumber (mix, 0.25f, -1.0), WithinAbs (0.25, 1e-6));
    CHECK_THAT (t->getNumber (mix, 0.5f,  -1.0), WithinAbs (0.5,  1e-6));
    CHECK_THAT (t->getNumber (mix, 0.75f, -1.0), WithinAbs (0.35, 1e-6));
    CHECK_THAT (t->getNumber (mix, 1.0f,  -1.0), WithinAbs (0.2,  1e-6));
    CHECK_THAT (t->getNumber (mix, 7.0f,  -1.0), WithinAbs (0.2,  1e-6));   // clamped
}

TEST_CASE ("ZoneTable never overshoots its anchors", "[unit][zones]")
{
    auto t = silo::ZoneTable::fromJson (table);
    const juce::Identifier mix ("mix");

    for (int i = 0; i <= 1000; ++i)
    {
        const float k = (float) i / 1000.0f;
        const double v = t->getNumber (mix, k, 0.0);
        CHECK (v >= 0.0);
        CHECK (v <= 0.5 + 1e-9);
    }
}

TEST_CASE ("ZoneTable switches choices at the midpoint", "[unit][zones]")
{
    auto t = silo::ZoneTable::fromJson (table);
    const juce::Identifier rate ("rate");

    CHECK (t->getChoice (rate, 0.20f, "x") == "1/4");
    CHECK (t->getChoice (rate, 0.30f, "x") == "1/8");
    CHECK (t->getChoice (rate, 0.74f, "x") == "1/8");
    CHECK (t->getChoice (rate, 0.76f, "x") == "1/16");
    CHECK (t->getZoneName (0.9f) == "Full");
}

TEST_CASE ("ZoneTable rejects broken tables", "[unit][zones]")
{
    juce::String error;
    CHECK_FALSE (silo::ZoneTable::fromJson ("{", &error).has_value());
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"version": 1, "anchors": []})", &error).has_value());
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"version": 1, "anchors": [{"at": 0.2}]})", &error).has_value());
    CHECK (error.contains ("zero is bypass"));
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"version": 1, "anchors": [{"at": 0.0}, {"at": 0.5}, {"at": 0.4}]})", &error).has_value());
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"anchors": [{"at": 0.0}]})", &error).has_value());
    CHECK (error.contains ("version"));
}

namespace
{
juce::String tuningResource (const juce::String& fileName)
{
    for (int i = 0; i < TuningData::namedResourceListSize; ++i)
        if (fileName == TuningData::originalFilenames[i])
        {
            int size = 0;
            const char* data = TuningData::getNamedResource (TuningData::namedResourceList[i], size);
            return juce::String::fromUTF8 (data, size);
        }
    return {};
}
}

TEST_CASE ("Every registered macro has a valid tuning file that starts at bypass", "[unit][zones]")
{
    for (const auto& m : silo::macros)
    {
        const auto id = silo::toString (m.id);
        INFO ("macro " << id);

        const auto json = tuningResource (id + ".json");
        REQUIRE (json.isNotEmpty());   // tuning/<id>.json is missing

        juce::String error;
        auto t = silo::ZoneTable::fromJson (json, &error);
        INFO (error);
        REQUIRE (t.has_value());
        CHECK (t->getMacro() == id);
        CHECK (juce::exactlyEqual (t->getAnchors().front().at, 0.0f));
    }
}

TEST_CASE ("Every tuning file belongs to a registered macro", "[unit][zones]")
{
    for (int i = 0; i < TuningData::namedResourceListSize; ++i)
    {
        const juce::String file = TuningData::originalFilenames[i];
        INFO ("tuning/" << file << " has no entry in silo::macros (MacroRegistry.h)");

        bool registered = false;
        for (const auto& m : silo::macros)
            registered |= (silo::toString (m.id) + ".json" == file);
        CHECK (registered);
    }
}

TEST_CASE ("The width macro only widens; narrowing is the advanced view's job", "[unit][zones]")
{
    auto width = silo::ZoneTable::fromJson (tuningResource ("width_pan.json"));
    REQUIRE (width.has_value());
    for (auto& a : width->getAnchors())
        CHECK ((double) a.params.getWithDefault ("width", 1.0) >= 1.0);
}
