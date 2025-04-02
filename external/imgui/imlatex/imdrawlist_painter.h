#pragma once

#include "graphic_abstract.h"

#include <deque>
#include <vector>

namespace microtex {
    class ImDrawList_Painter : public Painter {
    private:
        int manim_n{ 1 };
        bool was_animating{ false };
        double t_start{ 0 };

        ImDrawList* m_draw_list{ nullptr };

        ImVec2 m_offset, m_scale, m_dimensions, m_origin;

        bool m_painting{ false };

        struct State {
            ImU32 color;
            Stroke stroke;
            float thickness{ 1.f };
            std::vector<float> dash;

            float dx = 0.f;
            float dy = 0.f;
            float sx = 1.f;
            float sy = 1.f;
        } m_state;
        std::deque<State> m_state_stack;

        // ImGui doesn't support discontinuous paths, i.e. sub-paths
        // to implement this, we build a path using ImGui draw list as usual
        // but when the user calls moveTo, we steal the path points and store
        // into a SubPath. When the user calls fillPath or strokePath, we draw
        // all stored SubPaths.
        struct SubPath {
            SubPath(const ImVector<ImVec2>& p) : points(p) {
                // Calculate the clockwise count
                // https://stackoverflow.com/questions/1165647/how-to-determine-if-a-list-of-polygon-points-are-in-clockwise-order
                for (int i = 0; i < points.Size; ++i) {
                    // For closed paths, which is par for the game here,
                    // the last point is the same as the first, so we'll just get 0
                    // on the last lap of the loop.
                    ImVec2 p1 = points[i];
                    ImVec2 p2 = points[(i + 1) % points.Size];
                    clockwise += (p2.x - p1.x) * (p2.y + p1.y);
                }
            }
            ImVector<ImVec2> points;
            float clockwise{ 0 };
        };
        std::vector<SubPath> m_path;

        inline ImVec2 getRealPos(float x, float y);

        void roundRect(float x, float y, float w, float h, float rx, float ry);
        void destroy();

        void pathSanityCheck();
    public:
        ImDrawList_Painter();
        ~ImDrawList_Painter();

        ImVec2 getImageDimensions() { return m_dimensions; }

        virtual void setColor(color c) override;
        virtual color getColor() const override;

        virtual void setStroke(const Stroke& s) override;

        virtual void setStrokeWidth(float w) override;

        void setDash(const std::vector<float>& dash) override;

        void setFont(const std::string& path, float size, int style = -1, const std::string& family = "") override;

        void setFontSize(float size) override;

        virtual void translate(float dx, float dy) override;

        virtual void scale(float sx, float sy) override;

        virtual void rotate(float angle) override;

        virtual void rotate(float angle, float px, float py) override;

        virtual void reset() override;

        virtual void save() override;
        virtual void restore() override;

        void drawGlyph(u16 glyph, float x, float y) override;

        // ImGui doesn't support discontinuous paths.
        // On each moveTo, we would need to store the path elements
        // and draw them on fillPath or strokePath because not until we get the
        // fillPath or strokePath call do we know if the path is closed or not.
        // TODO: Another option is to peek ahead in the call list and determine
        //       if it ends with a fillPath or a strokePath.
        void beginPath(i32 id) override;

        void moveTo(float x, float y) override;

        void lineTo(float x, float y) override;

        void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) override;

        void quadTo(float x1, float y1, float x2, float y2) override;

        // Close the path by drawing a line to the first point in the current sub-path
        // TODO: Another option is to look back and find the most recent moveTo or beginPath
        void closePath() override;

        void fillPath(i32 id) override;
        void strokePath(i32 id) override;

        /** Draw text */
        void drawText(const std::string& src, float x, float y);

        virtual void drawLine(float x, float y1, float x2, float y2) override;

        virtual void drawRect(float x, float y, float w, float h) override;

        virtual void fillRect(float x, float y, float w, float h) override;

        virtual void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override;

        virtual void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override;

        virtual void start(ImVec2 dimensions, ImVec2 scale = ImVec2(1.f, 1.f), ImVec2 inner_padding = ImVec2(20.f, 20.f)) override;

        virtual void finish() override;

        /**
         * @brief Distributes the (functions) call list to the painter, with optional animation
         *
         * Animates by scaling and fading in each path and fading in (no scaling) each draw/fill call
         *
         * @param animate if true, the calls will be animated
         * @return true if the animation is ongoing
         */
        bool distributeCallListFadeIn(std::vector<Call> &calls, bool animate);

        /**
         * @brief Distributes the (functions) call list to the painter, with optional animation
         *
         * @param animate if true, the calls will be animated with a Manim-inspired animation
         * @return true if the animation is ongoing
         */
        bool distributeCallListManim(const std::vector<Call> &calls, bool animate);
    };
}