#pragma once

#include <string>
#include <vector>

#include "imdrawlist_painter.h"

namespace Latex {
    /**
     * @brief Initializes the latex fonts and parser
     *
     * @return std::string error message if failed
     */
    std::string init();


    /**
     * @brief returns true if latex has been initialized
     * otherwise false
     */
    bool isInitialized();

    void release();

    /**
     * @brief A LatexImage generates an image from a latex source
     *
     * Can only be rescaled after creation
     *
     */
    class LatexImage {
    private:
        microtex::Render* m_render = nullptr;
        microtex::Graphics2D_abstract m_graphics;
        microtex::ImDrawList_Painter m_painter;
        float m_ascent;
        float m_descent;
        float m_baseline;

        std::string m_latex_error_msg;

    public:
        /**brief Create a Latex Image
         *
         latex source
         * @param font_size indicative font size for latex
         * @param text_color defaut text color
         * @param scale rescale the image (in x and y)
         * @param inner_padding horizontal and vertical inner padding (will be scaled)
         */
        LatexImage(const std::string& latex_src, float font_size = 18.f, float width = 0, float line_space = 7.f, microtex::color text_color = microtex::BLACK);

        ~LatexImage();

        /**
         * @brief Returns the dimensions of the latex image
         *
         * Returns the correct dimensions even if the image has been forgotten with forgetImage()
         * If a latex error occured in the creation of the object, returns (0,0)
         *
         * @return ImVec2
         */
        ImVec2 getDimensions();

        /**
         * @brief Returns latex error message (if any, otherwise empty string)
         */
        std::string getLatexErrorMsg() { return m_latex_error_msg; }

        float getAscent() { return m_ascent; }
        float getDescent() { return m_descent; }

        /**
         * @brief Redraws the parsed latex into an image
         * getImage will return a valid image (if no latex error occured)
         *
         * @param scale rescale the image (in x and y)
         * @param inner_padding horizontal and vertical inner padding (will be scaled)
         */
        bool render(ImVec2 scale = ImVec2(1.f, 1.f), ImVec2 inner_padding = ImVec2(20.f, 20.f), bool animate = false);
    };

    using LatexImagePtr = std::shared_ptr<LatexImage>;
    using LatexImageUPtr = std::unique_ptr<LatexImage>;
}
