#pragma once

struct ImFont;
struct ImFontAtlas;

// The application font set, loaded in a fixed order.
//
// LOAD-BEARING: slide code indexes the atlas directly (e.g.
// documents/test/slide0.cpp uses io.Fonts->Fonts[2]), so the order of
// AddFont* calls in LoadUiFonts() must never change, and the main process
// and the worker process must build byte-identical atlases by calling
// LoadUiFonts() with the same dpi_scale.
struct UiFonts {
    ImFont *fira_sans = nullptr;       // default UI font (MDI icons merged in)
    ImFont *fira_sans_big = nullptr;   // 48 px presentation font
    ImFont *fira_sans_small = nullptr; // 32 px presentation font
    ImFont *fira_mono = nullptr;       // editor mono font
};

// Loads all application fonts into `atlas`. Paths are relative to the
// executable's working directory at startup ("../data/fonts/..."), so this
// must be called before any current_path() change.
UiFonts LoadUiFonts(ImFontAtlas *atlas, float dpi_scale);
