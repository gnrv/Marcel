// Doesn't work properly on re-compile of slides...
#include <fmt/format.h>

#include <iostream>

#include <imgui.h>

#include <implot.h>
#include <implot_internal.h>

#include <imga.h>

#include <cstddef>

namespace ImPlot {

struct VectorFitter {
    VectorFitter(const ImVec2& start, const ImVec2& end) : Start(start), End(end) { }
    void Fit(ImPlotAxis& x_axis, ImPlotAxis& y_axis) const {
        x_axis.ExtendFitWith(y_axis, Start.x, Start.y);
        y_axis.ExtendFitWith(x_axis, Start.y, Start.x);
        x_axis.ExtendFitWith(y_axis, End.x, End.y);
        y_axis.ExtendFitWith(x_axis, End.y, End.x);
    }
    const ImVec2& Start;
    const ImVec2& End;
};

struct Transformer1 {
    Transformer1(double pixMin, double pltMin, double pltMax, double m, double scaMin, double scaMax, ImPlotTransform fwd, void* data) :
        ScaMin(scaMin),
        ScaMax(scaMax),
        PltMin(pltMin),
        PltMax(pltMax),
        PixMin(pixMin),
        M(m),
        TransformFwd(fwd),
        TransformData(data)
    { }

    template <typename T> inline float operator()(T p) const {
        if (TransformFwd != nullptr) {
            double s = TransformFwd(p, TransformData);
            double t = (s - ScaMin) / (ScaMax - ScaMin);
            p = PltMin + (PltMax - PltMin) * t;
        }
        return (float)(PixMin + M * (p - PltMin));
    }

    double ScaMin, ScaMax, PltMin, PltMax, PixMin, M;
    ImPlotTransform TransformFwd;
    void*           TransformData;
};

struct Transformer2 {
    Transformer2(const ImPlotAxis& x_axis, const ImPlotAxis& y_axis) :
        Tx(x_axis.PixelMin,
           x_axis.Range.Min,
           x_axis.Range.Max,
           x_axis.ScaleToPixel,
           x_axis.ScaleMin,
           x_axis.ScaleMax,
           x_axis.TransformForward,
           x_axis.TransformData),
        Ty(y_axis.PixelMin,
           y_axis.Range.Min,
           y_axis.Range.Max,
           y_axis.ScaleToPixel,
           y_axis.ScaleMin,
           y_axis.ScaleMax,
           y_axis.TransformForward,
           y_axis.TransformData)
    { }

    Transformer2(const ImPlotPlot& plot) :
        Transformer2(plot.Axes[plot.CurrentX], plot.Axes[plot.CurrentY])
    { }

    Transformer2() :
        Transformer2(*GImPlot->CurrentPlot)
    { }

    template <typename P> inline ImVec2 operator()(const P& plt) const {
        ImVec2 out;
        out.x = Tx(plt.x);
        out.y = Ty(plt.y);
        return out;
    }

    template <typename T> inline ImVec2 operator()(T x, T y) const {
        ImVec2 out;
        out.x = Tx(x);
        out.y = Ty(y);
        return out;
    }

    Transformer1 Tx;
    Transformer1 Ty;
};

template <typename T>
struct MaxIdx { static const unsigned int Value; };
template <> const unsigned int MaxIdx<unsigned short>::Value = 65535;
template <> const unsigned int MaxIdx<unsigned int>::Value   = 4294967295;

#if defined __SSE__ || defined __x86_64__ || defined _M_X64
#ifndef IMGUI_ENABLE_SSE
#include <immintrin.h>
#endif
static inline float  ImInvSqrt(float x) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x))); }
#else
static inline float  ImInvSqrt(float x) { return 1.0f / sqrtf(x); }
#endif

#define IMPLOT_NORMALIZE2F_OVER_ZERO(VX,VY) do { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = ImInvSqrt(d2); VX *= inv_len; VY *= inv_len; } } while (0)

