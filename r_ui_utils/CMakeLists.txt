
# r_ui_utils
#
add_library(
    r_ui_utils
    include/r_ui_utils/stb_image.h
    source/stb_image.cpp
    include/r_ui_utils/font_catalog.h
    source/font_catalog.cpp
    include/r_ui_utils/texture_loader.h
    source/texture_loader.cpp
    include/r_ui_utils/utils.h
    source/utils.cpp

    source/roboto.c
)

target_include_directories(
    r_ui_utils PUBLIC
    include
    ../imgui/include
    ../r_utils/include
    ${GSTREAMER_INCLUDE_DIRS}
)

target_link_directories(
    r_ui_utils PUBLIC
    ${GSTREAMER_LIB_DIRS}
)

target_link_libraries(
    r_ui_utils
    imgui
    r_utils
    ${GSTREAMER_LIBS}
)
