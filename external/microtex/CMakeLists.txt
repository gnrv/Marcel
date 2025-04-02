set(HAVE_LOG OFF)
set(HAVE_AUTO_FONT_FIND ON)
set(GLYPH_RENDER_TYPE OFF)

# set(BUILD_STATIC ON)
add_subdirectory(microtex)

# set_target_properties(
#     microtex-imgui PROPERTIES
#     VERSION ${PROJECT_VERSION}
#     SOVERSION ${MICROTEX_API_VERSION}
#     CXX_VISIBILITY_PRESET hidden
# )