inline void PrimLine(ImDrawList& draw_list, const ImVec2& P1, const ImVec2& P2, float half_weight, ImU32 col, const ImVec2& tex_uv0, const ImVec2 tex_uv1) {
    float dx = P2.x - P1.x;
    float dy = P2.y - P1.y;
    IMPLOT_NORMALIZE2F_OVER_ZERO(dx, dy);
    dx *= half_weight;
    dy *= half_weight;
    draw_list._VtxWritePtr[0].pos.x = P1.x + dy;
    draw_list._VtxWritePtr[0].pos.y = P1.y - dx;
    draw_list._VtxWritePtr[0].uv    = tex_uv0;
    draw_list._VtxWritePtr[0].col   = col;
    draw_list._VtxWritePtr[1].pos.x = P2.x + dy;
    draw_list._VtxWritePtr[1].pos.y = P2.y - dx;
    draw_list._VtxWritePtr[1].uv    = tex_uv0;
    draw_list._VtxWritePtr[1].col   = col;
    draw_list._VtxWritePtr[2].pos.x = P2.x - dy;
    draw_list._VtxWritePtr[2].pos.y = P2.y + dx;
    draw_list._VtxWritePtr[2].uv    = tex_uv1;
    draw_list._VtxWritePtr[2].col   = col;
    draw_list._VtxWritePtr[3].pos.x = P1.x - dy;
    draw_list._VtxWritePtr[3].pos.y = P1.y + dx;
    draw_list._VtxWritePtr[3].uv    = tex_uv1;
    draw_list._VtxWritePtr[3].col   = col;
    draw_list._VtxWritePtr += 4;
    draw_list._IdxWritePtr[0] = (ImDrawIdx)(draw_list._VtxCurrentIdx);
    draw_list._IdxWritePtr[1] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 1);
    draw_list._IdxWritePtr[2] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 2);
    draw_list._IdxWritePtr[3] = (ImDrawIdx)(draw_list._VtxCurrentIdx);
    draw_list._IdxWritePtr[4] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 2);
    draw_list._IdxWritePtr[5] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 3);
    draw_list._IdxWritePtr += 6;
    draw_list._VtxCurrentIdx += 4;
}

/// Renders primitive shapes in bulk as efficiently as possible.
template <class _Renderer>
void RenderPrimitivesEx(const _Renderer& renderer, ImDrawList& draw_list, const ImRect& cull_rect) {
    unsigned int prims        = renderer.Prims;
    unsigned int prims_culled = 0;
    unsigned int idx          = 0;
    renderer.Init(draw_list);
    while (prims) {
        // find how many can be reserved up to end of current draw command's limit
        unsigned int cnt = ImMin(prims, (MaxIdx<ImDrawIdx>::Value - draw_list._VtxCurrentIdx) / renderer.VtxConsumed);
        // make sure at least this many elements can be rendered to avoid situations where at the end of buffer this slow path is not taken all the time
        if (cnt >= ImMin(64u, prims)) {
            if (prims_culled >= cnt)
                prims_culled -= cnt; // reuse previous reservation
            else {
                // add more elements to previous reservation
                draw_list.PrimReserve((cnt - prims_culled) * renderer.IdxConsumed, (cnt - prims_culled) * renderer.VtxConsumed);
                prims_culled = 0;
            }
        }
        else
        {
            if (prims_culled > 0) {
                draw_list.PrimUnreserve(prims_culled * renderer.IdxConsumed, prims_culled * renderer.VtxConsumed);
                prims_culled = 0;
            }
            cnt = ImMin(prims, (MaxIdx<ImDrawIdx>::Value - 0/*draw_list._VtxCurrentIdx*/) / renderer.VtxConsumed);
            // reserve new draw command
            draw_list.PrimReserve(cnt * renderer.IdxConsumed, cnt * renderer.VtxConsumed);
        }
        prims -= cnt;
        for (unsigned int ie = idx + cnt; idx != ie; ++idx) {
            if (!renderer.Render(draw_list, cull_rect, idx))
                prims_culled++;
        }
    }
    if (prims_culled > 0)
        draw_list.PrimUnreserve(prims_culled * renderer.IdxConsumed, prims_culled * renderer.VtxConsumed);
}

template <template <class> class _Renderer, class _Getter, typename ...Args>
void RenderPrimitives1(const _Getter& getter, Args... args) {
    ImDrawList& draw_list = *GetPlotDrawList();
    const ImRect& cull_rect = GetCurrentPlot()->PlotRect;
    RenderPrimitivesEx(_Renderer<_Getter>(getter,args...), draw_list, cull_rect);
}

