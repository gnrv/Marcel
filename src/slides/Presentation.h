#pragma once

#include "Slide.h"

#include <cassert>
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
public:
    std::filesystem::path path;
    mutable bool dirty{ false };
    bool validated{ false };
    bool compiled{ false };
    bool syntax_error{ false };
    cling::Transaction *last_transaction{ nullptr };
    std::function<void (ImVec2)> function; // Function to execute the slide, if any
    std::string value; // The cling::Value converted to a string, if any
    std::string exception;
    std::map<int, std::string> error_markers;
    size_t lines{ 0 };

    SourceFile(std::filesystem::path);

    std::string text() const;
    void setText(const std::string &);

    void execute();

    void save() const;
    void saveAndExecute() {
        save();
        execute();
    }

    void setValidated(bool valid) { validated = valid; }
    bool needsCompile() const { return compiled; }
    void setCompiled(bool compilated) { compiled = compilated; }
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

    SourceFile& getSourceFile(const std::string &name) {
        if (name == "setup")
            return setup;
        assert(name.substr(0, 5) == "slide" && slides.size() > std::stoul(name.substr(5)));
        return slides.at(std::stoi(name.substr(5)));
    }
};
