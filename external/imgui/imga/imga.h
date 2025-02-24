#pragma once

#include <imgui.h>
#include <implot.h>

enum ImPlotItemFlagsExtra_ {
    ImPlotItemFlags_NoLabel    = 1 << 10, // the item won't be labelled
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
    void Vector(const char* label_id, ImPlotPoint start, ImPlotPoint end, ImPlotItemFlags flags = ImPlotItemFlags_None);
    void Bivector(const char* label_id, ImPlotPoint start, ImPlotPoint mid, ImPlotPoint end, ImPlotItemFlags flags = ImPlotItemFlags_None);
    void Bivector(const char* label_id, ImPlotPoint center, double area, ImPlotItemFlags flags = ImPlotItemFlags_None);
}
