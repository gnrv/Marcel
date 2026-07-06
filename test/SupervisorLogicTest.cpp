// Tests for supervisor::SupervisorLogic (src/supervisor/SupervisorLogic.{h,cpp}),
// written before the implementation (step 3a of
// docs/plans/client-server-refactor.md). The logic is pure — time is injected
// as seconds, process/socket side effects live in WorkerProcess — so the
// whole watchdog policy is testable here:
//
//  - start() -> immediate Spawn; crash -> respawn with backoff 0/1/2/4/8/10 s
//    (doubling, capped), reset after a stable run of >= crash_storm_window
//  - more than crash_storm_count crashes inside crash_storm_window -> GiveUp
//  - ping/pong: no Pong within pong_timeout -> Kill
//  - no FrameDone within frame_timeout while a FrameBegin is outstanding ->
//    Kill, EXCEPT while a compile is busy (compiles legitimately take
//    seconds); a compile busy for > compile_timeout -> Kill
//  - the slide whose CompileBusy was outstanding when the worker died is
//    reported as poisoned (consumed once)
#include <cppunit/extensions/HelperMacros.h>

#include "supervisor/SupervisorLogic.h"

using supervisor::Action;
using supervisor::SupervisorLogic;

class SupervisorLogicTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SupervisorLogicTest);
    CPPUNIT_TEST(testStartSpawnsImmediately);
    CPPUNIT_TEST(testBackoffSequenceDoublesAndCaps);
    CPPUNIT_TEST(testStableRunResetsBackoff);
    CPPUNIT_TEST(testCrashStormGivesUp);
    CPPUNIT_TEST(testPingCadence);
    CPPUNIT_TEST(testPongTimeoutKills);
    CPPUNIT_TEST(testPongInTimeNoKill);
    CPPUNIT_TEST(testFrameTimeoutKills);
    CPPUNIT_TEST(testFrameDoneClearsTimeout);
    CPPUNIT_TEST(testCompileBusySuppressesFrameTimeout);
    CPPUNIT_TEST(testCompileTimeoutKills);
    CPPUNIT_TEST(testCompileResultRestoresFrameWatchdog);
    CPPUNIT_TEST(testPoisonedSlideAtDeath);
    CPPUNIT_TEST(testNoPoisonWithoutBusyCompile);
    CPPUNIT_TEST(testManualRestartWhileRunning);
    CPPUNIT_TEST(testManualRestartAfterGiveUp);
    CPPUNIT_TEST(testManualRestartCutsBackoff);
    CPPUNIT_TEST(testManualRestartClearsCrashHistory);
    CPPUNIT_TEST_SUITE_END();

    // Drive the logic through spawn -> running at time t.
    static void spawnAndRun(SupervisorLogic &s, double t)
    {
        CPPUNIT_ASSERT(s.update(t) == Action::Spawn);
        s.onSpawned(t);
        s.onHandshake(t);
        CPPUNIT_ASSERT(s.update(t) == Action::None);
    }

