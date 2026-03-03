cmake_minimum_required(VERSION 3.10)
project(hmi_scene LANGUAGES CXX)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项优化
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV2 REQUIRED glesv2)

find_package(Threads REQUIRED)

add_executable(hmi_scene
    main.cpp
    src/Scene.cpp
    src/FpsCounter.cpp
    src/model/BaseModel.cpp
    src/model/ImportedModel.cpp
    src/model/ModelManager.cpp
    src/loader/GLTFLoader.cpp
    src/importer/ModelImporter.cpp
    src/controller/ModelController.cpp
    src/controller/UDPDataManager.cpp
    src/ui/TextRenderer.cpp
)

target_include_directories(hmi_scene PRIVATE
    include
    include/loader
    include/importer
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

# 添加jsoncpp依赖
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# 添加Draco支持
find_library(DRACO_LIBRARY NAMES draco PATHS /usr/local/lib)
find_path(DRACO_INCLUDE_DIR NAMES draco/draco_features.h PATHS /usr/local/include)

target_link_libraries(hmi_scene PRIVATE
    ${JSONCPP_LIBRARIES}
    ${DRACO_LIBRARY}
    ${GLFW3_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
    Threads::Threads
)

target_include_directories(hmi_scene PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
    ${DRACO_INCLUDE_DIR}
    include
    include/loader
    include/importer
    ${GLFW3_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
)
