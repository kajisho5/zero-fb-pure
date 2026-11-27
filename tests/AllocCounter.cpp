#include "AllocCounter.h"
#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
std::atomic<bool> counting { false };
std::atomic<std::size_t> allocations { 0 };

void* allocate (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size)   { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t& tag) noexcept { return operator new (size, tag); }
void operator delete (void* p, const std::nothrow_t&) noexcept   { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete (void* p) noexcept                { std::free (p); }
void operator delete[] (void* p) noexcept              { std::free (p); }
void operator delete (void* p, std::size_t) noexcept   { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace zfptest
{
AllocScope::AllocScope() noexcept  { allocations.store (0); counting.store (true); }
AllocScope::~AllocScope() noexcept { counting.store (false); }
std::size_t AllocScope::count() noexcept { return allocations.load(); }
} // namespace zfptest
