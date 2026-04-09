cmake_minimum_required(VERSION 3.20)
project(ColloidVisualizationTool DESCRIPTION "Colloid Visualization Tool")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# Build only the bgfx library; skip tools, examples, and install targets.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL        OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/bgfx.cmake)

add_executable(cvt
    src/AppBootstrap.cpp
    src/ColorPalette.cpp
    src/SceneRenderSupport.cpp
    src/CubeType.cpp
    src/CylinderType.cpp
    src/ImGuiBgfx.cpp
    src/ImGuiCompat.cpp
    src/main.cpp
    src/ArrowType.cpp
    src/BondOrderScatter.cpp
    src/Mesh.cpp
    src/OctahedronType.cpp
    src/PatchCapType.cpp
    src/PatchConeType.cpp
    src/PatchPlacement.cpp
    src/Particle.cpp
    src/ParticleType.cpp
    src/ParticleSystem.cpp
    src/PolygonType.cpp
    src/RodType.cpp
    src/ScreenshotSupport.cpp
    src/SimulationBox.cpp
    src/SphereType.cpp
    src/StructureFactor.cpp
    src/TrajectoryReader.cpp
    src/ViewerSupport.cpp
    src/ViewerUi.cpp
    third_party/implot/implot.cpp
    third_party/implot/implot_items.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cvt PRIVATE
        $<$<CONFIG:Release>:-Ofast>
    )
endif()

target_include_directories(cvt PRIVATE
    ${BGFX_DIR}/3rdparty
    ${BGFX_DIR}/examples/common/imgui
    ${CMAKE_SOURCE_DIR}/third_party/implot
)

find_package(glfw3 CONFIG QUIET)

set(GLFW_TARGET "")
if(TARGET glfw)
    set(GLFW_TARGET glfw)
elseif(TARGET glfw3)
    set(GLFW_TARGET glfw3)
endif()

if(NOT GLFW_TARGET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW QUIET glfw3)
    endif()

    if(GLFW_FOUND)
        target_include_directories(cvt PRIVATE ${GLFW_INCLUDE_DIRS})
        target_link_libraries(cvt PRIVATE ${GLFW_LIBRARIES})
    else()
        message(FATAL_ERROR
            "GLFW 3 not found. Install a CMake package that provides target glfw/glfw3, "
            "or make glfw3 available through pkg-config.")
    endif()
endif()

target_link_libraries(cvt PRIVATE
    bgfx
    dear-imgui
)

if(GLFW_TARGET)
    target_link_libraries(cvt PRIVATE ${GLFW_TARGET})
endif()