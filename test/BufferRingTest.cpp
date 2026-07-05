// Tests for ipc::BufferRing (src/ipc/BufferRing.h), written before the
// implementation (step 3b of docs/plans/client-server-refactor.md). One ring
// tracks the kBuffersPerSlide (3) render buffers of a single slide on the
// worker side:
//
//  - acquire() hands out a FREE buffer and marks it busy (INFLIGHT/HELD is
//    the peer's business — from here they're both just "not ours")
//  - no FREE buffer -> kInvalid: the worker skips the slide this frame and
//    the main process keeps showing the front buffer (never tears)
//  - release() (BufferRelease from the peer) makes a buffer FREE again
//  - reset() frees everything (worker restart)
#include <cppunit/extensions/HelperMacros.h>

#include "ipc/BufferRing.h"

#include <set>

class BufferRingTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(BufferRingTest);
    CPPUNIT_TEST(testAcquireAllThenStarve);
    CPPUNIT_TEST(testReleaseMakesBufferAvailable);
    CPPUNIT_TEST(testResetFreesEverything);
    CPPUNIT_TEST(testOutOfRangeReleaseIgnored);
    CPPUNIT_TEST(testDoubleReleaseHarmless);
    CPPUNIT_TEST_SUITE_END();

public:
    void testAcquireAllThenStarve()
    {
        ipc::BufferRing ring;
        CPPUNIT_ASSERT_EQUAL(ipc::kBuffersPerSlide, ring.freeCount());
        std::set<int> got;
        for (uint32_t i = 0; i < ipc::kBuffersPerSlide; ++i) {
            int b = ring.acquire();
            CPPUNIT_ASSERT(b >= 0 && b < static_cast<int>(ipc::kBuffersPerSlide));
            got.insert(b);
        }
        CPPUNIT_ASSERT_EQUAL(size_t(ipc::kBuffersPerSlide), got.size()); // all distinct
        CPPUNIT_ASSERT_EQUAL(uint32_t(0), ring.freeCount());
        CPPUNIT_ASSERT_EQUAL(ipc::BufferRing::kInvalid, ring.acquire());
    }

    void testReleaseMakesBufferAvailable()
    {
        ipc::BufferRing ring;
        while (ring.acquire() != ipc::BufferRing::kInvalid) {
        }
        ring.release(1);
        CPPUNIT_ASSERT_EQUAL(uint32_t(1), ring.freeCount());
        CPPUNIT_ASSERT_EQUAL(1, ring.acquire());
        CPPUNIT_ASSERT_EQUAL(ipc::BufferRing::kInvalid, ring.acquire());
    }

    void testResetFreesEverything()
    {
        ipc::BufferRing ring;
        ring.acquire();
        ring.acquire();
        ring.reset();
        CPPUNIT_ASSERT_EQUAL(ipc::kBuffersPerSlide, ring.freeCount());
    }

    void testOutOfRangeReleaseIgnored()
    {
        ipc::BufferRing ring;
        // A malicious/stale BufferRelease must not corrupt the ring.
        ring.release(ipc::kBuffersPerSlide);
        ring.release(1000000);
        CPPUNIT_ASSERT_EQUAL(ipc::kBuffersPerSlide, ring.freeCount());
    }

    void testDoubleReleaseHarmless()
    {
        ipc::BufferRing ring;
        int b = ring.acquire();
        ring.release(static_cast<uint32_t>(b));
        ring.release(static_cast<uint32_t>(b));
        CPPUNIT_ASSERT_EQUAL(ipc::kBuffersPerSlide, ring.freeCount());
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BufferRingTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(BufferRingTest, "BufferRingTest");
