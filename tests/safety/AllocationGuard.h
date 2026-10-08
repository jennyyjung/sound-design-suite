#pragma once

#include <cstddef>

// Counts heap allocations made on the current thread while a guard is alive.
// Used to prove processBlock never allocates.
namespace silo::test
{
struct AllocationGuard
{
    AllocationGuard();
    ~AllocationGuard();
    static std::size_t count();
};
}