struct RendererBase {
    RendererBase(int prims, int idx_consumed, int vtx_consumed) :
        Prims(prims),
        IdxConsumed(idx_consumed),
        VtxConsumed(vtx_consumed)
    { }
    const int Prims;
    Transformer2 Transformer;
    const int IdxConsumed;
    const int VtxConsumed;
};

inline void GetLineRenderProps(const ImDrawList& draw_list, float& half_weight, ImVec2& tex_uv0, ImVec2& tex_uv1) {
    const bool aa = ImHasFlag(draw_list.Flags, ImDrawListFlags_AntiAliasedLines) &&
                    ImHasFlag(draw_list.Flags, ImDrawListFlags_AntiAliasedLinesUseTex);
    if (aa) {
        ImVec4 tex_uvs = draw_list._Data->TexUvLines[(int)(half_weight*2)];
        tex_uv0 = ImVec2(tex_uvs.x, tex_uvs.y);
        tex_uv1 = ImVec2(tex_uvs.z, tex_uvs.w);
        half_weight += 1;
    }
    else {
        tex_uv0 = tex_uv1 = draw_list._Data->TexUvWhitePixel;
    }
}

template <class _Getter>
struct RendererLineStrip : RendererBase {
    RendererLineStrip(const _Getter& getter, ImU32 col, float weight) :
        RendererBase(getter.Count - 1, 6, 4),
        Getter(getter),
        Col(col),
        HalfWeight(ImMax(1.0f,weight)*0.5f)
    {
        P1 = this->Transformer(Getter(0));
    }
    void Init(ImDrawList& draw_list) const {
        GetLineRenderProps(draw_list, HalfWeight, UV0, UV1);
    }
    inline bool Render(ImDrawList& draw_list, const ImRect& cull_rect, int prim) const {
        ImVec2 P2 = this->Transformer(Getter(prim + 1));
        if (!cull_rect.Overlaps(ImRect(ImMin(P1, P2), ImMax(P1, P2)))) {
            P1 = P2;
            return false;
        }
        PrimLine(draw_list,P1,P2,HalfWeight,Col,UV0,UV1);
        P1 = P2;
        return true;
    }
    const _Getter& Getter;
    const ImU32 Col;
    mutable float HalfWeight;
    mutable ImVec2 P1;
    mutable ImVec2 UV0;
    mutable ImVec2 UV1;
};

template <class _Getter>
struct RendererLineSegments1 : RendererBase {
    RendererLineSegments1(const _Getter& getter, ImU32 col, float weight) :
        RendererBase(getter.Count / 2, 6, 4),
        Getter(getter),
        Col(col),
        HalfWeight(ImMax(1.0f,weight)*0.5f)
    { }
    void Init(ImDrawList& draw_list) const {
        GetLineRenderProps(draw_list, HalfWeight, UV0, UV1);
    }
    inline bool Render(ImDrawList& draw_list, const ImRect& cull_rect, int prim) const {
        ImVec2 P1 = this->Transformer(Getter(prim*2+0));
        ImVec2 P2 = this->Transformer(Getter(prim*2+1));
        if (!cull_rect.Overlaps(ImRect(ImMin(P1, P2), ImMax(P1, P2))))
            return false;
        PrimLine(draw_list,P1,P2,HalfWeight,Col,UV0,UV1);
        return true;
    }
    const _Getter& Getter;
    const ImU32 Col;
    mutable float HalfWeight;
    mutable ImVec2 UV0;
    mutable ImVec2 UV1;
};

struct PointGetter {
    PointGetter(const ImVec2& end) : End(end) { }
    template <typename I> inline ImPlotPoint operator()(I) const {
        return ImPlotPoint(End.x, End.y);
    }
    const ImVec2& End;
    const int Count{ 1 };
};

struct VectorGetter {
    VectorGetter(const ImVec2& start, const ImVec2& end) : Start(start), End(end) { }
    template <typename I> inline ImPlotPoint operator()(I idx) const {
        return ImPlotPoint((idx > 0) ? End.x : Start.x, (idx > 0) ? End.y : Start.y);
    }
    const ImVec2& Start;
    const ImVec2& End;
    const int Count{ 2 };
};

