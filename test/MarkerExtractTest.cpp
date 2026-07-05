// Characterization tests for extractMarkers(), written BEFORE the function
// moves from src/main.cpp into src/engine/MarkerExtract.{h,cpp} (step 1 of
// docs/plans/client-server-refactor.md). These pin the current behavior so
// the code motion into ClingEngine is provably behavior-preserving:
//
//  - the line number is parsed from the text after the FIRST ':' in the
//    buffer (clang stderr like "input_line_29:3:10: error: ..." -> line 3)
//  - non-numeric / missing line info falls back to line 1
//  - line is clamped to [1, source_file.lines] (offset added before clamping)
//  - ANSI color codes are stripped from the stored marker text
//  - each call clears previously stored markers (single-marker semantics)
#include <cppunit/extensions/HelperMacros.h>

#include "engine/MarkerExtract.h"
#include "Presentation.h"

#include <filesystem>
#include <string>

class MarkerExtractTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(MarkerExtractTest);
    CPPUNIT_TEST(testClangDiagnosticLine);
    CPPUNIT_TEST(testAnsiCodesStripped);
    CPPUNIT_TEST(testClampToLineCount);
    CPPUNIT_TEST(testClampToMinimumOne);
    CPPUNIT_TEST(testNonNumericFallsBackToLineOne);
    CPPUNIT_TEST(testNoColonFallsBackToLineOne);
    CPPUNIT_TEST(testOffsetAddedBeforeClamp);
    CPPUNIT_TEST(testEachCallClearsPreviousMarkers);
    CPPUNIT_TEST_SUITE_END();

    std::filesystem::path tmp_path;
    SourceFile *sf = nullptr;

public:
    void setUp() override
    {
        tmp_path = std::filesystem::temp_directory_path() / "marcel_marker_test_slide.cpp";
        sf = new SourceFile(tmp_path);
        sf->lines = 10;
    }

    void tearDown() override
    {
        delete sf;
        sf = nullptr;
        std::filesystem::remove(tmp_path);
    }

    static void extract(SourceFile &f, const std::string &text, size_t offset = 0)
    {
        extractMarkers(f, text.data(), text.size(), offset);
    }

    void testClangDiagnosticLine()
    {
        extract(*sf, "input_line_29:3:10: error: use of undeclared identifier 'foo'");
        CPPUNIT_ASSERT_EQUAL(size_t(1), sf->error_markers.size());
        CPPUNIT_ASSERT_EQUAL(3, sf->error_markers.begin()->first);
        CPPUNIT_ASSERT_EQUAL(std::string("input_line_29:3:10: error: use of undeclared identifier 'foo'"),
                             sf->error_markers.begin()->second);
    }

    void testAnsiCodesStripped()
    {
        extract(*sf, "\033[1minput_line_4:2:5: \033[0;1;31merror: \033[0mbad thing");
        CPPUNIT_ASSERT_EQUAL(size_t(1), sf->error_markers.size());
        CPPUNIT_ASSERT_EQUAL(2, sf->error_markers.begin()->first);
        CPPUNIT_ASSERT_EQUAL(std::string("input_line_4:2:5: error: bad thing"),
                             sf->error_markers.begin()->second);
    }

    void testClampToLineCount()
    {
        extract(*sf, "input_line_7:99:1: error: eof");
        CPPUNIT_ASSERT_EQUAL(10, sf->error_markers.begin()->first);
    }

    void testClampToMinimumOne()
    {
        extract(*sf, "x:0: something");
        CPPUNIT_ASSERT_EQUAL(1, sf->error_markers.begin()->first);
    }

    void testNonNumericFallsBackToLineOne()
    {
        extract(*sf, "error: something terrible: happened");
        CPPUNIT_ASSERT_EQUAL(1, sf->error_markers.begin()->first);
    }

    void testNoColonFallsBackToLineOne()
    {
        extract(*sf, "linker command failed");
        CPPUNIT_ASSERT_EQUAL(1, sf->error_markers.begin()->first);
    }

    void testOffsetAddedBeforeClamp()
    {
        extract(*sf, "x:4: error: e", /*offset=*/3);
        CPPUNIT_ASSERT_EQUAL(7, sf->error_markers.begin()->first);
        // offset pushing past the line count still clamps to lines
        extract(*sf, "x:4: error: e", /*offset=*/100);
        CPPUNIT_ASSERT_EQUAL(10, sf->error_markers.begin()->first);
    }

    void testEachCallClearsPreviousMarkers()
    {
        extract(*sf, "x:2: first");
        extract(*sf, "x:5: second");
        CPPUNIT_ASSERT_EQUAL(size_t(1), sf->error_markers.size());
        CPPUNIT_ASSERT_EQUAL(5, sf->error_markers.begin()->first);
        CPPUNIT_ASSERT_EQUAL(std::string("x:5: second"), sf->error_markers.begin()->second);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(MarkerExtractTest);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(MarkerExtractTest, "MarkerExtractTest");
