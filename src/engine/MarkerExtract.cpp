#include "MarkerExtract.h"

#include "Presentation.h"

#include <algorithm>
#include <regex>
#include <stdexcept>
#include <string>

void extractMarkers(SourceFile &source_file, const char *buf, size_t size, size_t offset) {
    source_file.error_markers.clear();
    std::string buf_str(buf, size);
    size_t line_no = 0;
    auto colon_pos = buf_str.find(':');
    if (colon_pos != std::string::npos) {
        auto line_str = buf_str.substr(colon_pos + 1);
        try {
            line_no = std::stoi(line_str);
        } catch (const std::invalid_argument& e) {
            line_no = 1;
        } catch (const std::out_of_range& e) {
            line_no = 1;
        }
    }

    line_no += offset;
    line_no = std::min(line_no, source_file.lines);
    line_no = std::max(line_no, size_t(1));

    // For now, strip out the ansi color codes
    buf_str = std::regex_replace(buf_str, std::regex("\033\\[[0-9;]*m"), "");
    source_file.error_markers.emplace(line_no, buf_str);
}
