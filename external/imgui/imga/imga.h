#pragma once

#include <imgui.h>
#include <implot.h>

enum ImPlotItemFlagsExtra_ {
    ImPlotItemFlags_NoLabel    = 1 << 10, // the item won't be labelled
    ImPlotItemFlags_NoLatex    = 1 << 11, // the item won't be labelled with latex
};

static inline ImPlotPoint  operator*(const ImPlotPoint& lhs, const float rhs)     { return ImPlotPoint(lhs.x * rhs, lhs.y * rhs); }
static inline ImPlotPoint  operator/(const ImPlotPoint& lhs, const float rhs)     { return ImPlotPoint(lhs.x / rhs, lhs.y / rhs); }
static inline ImPlotPoint  operator+(const ImPlotPoint& lhs, const ImPlotPoint& rhs)   { return ImPlotPoint(lhs.x + rhs.x, lhs.y + rhs.y); }
static inline ImPlotPoint  operator-(const ImPlotPoint& lhs, const ImPlotPoint& rhs)   { return ImPlotPoint(lhs.x - rhs.x, lhs.y - rhs.y); }
static inline ImPlotPoint  operator*(const ImPlotPoint& lhs, const ImPlotPoint& rhs)   { return ImPlotPoint(lhs.x * rhs.x, lhs.y * rhs.y); }
static inline ImPlotPoint  operator/(const ImPlotPoint& lhs, const ImPlotPoint& rhs)   { return ImPlotPoint(lhs.x / rhs.x, lhs.y / rhs.y); }
static inline ImPlotPoint  operator-(const ImPlotPoint& lhs)                      { return ImPlotPoint(-lhs.x, -lhs.y); }
static inline ImPlotPoint& operator*=(ImPlotPoint& lhs, const float rhs)          { lhs.x *= rhs; lhs.y *= rhs; return lhs; }
static inline ImPlotPoint& operator/=(ImPlotPoint& lhs, const float rhs)          { lhs.x /= rhs; lhs.y /= rhs; return lhs; }
static inline ImPlotPoint& operator+=(ImPlotPoint& lhs, const ImPlotPoint& rhs)        { lhs.x += rhs.x; lhs.y += rhs.y; return lhs; }
static inline ImPlotPoint& operator-=(ImPlotPoint& lhs, const ImPlotPoint& rhs)        { lhs.x -= rhs.x; lhs.y -= rhs.y; return lhs; }
static inline ImPlotPoint& operator*=(ImPlotPoint& lhs, const ImPlotPoint& rhs)        { lhs.x *= rhs.x; lhs.y *= rhs.y; return lhs; }
static inline ImPlotPoint& operator/=(ImPlotPoint& lhs, const ImPlotPoint& rhs)        { lhs.x /= rhs.x; lhs.y /= rhs.y; return lhs; }
static inline bool    operator==(const ImPlotPoint& lhs, const ImPlotPoint& rhs)  { return lhs.x == rhs.x && lhs.y == rhs.y; }
static inline bool    operator!=(const ImPlotPoint& lhs, const ImPlotPoint& rhs)  { return lhs.x != rhs.x || lhs.y != rhs.y; }

namespace ImPlot {
    // Plots a centered text label at point x,y with an optional pixel offset. Text color can be changed with ImPlot::PushStyleColor(ImPlotCol_InlayText, ...).
    //void PlotLatex(const char* latex, double x, double y, const ImVec2& pix_offset=ImVec2(0,0), ImPlotTextFlags flags=0);
    void Vector(const char* label_id, ImPlotPoint start, ImPlotPoint end, ImPlotItemFlags flags = ImPlotItemFlags_None);
    void Bivector(const char* label_id, ImPlotPoint start, ImPlotPoint mid, ImPlotPoint end, ImPlotItemFlags flags = ImPlotItemFlags_None);
    void Bivector(const char* label_id, ImPlotPoint center, double area, ImPlotItemFlags flags = ImPlotItemFlags_None);
}
