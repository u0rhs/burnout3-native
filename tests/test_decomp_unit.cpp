// A matched decomp source compiled UNCHANGED for the host through the port's GtMathPs2 headers (include/gtmath comes
// first on the include path, so its `#include "b3/GtMathPs2.h"` resolves here). The decomp file is read from
// $(DECOMP)/src (read-only); the Makefile skips this test when that tree is absent.
// Unit: src/b3/_unclassified/B3EnvironmentManager_002BCEC0.cpp, CB3EnvironmentManager::Construct() (0x2BCEC0).
#include "b3/GtMathPs2.h"
#include "test.h"

#include <cstddef>

#include B3_DECOMP_UNIT

// Callees of the unit that are still original code: stubs here (in a full build they would be call_guest thunks).
static int g_sky = 0, g_sun = 0;
extern "C" void Construct__18CB3SkyDomeRendererFv(void*) { ++g_sky; }
void CB3SunRenderer::Construct() { ++g_sun; }

static_assert(offsetof(CB3EnvironmentManager, field_80) == 0x80 && offsetof(CB3EnvironmentManager, field_148) == 0x148,
              "the decomp's recovered layout holds on the host");

TEST(environment_manager_construct) {
    alignas(16) static CB3EnvironmentManager m;
    m.Construct();
    CHECK(g_sky == 1 && g_sun == 1);
    CHECK(m.field_140 == 1 && m.field_148 == 0 && m.field_130 == -1);
#if B3_PS2_FLOAT
    // (1,1,1) normalised on the VU: tools/vuref.py -> 0x3F13CD3A per lane
    CHECK_BITS(m.field_80.mData.w[0], 0x3F13CD3Au);
    CHECK_BITS(m.field_80.mData.w[1], 0x3F13CD3Au);
    CHECK_BITS(m.field_80.mData.w[2], 0x3F13CD3Au);
#else
    CHECK_NEAR(b3vu::lane(m.field_80.mData, 0), 0.57735026, 1e-7);
#endif
}

TEST_MAIN("decomp unit through the port headers")
