#include "latex.h"
#include "imgui_internal.h"
#include <cmath>
#include <locale>

namespace Latex {
    bool is_initialized = false;

    static std::string font_family = "Fira Math";
    static std::string font_name_math = "Fira Math Regular";

    std::string init() {
        using namespace microtex;

        microtex::MicroTeX::setRenderGlyphUsePath(true);
        try {
            const FontSrcFile fira_math("data/firamath/FiraMath-Regular.clm2", "data/firamath/FiraMath-Regular.otf");
            const FontSrcFile fira_sans_boldItalic("data/firasans/FiraSansOT-BoldItalic.clm2", "data/firasans/FiraSansOT-BoldItalic.otf");
            const FontSrcFile fira_sans_regular("data/firasans/FiraSansOT-Regular.clm2", "data/firasans/FiraSansOT-Regular.otf");
            const FontSrcFile fira_sans_bold("data/firasans/FiraSansOT-Bold.clm2", "data/firasans/FiraSansOT-Bold.otf");
            const FontSrcFile fira_sans_italic("data/firasans/FiraSansOT-RegularItalic.clm2", "data/firasans/FiraSansOT-RegularItalic.otf");
            // auto auto_font = microtex::InitFontSenseAuto();

            MicroTeX::init(fira_math);
            // MicroTeX::addFont(math_bold);
            MicroTeX::addFont(fira_sans_regular);
            MicroTeX::addFont(fira_sans_bold);
            MicroTeX::addFont(fira_sans_italic);
            MicroTeX::addFont(fira_sans_boldItalic);
            MicroTeX::setDefaultMainFont(font_family);
            MicroTeX::setDefaultMathFont(font_name_math);

            PlatformFactory::registerFactory("abstract", std::make_unique<PlatformFactory_abstract>());
            PlatformFactory::activate("abstract");
            is_initialized = true;
            return "";
        }
        catch (std::exception& e) {
            return e.what();
        }
    }

    bool isInitialized() {
        return is_initialized;
    }

    void release() {
        microtex::MicroTeX::release();
        is_initialized = false;
    }

    LatexImage::LatexImage(const std::string& latex_src,
                           float font_size, float width, float line_space,
                           microtex::color text_color) {
        if (!is_initialized) {
            m_latex_error_msg = "LateX has not been initialized";
            return;
        }
        using namespace microtex;
        try {
            std::locale::global(std::locale(""));
            // Default width large enough

            m_render = MicroTeX::parse(
                latex_src,
                width, font_size, line_space, text_color,
                true,
                OverrideTeXStyle{ true, TexStyle::display },
                font_name_math
            );
            float height = m_render->getHeight(); // total height of the box = ascent + descent
            m_descent = m_render->getDepth();   // depth = descent
            m_ascent = height - m_descent;

            m_render->draw(m_graphics, 0.f, 0.f);
            // m_render->~Render();

        }
        catch (std::exception& e) {
            m_latex_error_msg = e.what();
        }
    }

    LatexImage::~LatexImage() {
        if (m_render != nullptr) {
            delete m_render;
        }
    }

    ImVec2 LatexImage::getDimensions() {
        if (m_latex_error_msg.empty())
            return ImVec2(ceil(m_render->getWidth()), ceil(m_render->getHeight()));
        else
            return ImVec2(0, 0);
    }

    bool LatexImage::render(ImVec2 scale, ImVec2 inner_padding, bool animate) {
        if (m_latex_error_msg.empty()) {
            m_painter.start(getDimensions(), scale, inner_padding);
            bool animating = m_painter.distributeCallListFadeIn(m_graphics.getCallList(), animate);
            m_painter.finish();
            return animating;
        }

        return false;
    }
}
