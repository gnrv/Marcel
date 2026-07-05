// Step-0 scaffolding suite: exercises the DpiInfo helper extracted from
// main.cpp. Deliberately small — it proves the CppUnit/CTest wiring works
// and pins the invariants detectDpi() must uphold on any host.
#include <cppunit/extensions/HelperMacros.h>

#include "system/DpiInfo.h"

class DpiInfoTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(DpiInfoTest);
    CPPUNIT_TEST(testInvariants);
    CPPUNIT_TEST_SUITE_END();

public:
    void testInvariants()
    {
        DpiInfo info = detectDpi();
        // window_size_scale_factor is 1.0 on all current paths
        CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, info.window_size_scale_factor, 1e-9);
        if (info.is_wsl2) {
            // WSL2 forces a fixed 2x scale (WSLg reports a fake 1.0)
            CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, info.dpi_scale, 1e-9);
        } else {
            // Non-WSL2 hosts get the default; callers refine via GLFW
            CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, info.dpi_scale, 1e-9);
        }
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(DpiInfoTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(DpiInfoTest, "DpiInfoTest");
