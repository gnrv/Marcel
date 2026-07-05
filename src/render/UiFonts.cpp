#include "UiFonts.h"

#include "imgui.h"
#include "IconsMaterialDesignIcons.h"

UiFonts LoadUiFonts(ImFontAtlas *atlas, float dpi_scale)
{
    UiFonts fonts;

    const float base_font_size = 16.0f;
    fonts.fira_sans = atlas->AddFontFromFileTTF("../data/fonts/fira/FiraSans-Regular.ttf", base_font_size*dpi_scale);

    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphMinAdvanceX = base_font_size; // Use if you want to make the icon monospaced
    static const ImWchar icon_ranges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };
    atlas->AddFontFromFileTTF("../data/fonts/material-design-icons/materialdesignicons-webfont.ttf", base_font_size*dpi_scale, &config, icon_ranges);

    // Presentation sizes
    fonts.fira_sans_big = atlas->AddFontFromFileTTF("../data/fonts/fira/FiraSans-Regular.ttf", 48.0f*dpi_scale);
    fonts.fira_sans_small = atlas->AddFontFromFileTTF("../data/fonts/fira/FiraSans-Regular.ttf", 32.0f*dpi_scale);

    fonts.fira_mono = atlas->AddFontFromFileTTF("../data/fonts/fira/FiraMono-Regular.ttf", base_font_size*dpi_scale);

    return fonts;
}
