// Tests for ipc::Writer/ipc::Reader (src/ipc/Serialize.h), written before the
// implementation (step 2 of docs/plans/client-server-refactor.md). The
// contract under test:
//
//  - a payload is a trivially-copyable fixed struct followed by variable
//    tails (strings, repeated sub-blocks), appended and read in order
//  - Reader is bounds-checked: any read past the end fails, flips ok() to
//    false, and leaves all subsequent reads failing (no crash, no overread)
//  - zero-length strings and empty payloads are valid
#include <cppunit/extensions/HelperMacros.h>

#include "ipc/Protocol.h"
#include "ipc/Serialize.h"

#include <string>

class SerializeTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SerializeTest);
    CPPUNIT_TEST(testPodRoundTrip);
    CPPUNIT_TEST(testStringTail);
    CPPUNIT_TEST(testCompileResultWithMarkers);
    CPPUNIT_TEST(testReaderBoundsCheck);
    CPPUNIT_TEST(testReaderStaysFailedAfterOverread);
    CPPUNIT_TEST(testEmptyString);
    CPPUNIT_TEST_SUITE_END();

public:
    void testPodRoundTrip()
    {
        ipc::HelloMsg in{ipc::kProtocolVersion, ipc::kTransportShm, 2.0f, 1728, 1080};
        ipc::Writer w;
        w.append(in);
        CPPUNIT_ASSERT_EQUAL(uint32_t(sizeof(in)), w.size());

        ipc::Reader r(w.data(), w.size());
        ipc::HelloMsg out{};
        CPPUNIT_ASSERT(r.read(out));
        CPPUNIT_ASSERT(r.ok());
        CPPUNIT_ASSERT_EQUAL(size_t(0), r.remaining());
        CPPUNIT_ASSERT_EQUAL(in.protocol_version, out.protocol_version);
        CPPUNIT_ASSERT_EQUAL(in.transport_caps, out.transport_caps);
        CPPUNIT_ASSERT_EQUAL(in.dpi_scale, out.dpi_scale);
        CPPUNIT_ASSERT_EQUAL(in.design_w, out.design_w);
        CPPUNIT_ASSERT_EQUAL(in.design_h, out.design_h);
    }

    void testStringTail()
    {
        const std::string text = "ImGui::Text(\"hello\"); [](){}";
        ipc::SetSourceMsg in{};
        in.slide = 3;
        in.request_id = 42;
        in.is_cuda = 0;
        in.text_len = static_cast<uint32_t>(text.size());

        ipc::Writer w;
        w.append(in);
        w.appendString(text);

        ipc::Reader r(w.data(), w.size());
        ipc::SetSourceMsg out{};
        std::string out_text;
        CPPUNIT_ASSERT(r.read(out));
        CPPUNIT_ASSERT(r.readString(out_text, out.text_len));
        CPPUNIT_ASSERT(r.ok());
        CPPUNIT_ASSERT_EQUAL(size_t(0), r.remaining());
        CPPUNIT_ASSERT_EQUAL(int32_t(3), out.slide);
        CPPUNIT_ASSERT_EQUAL(uint64_t(42), out.request_id);
        CPPUNIT_ASSERT_EQUAL(text, out_text);
    }

    void testCompileResultWithMarkers()
    {
        // The most tail-heavy message: three strings then a repeated
        // sub-block, each marker with its own text tail.
        const std::string value = "(lambda)";
        const std::string exception;
        const std::string stderr_text = "input_line_4:2:5: error: boom";
        struct { uint32_t line; std::string text; } markers[2] = {
            {2, "error: boom"}, {7, "note: expanded from macro"}};

        ipc::CompileResultMsg in{};
        in.slide = 0;
        in.request_id = 7;
        in.validated = in.compiled = in.has_function = 1;
        in.syntax_error = 0;
        in.value_len = static_cast<uint32_t>(value.size());
        in.exception_len = static_cast<uint32_t>(exception.size());
        in.stderr_len = static_cast<uint32_t>(stderr_text.size());
        in.num_markers = 2;

        ipc::Writer w;
        w.append(in);
        w.appendString(value);
        w.appendString(exception);
        w.appendString(stderr_text);
        for (const auto &m : markers) {
            ipc::ErrorMarkerWire mw{m.line, static_cast<uint32_t>(m.text.size())};
            w.append(mw);
            w.appendString(m.text);
        }

        ipc::Reader r(w.data(), w.size());
        ipc::CompileResultMsg out{};
        CPPUNIT_ASSERT(r.read(out));
        std::string out_value, out_exception, out_stderr;
        CPPUNIT_ASSERT(r.readString(out_value, out.value_len));
        CPPUNIT_ASSERT(r.readString(out_exception, out.exception_len));
        CPPUNIT_ASSERT(r.readString(out_stderr, out.stderr_len));
        CPPUNIT_ASSERT_EQUAL(value, out_value);
        CPPUNIT_ASSERT_EQUAL(exception, out_exception);
        CPPUNIT_ASSERT_EQUAL(stderr_text, out_stderr);
        CPPUNIT_ASSERT_EQUAL(uint32_t(2), out.num_markers);
        for (uint32_t i = 0; i < out.num_markers; ++i) {
            ipc::ErrorMarkerWire mw{};
            std::string mtext;
            CPPUNIT_ASSERT(r.read(mw));
            CPPUNIT_ASSERT(r.readString(mtext, mw.text_len));
            CPPUNIT_ASSERT_EQUAL(markers[i].line, mw.line);
            CPPUNIT_ASSERT_EQUAL(markers[i].text, mtext);
        }
        CPPUNIT_ASSERT(r.ok());
        CPPUNIT_ASSERT_EQUAL(size_t(0), r.remaining());
    }

    void testReaderBoundsCheck()
    {
        // A truncated payload must fail cleanly, not overread.
        ipc::Writer w;
        w.append(uint32_t(5));
        ipc::Reader r(w.data(), w.size());
        uint64_t too_big = 0;
        CPPUNIT_ASSERT(!r.read(too_big));
        CPPUNIT_ASSERT(!r.ok());
    }

    void testReaderStaysFailedAfterOverread()
    {
        // A hostile/corrupt length field poisons the whole reader: even
        // reads that would fit must fail after the first overread.
        ipc::Writer w;
        w.append(uint32_t(0xffffffff));  // pretend length
        w.append(uint32_t(123));
        ipc::Reader r(w.data(), w.size());
        uint32_t len = 0;
        CPPUNIT_ASSERT(r.read(len));
        std::string s;
        CPPUNIT_ASSERT(!r.readString(s, len));  // 4 GB string: overread
        CPPUNIT_ASSERT(!r.ok());
        uint32_t fits = 0;
        CPPUNIT_ASSERT(!r.read(fits));  // would fit, but reader is poisoned
    }

    void testEmptyString()
    {
        ipc::Writer w;
        w.appendString("");
        CPPUNIT_ASSERT_EQUAL(uint32_t(0), w.size());
        ipc::Reader r(w.data(), w.size());
        std::string s = "sentinel";
        CPPUNIT_ASSERT(r.readString(s, 0));
        CPPUNIT_ASSERT(r.ok());
        CPPUNIT_ASSERT_EQUAL(std::string(), s);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SerializeTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(SerializeTest, "SerializeTest");
