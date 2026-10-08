#pragma once

#include <string_view>

namespace silo
{

// Maps a knob position saved with an older tuning version onto the current
// table, when a session saved under `fromVersion` is loaded today.
//
// Rule: once a tuning file has shipped, any change to it bumps its "version"
// (the tuning lock test enforces this). If the change moves or renames zones,
// add a case here so old sessions land in the zone they were set to, e.g.
//
//     if (macro == "space" && fromVersion < 3)
//         return value * 0.8f;   // v3 inserted a "Chamber" zone before "Hall"
//
// Without a case the knob position is kept as is.
inline float migrateMacroValue (std::string_view macro, int fromVersion, int toVersion, float value)
{
    (void) macro; (void) fromVersion; (void) toVersion;
    return value;
}

} // namespace silo
