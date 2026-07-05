#include "Presentation.h"

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

SourceFile::SourceFile(fs::path path)
: path(path)
{
    std::ifstream t(path);
    if (t.good()) {
        src = std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        lines = std::count(src.begin(), src.end(), '\n');
    } else {
        // Try to create a new file
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("Failed to create file: " + path.string());
        }
        src = "";
    }
    updateLastWriteTime();
}

std::string SourceFile::text() const {
    return src;
}

void SourceFile::setText(const std::string &source) {
    if (src == source)
        return;
    src = source;
    dirty = true;
}

void SourceFile::execute() {
    validated = false;
    compiled = false;
    syntax_error = false;
    // Invalidate any in-flight remote compile: its result must not be
    // applied over the new text (request_id 0 never matches a submission).
    compile_in_flight = false;
    compile_request_id = 0;

    // Count lines
    lines = std::count(src.begin(), src.end(), '\n');
}

void SourceFile::save() const {
    std::ofstream t(path);
    if (t.good())
        t << src;
    if (t.good())
        dirty = false;
    else
        throw std::runtime_error("Failed to save file: " + path.string());
}

void SourceFile::reload() {
    std::ifstream t(path);
    if (t.good()) {
        src = std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        lines = std::count(src.begin(), src.end(), '\n');
    } else {
        throw std::runtime_error("Failed to reload file: " + path.string());
    }
    updateLastWriteTime();
}

int Presentation::indexOf(const SourceFile &slide) const {
    if (&slide == &setup) {
        return -1; // Setup is not a slide
    }
    return std::distance(slides.begin(), std::find_if(slides.begin(), slides.end(), [&slide](const SourceFile &s) {
        return &s == &slide;
    }));
}