template <class _Getter>
struct RendererMarkersFill : RendererBase {
    RendererMarkersFill(const _Getter& getter, const ImVec2* marker, int count, float size, ImU32 col) :
        RendererBase(getter.Count, (count-2)*3, count),
        Getter(getter),
        Marker(marker),
        Count(count),
        Size(size),
        Col(col)
    { }
    void Init(ImDrawList& draw_list) const {
        UV = draw_list._Data->TexUvWhitePixel;
    }
    inline bool Render(ImDrawList& draw_list, const ImRect& cull_rect, int prim) const {
        ImVec2 p = this->Transformer(Getter(prim));
        if (p.x >= cull_rect.Min.x && p.y >= cull_rect.Min.y && p.x <= cull_rect.Max.x && p.y <= cull_rect.Max.y) {
            for (int i = 0; i < Count; i++) {
                draw_list._VtxWritePtr[0].pos.x = p.x + Marker[i].x * Size;
                draw_list._VtxWritePtr[0].pos.y = p.y + Marker[i].y * Size;
                draw_list._VtxWritePtr[0].uv = UV;
                draw_list._VtxWritePtr[0].col = Col;
                draw_list._VtxWritePtr++;
            }
            for (int i = 2; i < Count; i++) {
                draw_list._IdxWritePtr[0] = (ImDrawIdx)(draw_list._VtxCurrentIdx);
                draw_list._IdxWritePtr[1] = (ImDrawIdx)(draw_list._VtxCurrentIdx + i - 1);
                draw_list._IdxWritePtr[2] = (ImDrawIdx)(draw_list._VtxCurrentIdx + i);
                draw_list._IdxWritePtr += 3;
            }
            draw_list._VtxCurrentIdx += (ImDrawIdx)Count;
            return true;
        }
        return false;
    }
    const _Getter& Getter;
    const ImVec2* Marker;
    const int Count;
    const float Size;
    const ImU32 Col;
    mutable ImVec2 UV;
};

template <class _Getter>
struct RendererMarkersLine : RendererBase {
    RendererMarkersLine(const _Getter& getter, const ImVec2* marker, int count, float size, float weight, ImU32 col) :
        RendererBase(getter.Count, count/2*6, count/2*4),
        Getter(getter),
        Marker(marker),
        Count(count),
        HalfWeight(ImMax(1.0f,weight)*0.5f),
        Size(size),
        Col(col)
    { }
    void Init(ImDrawList& draw_list) const {
        GetLineRenderProps(draw_list, HalfWeight, UV0, UV1);
    }
    inline bool Render(ImDrawList& draw_list, const ImRect& cull_rect, int prim) const {
        ImVec2 p = this->Transformer(Getter(prim));
        if (p.x >= cull_rect.Min.x && p.y >= cull_rect.Min.y && p.x <= cull_rect.Max.x && p.y <= cull_rect.Max.y) {
            for (int i = 0; i < Count; i = i + 2) {
                ImVec2 p1(p.x + Marker[i].x * Size, p.y + Marker[i].y * Size);
                ImVec2 p2(p.x + Marker[i+1].x * Size, p.y + Marker[i+1].y * Size);
                PrimLine(draw_list, p1, p2, HalfWeight, Col, UV0, UV1);
            }
            return true;
        }
        return false;
    }
    const _Getter& Getter;
    const ImVec2* Marker;
    const int Count;
    mutable float HalfWeight;
    const float Size;
    const ImU32 Col;
    mutable ImVec2 UV0;
    mutable ImVec2 UV1;
};

#define SQRT_3_2 0.86602540378f
#define INV_SQRT_3 0.57735026919f

static ImVec2 MARKER_FILL_RIGHT[3]    = {ImVec2(0,0), ImVec2(-3, INV_SQRT_3), ImVec2(-3, -INV_SQRT_3)};
static ImVec2 MARKER_LINE_RIGHT[6]    = {ImVec2(0,0),  ImVec2(-3, INV_SQRT_3), ImVec2(-3, INV_SQRT_3), ImVec2(-3, -INV_SQRT_3), ImVec2(-3, -INV_SQRT_3), ImVec2(0,0) };

