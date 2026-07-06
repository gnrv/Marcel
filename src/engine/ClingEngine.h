#pragma once

#include "SlideEngine.h"

#include <map>
#include <memory>

namespace cling {
    class Interpreter;
    class Transaction;
}

// Cling implementation of SlideEngine. Owns the interpreter that was
// historically a stack local in main() (src/main.cpp before the
// client-server refactor). Compiled only into marcel_worker — the one
// binary that links Cling/LLVM; deliberately NOT part of marcel_lib.
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
    // Per-slide transaction of the last render evaluation, reused via
    // reevaluate() on subsequent frames. Keyed by the SourceFile (stable:
    // WorkerApp keeps one per slide); engine-side state, so SourceFile
    // itself stays Cling-free.
    std::map<const SourceFile *, cling::Transaction *> render_tx_;
};
