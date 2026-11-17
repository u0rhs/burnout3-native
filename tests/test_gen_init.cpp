// gen1 (determinism, 2026-11-07): the generated units' flags (Makefile GENFLAGS) zero every uninitialised local,
// including one of a class with a user-provided constructor. GCC's default -flifetime-dse=2 deleted that zero store
// (the constructor's start clobber), so UpdateSuspension 0x1CC900's CGtMatrix3x4 lScalingMat carried host stack bytes
// into the wheel matrices' w lanes in guest RAM, a new value per run. Built with $(GENFLAGS) at -O2.
#include "b3/GtMathPs2.h"
#include "test.h"

#include <cstring>

using namespace GtMathPs2;

namespace {
__attribute__((noinline)) void dirty() {
    volatile unsigned char b[8192];
    for (unsigned i = 0; i < sizeof b; ++i) b[i] = 0xA5;
}
alignas(16) unsigned g_out[16];
__attribute__((noinline)) void declared() { CGtMatrix3x4 l; std::memcpy(g_out, &l, sizeof l); }
__attribute__((noinline)) void identity_product(CGtMatrix3x4* m) { CGtMatrix3x4 l; l.SetIdentity(); *m = l * *m; }
struct WithCtor { int a, b; WithCtor() {} };   // a decomp class with a user-provided constructor
__attribute__((noinline)) int with_ctor() { WithCtor c; c.a = 1; return c.b; }
}  // namespace

TEST(locals_of_user_constructed_classes_are_zero) {
    dirty(); declared();
    for (unsigned w : g_out) CHECK(w == 0);
    alignas(16) static CGtMatrix3x4 m;
    std::memset(&m, 0, sizeof m);
    dirty(); identity_product(&m);
    const unsigned* p = reinterpret_cast<const unsigned*>(&m);
    CHECK(p[3] == 0 && p[7] == 0 && p[11] == 0 && p[15] == 0);   // the w lanes
    dirty(); CHECK(with_ctor() == 0);
}

TEST_MAIN("generated-unit flags: uninitialised locals")
