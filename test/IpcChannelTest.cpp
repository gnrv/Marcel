// Tests for ipc::Channel (src/ipc/Channel.{h,cpp}), written before the
// implementation (step 2 of docs/plans/client-server-refactor.md). The
// contract under test, over a loopback socketpair:
//
//  - one send() = one recv(): SOCK_SEQPACKET preserves message boundaries
//  - header type/payload_size and payload bytes arrive intact
//  - file descriptors pass via SCM_RIGHTS and refer to the same file
//  - recv() is non-blocking: WouldBlock on an empty socket
//  - peer death (Channel destroyed/closed) surfaces as Disconnected and
//    wait() wakes up on it (POLLHUP)
//  - payloads up to kMaxPayloadSize round-trip; larger sends are refused
#include <cppunit/extensions/HelperMacros.h>

#include "ipc/Channel.h"
#include "ipc/Protocol.h"

#include <cstring>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

class IpcChannelTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(IpcChannelTest);
    CPPUNIT_TEST(testSendRecvRoundTrip);
    CPPUNIT_TEST(testMessageBoundaries);
    CPPUNIT_TEST(testEmptyPayload);
    CPPUNIT_TEST(testWouldBlockWhenEmpty);
    CPPUNIT_TEST(testFdPassing);
    CPPUNIT_TEST(testDisconnectedOnPeerClose);
    CPPUNIT_TEST(testWaitWakesOnPeerDeath);
    CPPUNIT_TEST(testMaxPayloadRoundTrip);
    CPPUNIT_TEST(testOversizedSendRefused);
    CPPUNIT_TEST_SUITE_END();

    ipc::Channel a, b;

public:
    void setUp() override { CPPUNIT_ASSERT(ipc::Channel::createPair(a, b)); }
    void tearDown() override
    {
        a = ipc::Channel();
        b = ipc::Channel();
    }

    void testSendRecvRoundTrip()
    {
        ipc::PingMsg ping{0xdeadbeefcafef00d};
        CPPUNIT_ASSERT(a.send(ipc::MsgType::Ping, &ping, sizeof(ping)));

        ipc::Message m;
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Message);
        CPPUNIT_ASSERT(m.header.type == ipc::MsgType::Ping);
        CPPUNIT_ASSERT_EQUAL(uint32_t(sizeof(ping)), m.header.payload_size);
        CPPUNIT_ASSERT_EQUAL(size_t(sizeof(ping)), m.payload.size());
        ipc::PingMsg out{};
        std::memcpy(&out, m.payload.data(), sizeof(out));
        CPPUNIT_ASSERT_EQUAL(ping.token, out.token);
    }

    void testMessageBoundaries()
    {
        // Three different-sized messages must come out as three messages
        // with the original sizes — no coalescing, no splitting.
        std::string s1(10, 'x'), s2(100, 'y'), s3(1000, 'z');
        CPPUNIT_ASSERT(a.send(ipc::MsgType::LogText, s1.data(), s1.size()));
        CPPUNIT_ASSERT(a.send(ipc::MsgType::LogText, s2.data(), s2.size()));
        CPPUNIT_ASSERT(a.send(ipc::MsgType::LogText, s3.data(), s3.size()));

        for (const std::string *expect : {&s1, &s2, &s3}) {
            ipc::Message m;
            CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Message);
            CPPUNIT_ASSERT_EQUAL(expect->size(), m.payload.size());
            CPPUNIT_ASSERT(std::memcmp(expect->data(), m.payload.data(), expect->size()) == 0);
        }
    }

    void testEmptyPayload()
    {
        CPPUNIT_ASSERT(a.send(ipc::MsgType::Shutdown, nullptr, 0));
        ipc::Message m;
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Message);
        CPPUNIT_ASSERT(m.header.type == ipc::MsgType::Shutdown);
        CPPUNIT_ASSERT_EQUAL(size_t(0), m.payload.size());
    }

    void testWouldBlockWhenEmpty()
    {
        ipc::Message m;
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::WouldBlock);
        CPPUNIT_ASSERT(!b.wait(0));
    }

    void testFdPassing()
    {
        // Pass a memfd across; the receiver must see the same file content.
        int memfd = memfd_create("ipc_test", MFD_CLOEXEC);
        CPPUNIT_ASSERT(memfd >= 0);
        const char content[] = "framebuffer bytes";
        CPPUNIT_ASSERT_EQUAL(ssize_t(sizeof(content)), write(memfd, content, sizeof(content)));

        ipc::TextureAnnounceMsg ann{};
        ann.slide = 1;
        ann.transport = ipc::kTransportShm;
        CPPUNIT_ASSERT(a.send(ipc::MsgType::TextureAnnounce, &ann, sizeof(ann), &memfd, 1));
        close(memfd);

        ipc::Message m;
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Message);
        CPPUNIT_ASSERT_EQUAL(size_t(1), m.fds.size());
        int received = m.takeFd(0);
        CPPUNIT_ASSERT(received >= 0);
        char back[sizeof(content)] = {};
        CPPUNIT_ASSERT_EQUAL(ssize_t(sizeof(content)), pread(received, back, sizeof(back), 0));
        CPPUNIT_ASSERT(std::memcmp(content, back, sizeof(content)) == 0);
        close(received);
    }

    void testDisconnectedOnPeerClose()
    {
        // Queued data is still delivered after the peer closes; then EOF.
        ipc::PingMsg ping{1};
        CPPUNIT_ASSERT(a.send(ipc::MsgType::Ping, &ping, sizeof(ping)));
        a.close();

        ipc::Message m;
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Message);
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Disconnected);
    }

    void testWaitWakesOnPeerDeath()
    {
        a.close();
        CPPUNIT_ASSERT(b.wait(1000));  // POLLHUP, not a timeout
        ipc::Message m;
        CPPUNIT_ASSERT(b.recv(m) == ipc::Channel::RecvResult::Disconnected);
    }

    void testMaxPayloadRoundTrip()
    {
        std::vector<uint8_t> big(ipc::kMaxPayloadSize);
        for (size_t i = 0; i < big.size(); ++i)
            big[i] = static_cast<uint8_t>(i * 31 + 7);
        CPPUNIT_ASSERT(a.send(ipc::MsgType::LogText, big.data(), big.size()));

        ipc::Message m;
        CPPUNIT_ASSERT(b.recvBlocking(m) == ipc::Channel::RecvResult::Message);
        CPPUNIT_ASSERT_EQUAL(big.size(), m.payload.size());
        CPPUNIT_ASSERT(std::memcmp(big.data(), m.payload.data(), big.size()) == 0);
    }

    void testOversizedSendRefused()
    {
        std::vector<uint8_t> too_big(ipc::kMaxPayloadSize + 1);
        CPPUNIT_ASSERT(!a.send(ipc::MsgType::LogText, too_big.data(), too_big.size()));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(IpcChannelTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(IpcChannelTest, "IpcChannelTest");
