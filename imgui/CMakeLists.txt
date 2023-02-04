set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

# imgui
#
add_library(
    imgui
    include/imgui/dirent_win32.h
    include/imgui/imconfig.h
    include/imgui/imgui_impl_opengl3_loader.h
    include/imgui/imgui_internal.h
    include/imgui/ImGuiFileDialogConfig.h
    include/imgui/imstb_rectpack.h
    include/imgui/imstb_textedit.h
    include/imgui/imstb_truetype.h
    include/imgui/stb_image_resize.h

    include/imgui/imgui_impl_glfw.h
    source/imgui_impl_glfw.cpp
    include/imgui/imgui_impl_opengl3.h
    source/imgui_impl_opengl3.cpp
    include/imgui/imgui.h
    source/imgui.cpp
    include/imgui/ImGuiFileDialog.h
    source/ImGuiFileDialog.cpp

    source/imgui_demo.cpp
    source/imgui_draw.cpp
    source/imgui_tables.cpp
    source/imgui_widgets.cpp
)

target_compile_definitions(
    imgui PUBLIC
    USE_STD_FILESYSTEM
)

target_include_directories(
    imgui PUBLIC
    include/imgui
)

target_link_libraries(
    imgui
    glfw
    ${OPENGL_LIBRARIES}
)
