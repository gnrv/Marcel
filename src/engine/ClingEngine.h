#pragma once

#include "SlideEngine.h"

#include <memory>

namespace cling {
    class Interpreter;
}

// In-process Cling implementation of SlideEngine. Owns the interpreter that
// was historically a stack local in main() (src/main.cpp before the
// client-server refactor). Compiled only into targets that link Cling
// (marcel for now; marcel_worker later) — deliberately NOT part of
// marcel_lib, so the tests don't need LLVM.
class ClingEngine : public SlideEngine {
public:
    // Builds the interpreter from the program's argc/argv (adds CUDA flags
    // when built with USE_CUDA), adds the imgui/implot/imlatex include
    // paths, preloads the standard slide headers, and enables redefinition.
    ClingEngine(int argc, char **argv);
    ~ClingEngine() override;

    void compileSetup(SourceFile &setup) override;
    void compileSlide(SourceFile &slide) override;
    void dump(const char *what, const char *filter) override;

private:
    std::unique_ptr<cling::Interpreter> interp_;
};
