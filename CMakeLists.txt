cmake_minimum_required(VERSION 3.10)
project(hmi_scene LANGUAGES CXX)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项优化（为 perf 火焰图保留栈帧）
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -DNDEBUG -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-omit-frame-pointer")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV2 REQUIRED glesv2)

find_package(Threads REQUIRED)

add_executable(hmi_scene
    main.cpp
    src/Application.cpp
    src/scene/gui/ImGuiLayer.cpp
    src/scene/Scene.cpp
    src/scene/fps/FpsCounter.cpp
    src/model/base/BaseModel.cpp
    src/model/instances/ImportedModel.cpp
    src/model/manager/ModelManager.cpp
    src/model/loader/GLTFLoader.cpp
    src/model/importer/ModelImporter.cpp
    src/model/controller/ModelController.cpp
    src/data/UDPDataManager.cpp
    src/scene/text/TextRenderer.cpp
    src/scene/audio/AudioPlayer.cpp

    # Dear ImGui core sources
    include/third_party/imgui/imgui.cpp
    include/third_party/imgui/imgui_draw.cpp
    include/third_party/imgui/imgui_tables.cpp
    include/third_party/imgui/imgui_widgets.cpp
    # include/third_party/imgui/imgui_demo.cpp  # 可选：示例窗口

    # Dear ImGui backends
    include/third_party/imgui/backends/imgui_impl_glfw.cpp
    include/third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(hmi_scene PRIVATE
    include
    include/scene
    include/scene/gui
    include/scene/text
    include/scene/fps
    include/scene/audio
    include/model
    include/model/loader
    include/model/importer
    include/model/controller
    include/data
    include/third_party
    include/third_party/imgui
    include/third_party/imgui/backends
    ${GLFW3_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
)

target_link_libraries(hmi_scene PRIVATE
    ${GLFW3_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
)

target_compile_options(hmi_scene PRIVATE
    ${GLFW3_CFLAGS_OTHER}
    ${EGL_CFLAGS_OTHER}
    ${GLESV2_CFLAGS_OTHER}
)

# Dear ImGui: 为 OpenGL ES 2 打开对应后端宏
target_compile_definitions(hmi_scene PRIVATE
    IMGUI_IMPL_OPENGL_ES2
)

# 添加jsoncpp依赖
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# 添加ALSA音频支持
pkg_check_modules(ALSA REQUIRED alsa)

# 添加Draco支持
find_library(DRACO_LIBRARY NAMES draco PATHS /usr/local/lib)
find_path(DRACO_INCLUDE_DIR NAMES draco/draco_features.h PATHS /usr/local/include)

target_link_libraries(hmi_scene PRIVATE
    ${JSONCPP_LIBRARIES}
    ${DRACO_LIBRARY}
    ${GLFW3_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
    ${ALSA_LIBRARIES}
    dl
    Threads::Threads
)

target_include_directories(hmi_scene PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
    ${DRACO_INCLUDE_DIR}
    ${ALSA_INCLUDE_DIRS}
    include
    include/scene
    include/scene/gui
    include/scene/text
    include/scene/fps
    include/scene/audio
    include/model
    include/model/loader
    include/model/importer
    include/model/controller
    include/data
    include/third_party
    include/third_party/imgui
    include/third_party/imgui/backends
    ${GLFW3_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
)
