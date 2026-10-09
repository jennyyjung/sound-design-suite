#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace silo
{

// A macro's sweep, as hand-voiced anchors loaded from tuning/<macro>.json.
//
// Numeric parameters morph linearly between the two anchors around the knob
// position. String parameters (note values, pattern names) can't be blended,
// so they switch at the midpoint between anchors. A parameter an anchor leaves
// out takes its value from the table's "defaults".
//
// Lookups do not allocate, so they are safe to call from processBlock.
class ZoneTable
{
public:
    struct Anchor
    {
        float              at = 0.0f;
        juce::String       name;
        juce::NamedValueSet params;
    };

    static std::optional<ZoneTable> fromJson (const juce::String& json, juce::String* error = nullptr)
    {
        auto fail = [error] (const juce::String& msg) -> std::optional<ZoneTable>
        {
            if (error != nullptr) *error = msg;
            return std::nullopt;
        };

        juce::var root;
        auto result = juce::JSON::parse (json, root);
        if (result.failed())
            return fail ("invalid JSON: " + result.getErrorMessage());

        ZoneTable table;
        table.macro   = root.getProperty ("macro", {}).toString();
        table.version = (int) root.getProperty ("version", 0);
        if (table.version < 1)
            return fail ("missing or invalid \"version\" (must be a whole number >= 1)");

        if (auto* defaults = root.getProperty ("defaults", {}).getDynamicObject())
            table.defaults = defaults->getProperties();

        auto* anchors = root.getProperty ("anchors", {}).getArray();
        if (anchors == nullptr || anchors->isEmpty())
            return fail ("no anchors");

        for (auto& a : *anchors)
        {
            Anchor anchor;
            anchor.at   = (float) (double) a.getProperty ("at", -1.0);
            anchor.name = a.getProperty ("name", {}).toString();

            if (auto* params = a.getProperty ("params", {}).getDynamicObject())
                anchor.params = params->getProperties();

            if (anchor.at < 0.0f || anchor.at > 1.0f)
                return fail ("anchor '" + anchor.name + "' has 'at' outside 0..1");

            if (! table.anchors.empty() && anchor.at <= table.anchors.back().at)
                return fail ("anchors must be in increasing 'at' order");

            table.anchors.push_back (std::move (anchor));
        }

        if (! juce::exactlyEqual (table.anchors.front().at, 0.0f))
            return fail ("first anchor must be at 0 (zero is bypass)");

        return table;
    }

    double getNumber (const juce::Identifier& param, float knob, double fallback) const
    {
        auto [lo, hi, t] = locate (knob);
        const double a = valueAt (*lo, param, fallback);
        const double b = valueAt (*hi, param, fallback);
        return a + (b - a) * (double) t;
    }

    juce::String getChoice (const juce::Identifier& param, float knob, const juce::String& fallback) const
    {
        auto [lo, hi, t] = locate (knob);
        const auto& anchor = t < 0.5f ? *lo : *hi;

        if (auto* v = anchor.params.getVarPointer (param))
            return v->toString();
        if (auto* v = defaults.getVarPointer (param))
            return v->toString();
        return fallback;
    }

    // Name of the anchor nearest to the knob, for the UI.
    const juce::String& getZoneName (float knob) const
    {
        auto [lo, hi, t] = locate (knob);
        return t < 0.5f ? lo->name : hi->name;
    }

    const juce::String& getMacro() const          { return macro; }
    int getVersion() const                        { return version; }
    const std::vector<Anchor>& getAnchors() const { return anchors; }

private:
    struct Location { const Anchor* lo; const Anchor* hi; float t; };

    Location locate (float knob) const
    {
        knob = juce::jlimit (0.0f, 1.0f, knob);

        for (size_t i = 1; i < anchors.size(); ++i)
        {
            const auto& lo = anchors[i - 1];
            const auto& hi = anchors[i];
            if (knob <= hi.at)
                return { &lo, &hi, (knob - lo.at) / (hi.at - lo.at) };
        }

        // Past the last anchor (or a single-anchor table): hold the last one.
        return { &anchors.back(), &anchors.back(), 0.0f };
    }

    double valueAt (const Anchor& anchor, const juce::Identifier& param, double fallback) const
    {
        if (auto* v = anchor.params.getVarPointer (param))
            return (double) *v;
        if (auto* v = defaults.getVarPointer (param))
            return (double) *v;
        return fallback;
    }

    juce::String         macro;
    int                  version = 0;
    juce::NamedValueSet  defaults;
    std::vector<Anchor>  anchors;
};

// Holds the live table for one macro and lets a debug build swap in a new one
// when the JSON file changes on disk. The audio thread reads through an atomic
// pointer; replaced tables are kept alive until destruction, so a reader never
// sees a deleted table. Hot-reload is a development tool, so that small leak is fine.
class ZoneTableHolder
{
public:
    explicit ZoneTableHolder (ZoneTable initial)
    {
        install (std::move (initial));
    }

    const ZoneTable& get() const noexcept { return *current.load (std::memory_order_acquire); }

    // Message thread only.
    void install (ZoneTable table)
    {
        owned.push_back (std::make_unique<ZoneTable> (std::move (table)));
        current.store (owned.back().get(), std::memory_order_release);
    }

    // Message thread only. Returns true if the file changed and parsed; on a parse
    // error the old table stays live and the error is written to `error`.
    bool reloadIfChanged (const juce::File& file, juce::String* error = nullptr)
    {
        if (! file.existsAsFile())
            return false;

        const auto modified = file.getLastModificationTime();
        if (modified == lastModified)
            return false;

        lastModified = modified;

        if (auto table = ZoneTable::fromJson (file.loadFileAsString(), error))
        {
            install (std::move (*table));
            return true;
        }
        return false;
    }

private:
    std::vector<std::unique_ptr<ZoneTable>> owned;
    std::atomic<const ZoneTable*>           current { nullptr };
    juce::Time                              lastModified;
};

} // namespace silo