static void RenderLine(ImVec2 start, ImVec2 end) {
    const ImPlotNextItemData& s = GetItemData();
    if (!s.RenderLine)
        return;

    const ImU32 col_line = ImGui::GetColorU32(s.Colors[ImPlotCol_Line]);
    VectorGetter getter1(start, end);
    RenderPrimitives1<RendererLineSegments1>(getter1,col_line,s.LineWeight);
}

static void RenderVector(ImVec2 start, ImVec2 end) {
    const ImPlotNextItemData& s = GetItemData();
    if (!s.RenderLine)
        return;

    Transformer2 transformer;
    ImVec2 start_screen = transformer(start.x, start.y);
    ImVec2 end_screen = transformer(end.x, end.y);
    ImVec2 dir_screen = end_screen - start_screen;
    ImVec2 normalized_dir_screen = dir_screen / sqrt(dir_screen.x * dir_screen.x + dir_screen.y * dir_screen.y);
    ImVec2 adjusted_end_screen = end_screen - normalized_dir_screen * s.MarkerSize;

    // TODO: We need to inverse transform adjusted_end_screen back to the plot space
    // to get the actual end point of the vector
    // For now, just compute normalized_dir
    ImVec2 dir = end - start;
    ImVec2 normalized_dir = dir / sqrt(dir.x * dir.x + dir.y * dir.y);
    float scale = sqrt(dir.x * dir.x + dir.y * dir.y) / sqrt(dir_screen.x * dir_screen.x + dir_screen.y * dir_screen.y);

    const ImU32 col_line = ImGui::GetColorU32(s.Colors[ImPlotCol_Line]);
    VectorGetter getter1(start, end - normalized_dir*scale*10);
    RenderPrimitives1<RendererLineSegments1>(getter1,col_line,s.LineWeight);

    // Rotate the marker points so they point in the direction of the vector
    ImVec2 fill[3];
    ImVec2 line[6];
    {
        // Now, dir is expressed in the plot space
        // We need to use the ImPlot Transform to move to screen coordinates
        // Watch out, the ImGui coordinate system is flipped on the y axis
        float angle = atan2(dir_screen.y, dir_screen.x);
        float c = cos(angle);
        float s = sin(angle);
        for (int i = 0; i < 6; ++i) {
            if (i < 3) {
                float x = MARKER_FILL_RIGHT[i].x;
                float y = MARKER_FILL_RIGHT[i].y;
                fill[i].x = x * c - y * s;
                fill[i].y = x * s + y * c;
            }

            float x = MARKER_LINE_RIGHT[i].x;
            float y = MARKER_LINE_RIGHT[i].y;
            line[i].x = x * c - y * s;
            line[i].y = x * s + y * c;
        }
    }

    // Render vector arrow
    PointGetter getter2(end);
    RenderPrimitives1<RendererMarkersFill>(getter2, fill, 3, s.MarkerSize, col_line);
    // We add this just to get an antialiased outline for the vector arrow
    RenderPrimitives1<RendererMarkersLine>(getter2, line, 6, s.MarkerSize, 1, col_line);
}

static size_t g_Ordinal = 0;

struct ImGaBase {
    size_t Ordinal{ g_Ordinal++ };
    bool Animate{ false };
    double StartTime{ 0 };
};

struct ImVector : ImGaBase {
};

ImPool<ImVector> g_Vectors;

double BeginFade(ImGaBase *item) {
    ImGuiContext& g = *GImGui;
    double now = ImGui::GetTime();
    if (g.CurrentItemFlags & ImGuiItemFlags_Animated) {
        item->Animate = true;
        item->StartTime = now + item->Ordinal * 0.05; // 0.05 is used in latex.cpp
    }

    double t = now - item->StartTime;
    double s = ImSaturate(t / 0.5);
    //s = s * s * s * (s * (s * 6 - 15) + 10);
    //s = 1 - pow(1 - s, 3);
    if (item->Animate) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s);
    } else {
        s = 1;
    }
    return s;
}

void EndFade(ImGaBase *item, double s) {
    if (item->Animate) {
        if (s >= 1) {
            item->Animate = false;
        }
        ImGui::PopStyleVar();
    }
}

