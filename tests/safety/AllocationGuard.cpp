#include "AllocationGuard.h"

#include <cstdlib>
#include <new>

namespace
{
thread_local bool        counting    = false;
thread_local std::size_t allocations = 0;
}

namespace silo::test
{
AllocationGuard::AllocationGuard()  { allocations = 0; counting = true; }
AllocationGuard::~AllocationGuard() { counting = false; }
std::size_t AllocationGuard::count() { return allocations; }
}

void* operator new (std::size_t size)
{
    if (counting) ++allocations;
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
