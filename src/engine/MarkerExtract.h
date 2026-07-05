#pragma once

#include <cstddef>

class SourceFile;

// Parses one captured stderr chunk from the compiler/interpreter into an
// editor error marker on `source_file` (clearing any previous markers).
// The line number is taken from the text after the first ':' (clang
// diagnostics look like "input_line_29:3:10: error: ..."), `offset` is added,
// and the result is clamped to [1, source_file.lines]. ANSI color codes are
// stripped from the stored text.
//
// Behavior is pinned by test/MarkerExtractTest.cpp — extend the tests first
// if you change it.
void extractMarkers(SourceFile &source_file, const char *buf, size_t size, size_t offset = 0);
