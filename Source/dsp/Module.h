#pragma once

namespace silo
{

// What every DSP module tells the processor about itself, so plugin-level facts
// (tail length, latency) come from the modules instead of being hard-coded.
//
// Both may change with the module's settings (a longer reverb decay, a
// lookahead switched on), so they're read live and must be safe to call from
// any thread.
class Module
{
public:
    virtual ~Module() = default;

    // How long the module keeps producing sound after its input goes silent.
    virtual double getTailSeconds() const { return 0.0; }

    // How many samples late the module's output is relative to its input.
    virtual int getLatencySamples() const { return 0; }
};

} // namespace silo
