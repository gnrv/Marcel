#pragma once

class SourceFile;

// Interface between the UI loop and the slide-execution backend.
// Implementations: ClingEngine (in-process Cling interpreter, migration
// scaffolding) and, later, RemoteEngine (supervised marcel_worker process —
// see docs/plans/client-server-refactor.md). The surface is synchronous for
// now; the async event surface (drainEvents) arrives with RemoteEngine.
class SlideEngine {
public:
    virtual ~SlideEngine() = default;

    // Validate + compile the file if it needs it (no-op otherwise).
    // Fills validated/compiled/syntax_error/error_markers/value/function on
    // the SourceFile. May throw std::exception from syntax validation — the
    // caller shows the Exception popup, matching the historical behavior.
    virtual void compileSetup(SourceFile &setup) = 0;
    virtual void compileSlide(SourceFile &slide) = 0;

    // Debug window: dump interpreter state.
    virtual void dump(const char *what, const char *filter) = 0;
};
