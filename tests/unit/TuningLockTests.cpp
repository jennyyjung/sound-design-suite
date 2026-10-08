// Guards saved sessions against silent retuning.
//
// tuning/tuning.lock records, per macro, the tuning version and a fingerprint
// of its table. Changing a table (anchors, defaults, dice ranges) without
// bumping its "version" fails here, because sessions saved with the old table
// would otherwise play differently with nothing to tell them apart. Editing
// only "status" or "notes" doesn't count as a change.
//
// After an intentional change: bump "version" in the JSON, then paste the line
// this test prints into tuning/tuning.lock.

#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "macros/ZoneTable.h"
#include <BinaryData.h>

#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>

namespace
{
std::string tuningJson (const std::string& fileName)
{
    for (int i = 0; i < TuningData::namedResourceListSize; ++i)
        if (fileName == TuningData::originalFilenames[i])
        {
            int size = 0;
            const char* data = TuningData::getNamedResource (TuningData::namedResourceList[i], size);
            return std::string (data, (size_t) size);
        }
    return {};
}

// Whitespace and line endings removed, and the fields that don't affect the
// sound ("version", "status", "notes") dropped, so the fingerprint only moves
// when the tuning does, on every platform.
std::string canonical (std::string json)
{
    std::string compact;
    bool inString = false;
    for (size_t i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if (c == '"' && (i == 0 || json[i - 1] != '\\')) inString = ! inString;
        if (! inString && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) continue;
        compact += c;
    }
    compact = std::regex_replace (compact, std::regex (R"re("(version|status|notes)":("([^"\\]|\\.)*"|[0-9]+),?)re"), "");
    return compact;
}

std::string fingerprint (const std::string& text)
{
    std::uint64_t h = 1469598103934665603ull;   // FNV-1a 64
    for (char c : text) { h ^= (std::uint64_t) (unsigned char) c; h *= 1099511628211ull; }
    char buf[17];
    std::snprintf (buf, sizeof buf, "%016llx", (unsigned long long) h);
    return buf;
}
}

TEST_CASE ("Tuning changes come with a version bump", "[unit][zones][lock]")
{
    const juce::File lockFile = juce::File (SOUNDSUITE_TUNING_DIR).getChildFile ("tuning.lock");
    REQUIRE (lockFile.existsAsFile());

    juce::StringArray lockLines;
    lockFile.readLines (lockLines);

    for (const auto& m : silo::macros)
    {
        const auto id   = std::string (m.id);
        const auto json = tuningJson (id + ".json");
        REQUIRE (! json.empty());

        auto table = silo::ZoneTable::fromJson (juce::String::fromUTF8 (json.data(), (int) json.size()));
        REQUIRE (table.has_value());

        const auto version  = table->getVersion();
        const auto expected = id + " " + std::to_string (version) + " " + fingerprint (canonical (json));

        juce::String locked;
        for (auto& line : lockLines)
            if (line.trim().startsWith (juce::String (id) + " "))
                locked = line.trim();

        INFO ("tuning/" << id << ".json doesn't match tuning/tuning.lock.\n"
              "If you changed the tuning on purpose: bump its \"version\" (it's " << version << " now),\n"
              "then set this line in tuning/tuning.lock to what this test prints for the new version.\n"
              "Expected lock line for the file as it is: " << expected);

        if (locked.isEmpty())
            FAIL ("no lock line for " << id);

        const auto parts = juce::StringArray::fromTokens (locked, " ", "");
        REQUIRE (parts.size() == 3);

        if (parts[1].getIntValue() == version)
            CHECK (parts[2].toStdString() == fingerprint (canonical (json)));   // same version, different table
        else
            CHECK (locked.toStdString() == expected);                              // version bumped, lock not updated
    }
}

TEST_CASE ("Lock fingerprint ignores formatting and comments, not values", "[unit][zones][lock]")
{
    const std::string a = R"({"macro": "x", "version": 1, "status": "draft", "anchors": [{"at": 0.0, "name": "Off"}]})";
    const std::string b = "{\r\n  \"macro\":\"x\",\r\n  \"version\": 7,\r\n  \"status\": \"tuned\",\r\n  \"anchors\": [ { \"at\": 0.0, \"name\": \"Off\" } ]\r\n}";
    const std::string c = R"({"macro": "x", "version": 1, "status": "draft", "anchors": [{"at": 0.0, "name": "On"}]})";

    CHECK (fingerprint (canonical (a)) == fingerprint (canonical (b)));
    CHECK (fingerprint (canonical (a)) != fingerprint (canonical (c)));
}
