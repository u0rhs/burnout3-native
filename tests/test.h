// Minimal test harness (no dependencies): TEST(name) { CHECK(...); } and a main that runs them all.
#ifndef B3_TESTS_TEST_H
#define B3_TESTS_TEST_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace t {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
}
inline std::uint32_t fbits(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return b; }
inline float bitsf(std::uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
}  // namespace t

#define TEST(name) static void name(); static t::Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { ++t::checks(); if (!(cond)) t::fail(__FILE__, __LINE__, #cond); } while (0)
#define CHECK_BITS(expr, expected) do { ++t::checks(); const std::uint32_t got_ = (expr), exp_ = (expected); \
    if (got_ != exp_) { char m_[200]; std::snprintf(m_, sizeof m_, "%s = 0x%08X, expected 0x%08X", #expr, got_, exp_); t::fail(__FILE__, __LINE__, m_); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++t::checks(); const double a_ = (a), b_ = (b); if (!(std::fabs(a_ - b_) <= (tol))) { \
    char m_[200]; std::snprintf(m_, sizeof m_, "%s = %.9g, expected %.9g (tol %g)", #a, a_, b_, (double)(tol)); t::fail(__FILE__, __LINE__, m_); } } while (0)
#define CHECK_THROWS(stmt) do { ++t::checks(); bool thrown_ = false; try { stmt; } catch (...) { thrown_ = true; } \
    if (!thrown_) t::fail(__FILE__, __LINE__, "expected an exception: " #stmt); } while (0)

#define TEST_MAIN(title) int main() { \
    std::printf("%s\n", title); \
    for (auto& c : t::cases()) { const int before = t::failures(); \
        try { c.fn(); } catch (const std::exception& e) { t::fail(__FILE__, __LINE__, std::string("exception: ") + e.what()); } \
        std::printf("  %-44s %s\n", c.name, t::failures() == before ? "ok" : "FAILED"); } \
    std::printf("%zu tests, %d checks, %d failures\n", t::cases().size(), t::checks(), t::failures()); \
    return t::failures() ? 1 : 0; }

#endif