void Vector(const char* label_id, ImVec2 start, ImVec2 end, ImPlotItemFlags flags) {
    if (BeginItemEx(label_id, VectorFitter(start, end), flags, ImPlotCol_Line)) {
        const ImGuiID id = ImGui::GetID(label_id);
        ImVector *vector = g_Vectors.GetOrAddByKey(id);
        double s = BeginFade(vector);
        RenderVector(start, end);
        if (!ImHasFlag(flags, ImPlotItemFlags_NoLabel)) {
            ImVec2 pos = (end + start) / 2;
            ImVec4 col = ImPlot::GetStyleColorVec4(ImPlotCol_InlayText);
            ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(col.x, col.y, col.z, s));
            ImPlot::PlotText(label_id, pos.x, pos.y);
            ImPlot::PopStyleColor();
        }
        EndFade(vector, s);
        EndItem();
    }
}

struct ImBivector : ImGaBase {
};

ImPool<ImBivector> g_Bivectors;

void Bivector(const char* label_id, ImVec2 start, ImVec2 mid, ImVec2 end, ImPlotItemFlags flags) {
    if (BeginItemEx(label_id, VectorFitter(start, end), flags, ImPlotCol_Line)) {
        ImVec2 a = mid - start;
        auto bivector = g_Bivectors.GetOrAddByKey(ImGui::GetID(label_id));
        double s = BeginFade(bivector);
        ImDrawList &draw_list = *GetPlotDrawList();
        ImVec2 start_screen = Transformer2()(start.x, start.y);
        ImVec2 mid_screen = Transformer2()(mid.x, mid.y);
        ImVec2 end_screen = Transformer2()(end.x, end.y);

        // Add a polygon (parallelogram) to represent the bivector
        // We need to add the points in a clockwise order, according to ImGui docs.
        // Note that the notion of clockwise is reversed in the plot because the y-axis is flipped.
        float orientation = a.x * (end.y - mid.y) + a.y * (mid.x - end.x);
        ImVec2 points[4] = {
            start_screen,
            mid_screen,
            end_screen,
            end_screen + (start_screen - mid_screen)
        };
        if (orientation > 0)
            std::swap(points[1], points[3]);
        const ImPlotNextItemData& data = GetItemData();
        ImVec4 col4 = data.Colors[ImPlotCol_Line];
        col4.w *= 0.25;
        const ImU32 col_line = ImGui::GetColorU32(col4);
        draw_list.AddConvexPolyFilled(points, 4, col_line);

#if 0
        RenderVector(start, mid);
        RenderVector(mid, end);
        RenderVector(end, end - a);
        RenderVector(end - a, start);
#else
        // Render a polyline around the perimeter of the bivector
        draw_list.AddPolyline(points, 4, ImGui::GetColorU32(data.Colors[ImPlotCol_Line]), ImDrawFlags_Closed, data.LineWeight);

        // Compute the four midpoints of each side of the parallelogram
        ImVec2 midpoints[4] = {
            (start_screen + mid_screen) / 2,
            (mid_screen + end_screen) / 2,
            (start_screen     + end_screen * 2 - mid_screen) / 2,
            (start_screen * 2 + end_screen     - mid_screen) / 2
        };
        if (orientation > 0)
            std::swap(midpoints[1], midpoints[3]);

        // From each midpoint, we draw an arrow pointing in the tangent direction
        for (int i = 0; i < 4; ++i) {
            ImVec2 tangent = midpoints[i] - points[i];
            tangent = tangent / sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
            ImVec2 normal = ImVec2(tangent.y, -tangent.x);
            ImVec2 arrow[3] = {
                midpoints[i] - tangent * 4 + normal * 8 * SQRT_3_2,
                midpoints[i] + tangent * 4,
                midpoints[i] - tangent * 4 - normal * 8 * SQRT_3_2
            };
            draw_list.AddPolyline(arrow, 3, ImGui::GetColorU32(data.Colors[ImPlotCol_Line]), false, data.LineWeight);
        }
#endif

        if (!ImHasFlag(flags, ImPlotItemFlags_NoLabel)) {
            ImVec2 pos = (end + start) / 2;
            ImVec4 col = ImPlot::GetStyleColorVec4(ImPlotCol_InlayText);
            ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(col.x, col.y, col.z, s));
            ImPlot::PlotText(label_id, pos.x, pos.y);
            ImPlot::PopStyleColor();
        }

        EndFade(bivector, s);
        EndItem();
    }
}

}
