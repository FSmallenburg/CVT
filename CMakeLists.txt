cmake_minimum_required(VERSION 3.10)
project(ColloidVisualizationTool DESCRIPTION "Colloid Visualization Tool")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

set(BGFX_DIR "${CMAKE_SOURCE_DIR}/third_party/bgfx" CACHE PATH "Path to the vendored bgfx source tree")
set(BX_DIR "${CMAKE_SOURCE_DIR}/third_party/bx" CACHE PATH "Path to the vendored bx source tree")
set(BIMG_DIR "${CMAKE_SOURCE_DIR}/third_party/bimg" CACHE PATH "Path to the vendored bimg source tree")

set(_bgfx_release_suffix "Release${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_bgfx_lib_prefix "${CMAKE_STATIC_LIBRARY_PREFIX}")
set(_bgfx_lib_name "${_bgfx_lib_prefix}bgfx${_bgfx_release_suffix}")
set(_bx_lib_name "${_bgfx_lib_prefix}bx${_bgfx_release_suffix}")
set(_bimg_lib_name "${_bgfx_lib_prefix}bimg${_bgfx_release_suffix}")



set(_bgfx_candidate_build_dirs "")
file(GLOB _bgfx_candidate_bin_dirs LIST_DIRECTORIES true "${BGFX_DIR}/.build/*/bin")
foreach(_bgfx_candidate_bin_dir IN LISTS _bgfx_candidate_bin_dirs)
    if(EXISTS "${_bgfx_candidate_bin_dir}/${_bgfx_lib_name}"
       AND EXISTS "${_bgfx_candidate_bin_dir}/${_bx_lib_name}"
       AND EXISTS "${_bgfx_candidate_bin_dir}/${_bimg_lib_name}")
        get_filename_component(_bgfx_candidate_build_dir "${_bgfx_candidate_bin_dir}" DIRECTORY)
        list(APPEND _bgfx_candidate_build_dirs "${_bgfx_candidate_build_dir}")
    endif()
endforeach()

set(_bgfx_auto_build_dir "")
if(_bgfx_candidate_build_dirs)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_bgfx_arch_hint "64")
    else()
        set(_bgfx_arch_hint "32")
    endif()

    foreach(_bgfx_candidate_build_dir IN LISTS _bgfx_candidate_build_dirs)
        get_filename_component(_bgfx_candidate_name "${_bgfx_candidate_build_dir}" NAME)
        if(_bgfx_candidate_name MATCHES ".*${_bgfx_arch_hint}.*")
            set(_bgfx_auto_build_dir "${_bgfx_candidate_build_dir}")
            break()
        endif()
    endforeach()

    if(NOT _bgfx_auto_build_dir)
        list(LENGTH _bgfx_candidate_build_dirs _bgfx_candidate_count)
        if(_bgfx_candidate_count GREATER 1)
            message(FATAL_ERROR
                "Multiple bgfx build directories were found under ${BGFX_DIR}/.build. "
                "Set BGFX_BUILD_DIR explicitly.")
        endif()
        list(GET _bgfx_candidate_build_dirs 0 _bgfx_auto_build_dir)
    endif()
endif()

set(BGFX_BUILD_DIR "" CACHE PATH "Path to the bgfx build output directory")
if(NOT BGFX_BUILD_DIR AND _bgfx_auto_build_dir)
    set(BGFX_BUILD_DIR "${_bgfx_auto_build_dir}" CACHE PATH "Path to the bgfx build output directory" FORCE)
endif()

set(BGFX_LIB "${BGFX_BUILD_DIR}/bin/${_bgfx_lib_name}")
set(BX_LIB "${BGFX_BUILD_DIR}/bin/${_bx_lib_name}")
set(BIMG_LIB "${BGFX_BUILD_DIR}/bin/${_bimg_lib_name}")

add_executable(cvt
    src/ColorPalette.cpp
    src/CubeType.cpp
    src/CylinderType.cpp
    src/ImGuiBgfx.cpp
    src/ImGuiCompat.cpp
    src/main.cpp
    src/ArrowType.cpp
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
    third_party/bgfx/3rdparty/dear-imgui/imgui.cpp
    third_party/bgfx/3rdparty/dear-imgui/imgui_draw.cpp
    third_party/bgfx/3rdparty/dear-imgui/imgui_tables.cpp
    third_party/bgfx/3rdparty/dear-imgui/imgui_widgets.cpp
)

target_compile_definitions(cvt PRIVATE
    $<$<CONFIG:Debug>:BX_CONFIG_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:BX_CONFIG_DEBUG=0>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cvt PRIVATE
        $<$<CONFIG:Release>:-Ofast>
    )
endif()

if(MINGW)
    target_include_directories(cvt PRIVATE
        ${BX_DIR}/include/compat/mingw
    )
endif()

if(MSVC)
    target_include_directories(cvt PRIVATE
        ${BX_DIR}/include/compat/msvc
    )
endif()

target_include_directories(cvt PRIVATE 
    ${BGFX_DIR}/include
    ${BGFX_DIR}/3rdparty
    ${BGFX_DIR}/examples/common/imgui
    ${BX_DIR}/include
    ${BIMG_DIR}/include
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

target_link_libraries(cvt PRIVATE
    ${BGFX_LIB}
    ${BIMG_LIB}
    ${BX_LIB}
)

if(GLFW_TARGET)
    target_link_libraries(cvt PRIVATE ${GLFW_TARGET})
endif()