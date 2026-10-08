#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "macros/ZoneTable.h"
#include <BinaryData.h>

using Catch::Matchers::WithinAbs;

namespace
{
const char* table = R"({
  "macro": "test",
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
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"anchors": []})", &error).has_value());
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"anchors": [{"at": 0.2}]})", &error).has_value());
    CHECK (error.contains ("zero is bypass"));
    CHECK_FALSE (silo::ZoneTable::fromJson (R"({"anchors": [{"at": 0.0}, {"at": 0.5}, {"at": 0.4}]})", &error).has_value());
}

TEST_CASE ("Shipped tuning files parse and start at bypass", "[unit][zones]")
{
    const std::pair<const char*, int> files[] = {
        { TuningData::gate_chop_json, TuningData::gate_chop_jsonSize },
        { TuningData::width_pan_json, TuningData::width_pan_jsonSize },
        { TuningData::space_json,     TuningData::space_jsonSize },
    };

    for (auto [data, size] : files)
    {
        juce::String error;
        auto t = silo::ZoneTable::fromJson (juce::String::fromUTF8 (data, size), &error);
        INFO (error);
        REQUIRE (t.has_value());
        CHECK (juce::exactlyEqual (t->getAnchors().front().at, 0.0f));
    }

    // The width macro only widens; narrowing belongs to the advanced view.
    auto width = silo::ZoneTable::fromJson (juce::String::fromUTF8 (TuningData::width_pan_json, TuningData::width_pan_jsonSize));
    for (auto& a : width->getAnchors())
        CHECK ((double) a.params.getWithDefault ("width", 1.0) >= 1.0);
}
