// Tests for SlideInputMap (src/render/SlideInputMap.h), written before the
// implementation (step 3b of docs/plans/client-server-refactor.md). The map
// is the single affine transform between screen coordinates of the displayed
// ImGui::Image and the worker's design-resolution slide-local coordinates:
//
//  - toDesign() maps the image rect onto [0,design_w]x[0,design_h]
//  - contains() is true inside the displayed rect (top/left edge inclusive,
//    bottom/right exclusive — pixel convention)
//  - a degenerate (zero-sized) image contains nothing and maps to 0,0
//    rather than dividing by zero
#include <cppunit/extensions/HelperMacros.h>

#include "render/SlideInputMap.h"

class InputMapTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(InputMapTest);
    CPPUNIT_TEST(testIdentityWhenSameSize);
    CPPUNIT_TEST(testScalesToDesignResolution);
    CPPUNIT_TEST(testOffsetSubtracted);
    CPPUNIT_TEST(testContains);
    CPPUNIT_TEST(testDegenerateImage);
    CPPUNIT_TEST_SUITE_END();

    static void assertMaps(const SlideInputMap &m, float sx, float sy,
                           float ex, float ey)
    {
        float dx = -1, dy = -1;
        m.toDesign(sx, sy, dx, dy);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(ex, dx, 1e-3);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(ey, dy, 1e-3);
    }

public:
    void testIdentityWhenSameSize()
    {
        SlideInputMap m{0, 0, 1728, 1080, 1728, 1080};
        assertMaps(m, 0, 0, 0, 0);
        assertMaps(m, 864, 540, 864, 540);
        assertMaps(m, 1728, 1080, 1728, 1080);
    }

    void testScalesToDesignResolution()
    {
        // Slide displayed at half size: screen deltas double in design space.
        SlideInputMap m{0, 0, 864, 540, 1728, 1080};
        assertMaps(m, 432, 270, 864, 540);
        assertMaps(m, 864, 540, 1728, 1080);
    }

    void testOffsetSubtracted()
    {
        SlideInputMap m{100, 200, 864, 540, 1728, 1080};
        assertMaps(m, 100, 200, 0, 0);       // image top-left -> design origin
        assertMaps(m, 532, 470, 864, 540);   // image center -> design center
    }

    void testContains()
    {
        SlideInputMap m{100, 200, 864, 540, 1728, 1080};
        CPPUNIT_ASSERT(m.contains(100, 200));        // top-left inclusive
        CPPUNIT_ASSERT(m.contains(500, 400));
        CPPUNIT_ASSERT(!m.contains(99, 400));        // left of image
        CPPUNIT_ASSERT(!m.contains(500, 199));       // above image
        CPPUNIT_ASSERT(!m.contains(964, 400));       // right edge exclusive
        CPPUNIT_ASSERT(!m.contains(500, 740));       // bottom edge exclusive
    }

    void testDegenerateImage()
    {
        SlideInputMap m{100, 200, 0, 0, 1728, 1080};
        CPPUNIT_ASSERT(!m.contains(100, 200));
        assertMaps(m, 100, 200, 0, 0); // no division by zero
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(InputMapTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(InputMapTest, "InputMapTest");
