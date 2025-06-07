#pragma once

#include "Slide.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>
#include <map>

#include <fmt/format.h>

#include <imgui.h>

namespace cling {
    class Transaction;
}

class SourceFile {
    std::string src;
    bool needs_reload_check{ false };

public:
    std::filesystem::path path;
    std::filesystem::file_time_type last_write_time;
    mutable bool dirty{ false };
    bool validated{ false };
    bool compiled{ false };
    bool syntax_error{ false };
    cling::Transaction *last_transaction{ nullptr };
    std::function<void ()> function; // Function to execute the slide, if any
    std::string value; // The cling::Value converted to a string, if any
    std::string exception;
    std::map<int, std::string> error_markers;
    size_t lines{ 0 };
    bool is_cuda{ false }; // Is this a CUDA file?

    SourceFile(std::filesystem::path);

    std::string text() const;
    void setText(const std::string &);

    void execute();

    void save() const;
    void saveAndExecute() {
        save();
        execute();
    }

    bool hasFileChangedOnDisk() const {
        // If it's gone, it has changed to an extreme extent
        if (!std::filesystem::exists(path)) return true;
        auto current_time = std::filesystem::last_write_time(path);
        return current_time != last_write_time;
    }
    void reload();

    void setValidated(bool valid) { validated = valid; }
    bool needsCompile() const { return compiled; }
    void setCompiled(bool compilated) { compiled = compilated; }

    void markForReloadCheck() { needs_reload_check = true; }
    bool needsReloadCheck() const { return needs_reload_check; }
    void clearReloadCheck() { needs_reload_check = false; }
    void updateLastWriteTime() {
        if (std::filesystem::exists(path)) {
            last_write_time = std::filesystem::last_write_time(path);
        }
    }
};

class Presentation {
public:
    std::filesystem::path path;

    SourceFile setup;
    std::vector<SourceFile> slides;

    Presentation(const std::filesystem::path &path)
    : path(path)
    , setup(path / "setup.cpp") {
        for (int i = 0; i < 10; ++i) {
            slides.emplace_back(path / fmt::format("slide{}.cpp", i));
        }
    }

    int indexOf(const SourceFile &slide) const;

    std::string getName(const SourceFile &slide) const {
        if (&slide == &setup) {
            return "setup";
        }
        auto it = std::find_if(slides.begin(), slides.end(), [&slide](const SourceFile& s) { return &s == &slide; });
        if (it != slides.end()) {
            return fmt::format("slide{}", std::distance(slides.begin(), it));
        }
        throw std::runtime_error("Slide not found");
    }

    SourceFile& getSourceFile(const std::string &name) {
        if (name == "setup")
            return setup;
        assert(name.substr(0, 5) == "slide" && slides.size() > std::stoul(name.substr(5)));
        return slides.at(std::stoi(name.substr(5)));
    }
};