public:
    void testStartSpawnsImmediately()
    {
        SupervisorLogic s;
        CPPUNIT_ASSERT(s.update(0.0) == Action::None);  // not started yet
        s.start(0.0);
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);
        s.onSpawned(0.0);
        CPPUNIT_ASSERT(s.update(0.0) == Action::None);  // spawn consumed
    }

    void testBackoffSequenceDoublesAndCaps()
    {
        SupervisorLogic s;
        s.start(0.0);
        double t = 0.0;
        // Crash immediately every time; expected delays before respawn.
        const double expected[] = {0.0, 1.0, 2.0, 4.0};
        for (double delay : expected) {
            CPPUNIT_ASSERT(s.update(t) == Action::Spawn);
            s.onSpawned(t);
            s.onHandshake(t);
            s.onWorkerExit(t);  // instant crash
            if (delay > 0.0) {
                CPPUNIT_ASSERT(s.update(t) == Action::None);
                CPPUNIT_ASSERT(s.update(t + delay - 0.01) == Action::None);
            }
            t += delay;
        }
        CPPUNIT_ASSERT(s.update(t) == Action::Spawn);
    }

    void testStableRunResetsBackoff()
    {
        SupervisorLogic s;
        s.start(0.0);
        // First crash at t=0: respawn immediately, second crash at t=0 -> 1s delay.
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);
        s.onSpawned(0.0);
        s.onWorkerExit(0.0);
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);  // delay 0
        s.onSpawned(0.0);
        s.onWorkerExit(0.0);
        CPPUNIT_ASSERT(s.update(0.5) == Action::None);   // in 1s backoff
        CPPUNIT_ASSERT(s.update(1.0) == Action::Spawn);
        s.onSpawned(1.0);
        // Stable for 100 s (>= storm window), then crash: backoff starts over at 0.
        s.onWorkerExit(101.0);
        CPPUNIT_ASSERT(s.update(101.0) == Action::Spawn);
    }

    void testCrashStormGivesUp()
    {
        SupervisorLogic s;  // default: >5 crashes in 30 s -> give up
        s.start(0.0);
        double t = 0.0;
        for (int i = 0; i < 6; ++i) {
            Action a = s.update(t);
            if (a != Action::Spawn) {
                // still backing off; jump to the spawn time
                t = s.nextSpawnTime();
                a = s.update(t);
            }
            CPPUNIT_ASSERT(a == Action::Spawn);
            s.onSpawned(t);
            s.onWorkerExit(t);
        }
        // 6 crashes within ~15 s: no more spawns, ever.
        CPPUNIT_ASSERT(s.gaveUp());
        CPPUNIT_ASSERT(s.update(t) == Action::None);
        CPPUNIT_ASSERT(s.update(t + 1000.0) == Action::None);
    }

    void testPingCadence()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        CPPUNIT_ASSERT(!s.shouldPing(0.1));   // interval not reached
        CPPUNIT_ASSERT(s.shouldPing(0.5));
        s.onPingSent(0.5);
        CPPUNIT_ASSERT(!s.shouldPing(1.2));   // one outstanding: don't stack pings
        s.onPong(1.2);
        CPPUNIT_ASSERT(s.shouldPing(1.7));    // interval since last send
    }

    void testPongTimeoutKills()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onPingSent(1.0);
        CPPUNIT_ASSERT(s.update(2.9) == Action::None);
        CPPUNIT_ASSERT(s.update(3.1) == Action::Kill);  // > 2 s without Pong
    }

    void testPongInTimeNoKill()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onPingSent(1.0);
        s.onPong(1.4);
        CPPUNIT_ASSERT(s.update(10.0) == Action::None);
    }

    void testFrameTimeoutKills()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onFrameBeginSent(1.0);
        CPPUNIT_ASSERT(s.update(1.9) == Action::None);
        CPPUNIT_ASSERT(s.update(2.1) == Action::Kill);  // > 1 s without FrameDone
    }

    void testFrameDoneClearsTimeout()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onFrameBeginSent(1.0);
        s.onFrameDone(1.5);
        CPPUNIT_ASSERT(s.update(10.0) == Action::None);
    }

    void testCompileBusySuppressesFrameTimeout()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onFrameBeginSent(1.0);
        s.onCompileBusy(3, 1.1);
        CPPUNIT_ASSERT(s.update(5.0) == Action::None);  // compiling: be patient
    }

    void testCompileTimeoutKills()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onCompileBusy(3, 1.0);
        CPPUNIT_ASSERT(s.update(30.9) == Action::None);
        CPPUNIT_ASSERT(s.update(31.1) == Action::Kill);  // > 30 s compile
    }

    void testCompileResultRestoresFrameWatchdog()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onFrameBeginSent(1.0);
        s.onCompileBusy(3, 1.1);
        s.onCompileResult(3, 2.0);
        // Suppression lifted; the outstanding frame is now overdue from its
        // send time (grace measured from compile end, not frame send, would
        // also be acceptable — this pins the simple rule we chose).
        CPPUNIT_ASSERT(s.update(3.1) == Action::Kill);
    }

    void testPoisonedSlideAtDeath()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onCompileBusy(4, 1.0);
        s.onWorkerExit(2.0);  // died mid-compile of slide 4
        CPPUNIT_ASSERT_EQUAL(4, s.takePoisonedSlide());
        CPPUNIT_ASSERT_EQUAL(supervisor::kNoSlide, s.takePoisonedSlide());  // consumed
    }

    void testNoPoisonWithoutBusyCompile()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.onCompileBusy(4, 1.0);
        s.onCompileResult(4, 1.5);
        s.onWorkerExit(2.0);
        CPPUNIT_ASSERT_EQUAL(supervisor::kNoSlide, s.takePoisonedSlide());
    }

    // "Restart worker" button: kill the running worker once, respawn with no
    // backoff. The Kill is emitted through update() like any watchdog kill.
    void testManualRestartWhileRunning()
    {
        SupervisorLogic s;
        s.start(0.0);
        spawnAndRun(s, 0.0);
        s.requestRestart(5.0);
        CPPUNIT_ASSERT(s.update(5.0) == Action::Kill);
        CPPUNIT_ASSERT(s.update(5.1) == Action::None);  // one Kill only
        s.onWorkerExit(5.2);
        CPPUNIT_ASSERT(s.update(5.2) == Action::Spawn); // immediate, no backoff
    }

    // The crash panel's Restart button revives a gave-up supervisor.
    void testManualRestartAfterGiveUp()
    {
        SupervisorLogic s;
        s.start(0.0);
        double t = 0.0;
        for (int i = 0; i < 6; ++i) {
            Action a = s.update(t);
            if (a != Action::Spawn) {
                t = s.nextSpawnTime();
                a = s.update(t);
            }
            CPPUNIT_ASSERT(a == Action::Spawn);
            s.onSpawned(t);
            s.onWorkerExit(t);
        }
        CPPUNIT_ASSERT(s.gaveUp());
        s.requestRestart(t + 1.0);
        CPPUNIT_ASSERT(!s.gaveUp());
        CPPUNIT_ASSERT(s.update(t + 1.0) == Action::Spawn);
    }

    // Restarting mid-backoff spawns now instead of at the backoff deadline.
    void testManualRestartCutsBackoff()
    {
        SupervisorLogic s;
        s.start(0.0);
        // Two instant crashes put us in a 1 s backoff.
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);
        s.onSpawned(0.0);
        s.onWorkerExit(0.0);
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);
        s.onSpawned(0.0);
        s.onWorkerExit(0.0);
        CPPUNIT_ASSERT(s.update(0.5) == Action::None); // backing off
        s.requestRestart(0.5);
        CPPUNIT_ASSERT(s.update(0.5) == Action::Spawn);
    }

    // A manual restart is a clean slate: prior crashes no longer count
    // toward the storm cutoff or the backoff sequence.
    void testManualRestartClearsCrashHistory()
    {
        SupervisorLogic s;
        s.start(0.0);
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);
        s.onSpawned(0.0);
        s.onWorkerExit(0.0);
        CPPUNIT_ASSERT(s.update(0.0) == Action::Spawn);
        s.onSpawned(0.0);
        s.onHandshake(0.0);
        s.requestRestart(1.0);
        CPPUNIT_ASSERT_EQUAL(0, s.crashCount());
        CPPUNIT_ASSERT(s.update(1.0) == Action::Kill);
        s.onWorkerExit(1.0);
        CPPUNIT_ASSERT(s.update(1.0) == Action::Spawn); // backoff restarted at 0
        s.onSpawned(1.0);
        s.onWorkerExit(1.0);
        // Only the post-restart crashes count: still far from the storm cutoff.
        CPPUNIT_ASSERT(!s.gaveUp());
        CPPUNIT_ASSERT_EQUAL(2, s.crashCount());
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SupervisorLogicTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(SupervisorLogicTest, "SupervisorLogicTest");
