// The title's calloc contract, including its direct-memory and mmap routes.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void *__wrap_calloc(std::size_t, std::size_t);
extern "C" void __wrap_free(void *);

namespace
{
bool refuse_overflow = false;
unsigned native_callocs = 0;
void *overflow = nullptr;
std::size_t overflow_capacity = 0;
bool overflow_in_use = false;

bool check_zero(std::size_t count, std::size_t size, const char *name)
{
    auto *p = static_cast<unsigned char *>(__wrap_calloc(count, size));
    bool ok = p != nullptr;
    for (std::size_t i = 0; p && i < count * size; ++i)
        if (p[i] != 0)
        {
            ok = false;
            break;
        }
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL (not zero-initialized)");
    __wrap_free(p);
    return ok;
}
} // namespace

extern "C" void *__real_malloc(std::size_t n) { return std::malloc(n); }
extern "C" void *__real_calloc(std::size_t n, std::size_t s)
{
    ++native_callocs;
    return std::calloc(n, s);
}
extern "C" void *__real_realloc(void *p, std::size_t n) { return std::realloc(p, n); }
extern "C" void __real_free(void *p) { std::free(p); }

// A direct-memory heap can hand back a block containing its previous contents.
// Keep and dirty the block on free so the next allocation actually reuses it.
extern "C" void *ps5_overflow_malloc(std::size_t n)
{
    if (refuse_overflow || overflow_in_use)
        return nullptr;
    if (n > overflow_capacity)
    {
        void *grown = std::realloc(overflow, n);
        if (!grown)
            return nullptr;
        overflow = grown;
        overflow_capacity = n;
        std::memset(overflow, 0xa5, n);
    }
    overflow_in_use = true;
    return overflow;
}
extern "C" void *ps5_overflow_calloc(std::size_t n, std::size_t s)
{
    void *p = ps5_overflow_malloc(n * s);
    if (p)
        std::memset(p, 0, n * s);
    return p;
}
extern "C" int ps5_overflow_owns(const void *p) { return p && p == overflow; }
extern "C" void ps5_overflow_free(void *p)
{
    if (p == overflow)
    {
        std::memset(p, 0xa5, overflow_capacity);
        overflow_in_use = false;
    }
}
extern "C" void *ps5_overflow_realloc(void *, std::size_t) { return nullptr; }
extern "C" void *ps5_overflow_memalign(std::size_t, std::size_t) { return nullptr; }
#ifdef __APPLE__
// macOS has no malloc_usable_size. Its unrelated realloc fallback is not used.
extern "C" std::size_t malloc_usable_size(const void *) { return 0; }
#endif

int main()
{
    bool ok = true;
    ok &= check_zero(1, 1024 * 1024 - 1, "below threshold, direct heap");
    ok &= check_zero(1, 1024 * 1024, "at threshold, direct heap");
    ok &= check_zero(2, 1024 * 1024, "above threshold, direct heap");
    void *previous = overflow;
    ok &= check_zero(2, 1024 * 1024, "reused dirty direct-heap block");
    ok &= previous == overflow;

    refuse_overflow = true;
    const unsigned before = native_callocs;
    ok &= check_zero(1, 512, "small native fallback");
    ok &= native_callocs == before + 1;
    ok &= check_zero(2, 1024 * 1024, "large anonymous-mapping fallback");
    ok &= native_callocs == before + 1;

    errno = 0;
    void *too_big = __wrap_calloc(SIZE_MAX, 2);
    const bool rejected = !too_big && errno == ENOMEM;
    std::printf("multiplication overflow: %s\n", rejected ? "PASS" : "FAIL");
    ok &= rejected;
    __wrap_free(too_big);
    std::free(overflow);
    return ok ? 0 : 1;
}
