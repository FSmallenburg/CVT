cmake_minimum_required(VERSION 3.10)
project(MyBgfxProject)

set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(BX_CONFIG_DEBUG=1)

set(BGFX_DIR "${CMAKE_SOURCE_DIR}/third_party/bgfx" CACHE PATH "Path to the vendored bgfx source tree")
set(BX_DIR "${CMAKE_SOURCE_DIR}/third_party/bx" CACHE PATH "Path to the vendored bx source tree")
set(BIMG_DIR "${CMAKE_SOURCE_DIR}/third_party/bimg" CACHE PATH "Path to the vendored bimg source tree")
set(BGFX_BUILD_DIR "${BGFX_DIR}/.build/linux64_gcc" CACHE PATH "Path to the bgfx build output directory")

set(BGFX_LIB ${BGFX_BUILD_DIR}/bin/libbgfxRelease.a)
set(BX_LIB ${BGFX_BUILD_DIR}/bin/libbxRelease.a)
set(BIMG_LIB ${BGFX_BUILD_DIR}/bin/libbimgRelease.a)

add_executable(my_app
    src/ColorPalette.cpp
    src/main.cpp
    src/ArrowType.cpp
    src/Mesh.cpp
    src/Particle.cpp
    src/ParticleType.cpp
    src/ParticleSystem.cpp
    src/RodType.cpp
    src/ScreenshotSupport.cpp
    src/SimulationBox.cpp
    src/SphereType.cpp
    src/TrajectoryReader.cpp
    src/ViewerSupport.cpp
)
target_include_directories(my_app PRIVATE 
    ${BGFX_DIR}/include
    ${BX_DIR}/include
    ${BIMG_DIR}/include
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

target_include_directories(my_app PRIVATE ${GLFW_INCLUDE_DIRS})

if(NOT EXISTS "${BGFX_DIR}/include/bgfx/bgfx.h")
    message(FATAL_ERROR "bgfx headers not found at ${BGFX_DIR}. Initialize submodules or override BGFX_DIR.")
endif()

if(NOT EXISTS "${BX_DIR}/include/bx/bx.h")
    message(FATAL_ERROR "bx headers not found at ${BX_DIR}. Initialize submodules or override BX_DIR.")
endif()

if(NOT EXISTS "${BIMG_DIR}/include/bimg/bimg.h")
    message(FATAL_ERROR "bimg headers not found at ${BIMG_DIR}. Initialize submodules or override BIMG_DIR.")
endif()

if(NOT EXISTS "${BGFX_LIB}")
    message(FATAL_ERROR "libbgfxRelease.a not found at ${BGFX_LIB}. Build bgfx or override BGFX_BUILD_DIR.")
endif()

if(NOT EXISTS "${BX_LIB}")
    message(FATAL_ERROR "libbxRelease.a not found at ${BX_LIB}. Build bgfx or override BGFX_BUILD_DIR.")
endif()

if(NOT EXISTS "${BIMG_LIB}")
    message(FATAL_ERROR "libbimgRelease.a not found at ${BIMG_LIB}. Build bgfx or override BGFX_BUILD_DIR.")
endif()

target_link_libraries(my_app PRIVATE
    ${BGFX_LIB}
    ${BIMG_LIB}
    ${BX_LIB}
    ${GLFW_LIBRARIES}
)