#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace silo
{

// The one list of macros. Parameters, zone tables, hot-reload, editor knobs and
// the safety tests are all built from it, so adding a macro is one line here
// plus its tuning/<id>.json file (picked up by CMake automatically).
struct MacroDef
{
    std::string_view id;     // parameter ID, also the tuning file's name and its "macro" field
    std::string_view name;   // shown in the UI and the host
};

inline constexpr std::array macros {
    MacroDef { "gate_chop", "Gate / Chop" },
    MacroDef { "width_pan", "Width / Auto-pan" },
    MacroDef { "space",     "Space" },
};

inline constexpr std::size_t numMacros = macros.size();

// Index of a macro by ID; fails to compile when used in a constant expression
// with an unknown ID.
constexpr std::size_t macroIndex (std::string_view id)
{
    for (std::size_t i = 0; i < macros.size(); ++i)
        if (macros[i].id == id)
            return i;
    throw "unknown macro id";
}

} // namespace silo
