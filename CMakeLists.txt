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

# implot needs a flat imgui include ("imgui.h" without a subdirectory prefix) which
# conflicts with how ImGuiBgfx.cpp finds the bgfx example imgui.h.  Build implot as a
# separate static library so its include paths stay isolated.
add_library(implot STATIC
    third_party/implot/implot.cpp
    third_party/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/implot
    ${BGFX_DIR}/3rdparty/dear-imgui
)
target_link_libraries(implot PUBLIC bx)

add_executable(cvt
    src/AnalysisSupport.cpp
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
    src/VoronoiCellBuilder.cpp
    src/VoronoiType.cpp
    src/ViewerConfig.cpp
    src/ViewerSupport.cpp
    src/ViewerUi.cpp
    ${BGFX_DIR}/3rdparty/dear-imgui/imgui.cpp
    ${BGFX_DIR}/3rdparty/dear-imgui/imgui_draw.cpp
    ${BGFX_DIR}/3rdparty/dear-imgui/imgui_tables.cpp
    ${BGFX_DIR}/3rdparty/dear-imgui/imgui_widgets.cpp
)

target_compile_definitions(cvt PRIVATE
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cvt PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections>
        $<$<NOT:$<CONFIG:Debug>>:-fdata-sections>
        $<$<CONFIG:Release>:-Ofast>
    )
    target_link_options(cvt PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>
    )
    if(NOT MINGW)
        target_compile_options(cvt PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-flto=auto>
        )
        target_link_options(cvt PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-flto=auto>
        )
    endif()
endif()

target_include_directories(cvt PRIVATE
    ${BGFX_DIR}/3rdparty
    ${BGFX_DIR}/examples/common/imgui
)

# On MSVC, skip automatic system discovery: MinGW/MSYS2 packages are unusable with cl.exe.
# Users with a native MSVC GLFW install can set glfw3_DIR explicitly.
if(NOT MSVC)
    find_package(glfw3 CONFIG QUIET)
endif()

set(GLFW_TARGET "")
if(TARGET glfw)
    set(GLFW_TARGET glfw)
elseif(TARGET glfw3)
    set(GLFW_TARGET glfw3)
endif()

if(NOT GLFW_TARGET)
    if(NOT MSVC)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GLFW QUIET glfw3)
        endif()
    endif()

    if(GLFW_FOUND)
        target_include_directories(cvt PRIVATE ${GLFW_INCLUDE_DIRS})
        target_link_libraries(cvt PRIVATE ${GLFW_LIBRARIES})
    else()
        include(FetchContent)
        FetchContent_Declare(glfw3_src
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG        3.4
            GIT_SHALLOW    TRUE
        )
        set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw3_src)
        set(GLFW_TARGET glfw)
    endif()
endif()

target_link_libraries(cvt PRIVATE
    bgfx
    bx
    bimg
    implot
)

install(TARGETS cvt
    RUNTIME DESTINATION .
)

install(FILES
    ${CMAKE_SOURCE_DIR}/cvt.ini
    DESTINATION .
)

install(DIRECTORY
    ${CMAKE_SOURCE_DIR}/shaders
    DESTINATION .
)

install(DIRECTORY
    ${CMAKE_SOURCE_DIR}/TestInputFiles
    DESTINATION .
)

if(GLFW_TARGET)
    target_link_libraries(cvt PRIVATE ${GLFW_TARGET})
endif()

option(CVT_BUILD_SHADERS "Compile runtime shaders as part of the build" ON)

if(CVT_BUILD_SHADERS)
    if(WIN32)
        # Avoid picking Windows launcher shims (WSL/System32 and WindowsApps).
        if(DEFINED ENV{CVT_BASH_EXECUTABLE} AND EXISTS "$ENV{CVT_BASH_EXECUTABLE}")
            set(CVT_BASH_EXECUTABLE "$ENV{CVT_BASH_EXECUTABLE}")
        else()
            file(TO_CMAKE_PATH "$ENV{PATH}" CVT_ENV_PATH)
            set(CVT_BASH_EXECUTABLE "")
            foreach(CVT_PATH_DIR IN LISTS CVT_ENV_PATH)
                if(CVT_PATH_DIR MATCHES "[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/?$"
                    OR CVT_PATH_DIR MATCHES "[Mm]icrosoft/[Ww]indows[Aa]pps/?$")
                    continue()
                endif()
                foreach(CVT_SHELL_NAME bash.exe sh.exe bash sh)
                    if(EXISTS "${CVT_PATH_DIR}/${CVT_SHELL_NAME}")
                        set(CVT_BASH_EXECUTABLE "${CVT_PATH_DIR}/${CVT_SHELL_NAME}")
                        break()
                    endif()
                endforeach()
                if(CVT_BASH_EXECUTABLE)
                    break()
                endif()
            endforeach()
        endif()
    else()
        find_program(CVT_BASH_EXECUTABLE NAMES bash sh)
    endif()

    if(CVT_BASH_EXECUTABLE)
        set(CVT_SHADER_OUTPUTS
            ${CMAKE_SOURCE_DIR}/shaders/glsl/vs_instancing.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/fs_instancing.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/vs_picking.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/fs_picking.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/vs_lines.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/fs_lines.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/vs_structure_factor.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/fs_structure_factor.bin
            ${CMAKE_SOURCE_DIR}/shaders/glsl/fs_structure_factor_color.bin
        )

        if(WIN32)
            list(APPEND CVT_SHADER_OUTPUTS
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/vs_instancing.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/fs_instancing.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/vs_picking.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/fs_picking.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/vs_lines.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/fs_lines.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/vs_structure_factor.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/fs_structure_factor.bin
                ${CMAKE_SOURCE_DIR}/shaders/dxbc/fs_structure_factor_color.bin
            )
        elseif(APPLE)
            list(APPEND CVT_SHADER_OUTPUTS
                ${CMAKE_SOURCE_DIR}/shaders/metal/vs_instancing.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/fs_instancing.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/vs_picking.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/fs_picking.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/vs_lines.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/fs_lines.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/vs_structure_factor.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/fs_structure_factor.bin
                ${CMAKE_SOURCE_DIR}/shaders/metal/fs_structure_factor_color.bin
            )
        endif()

        set(CVT_SHADER_STAMP ${CMAKE_CURRENT_BINARY_DIR}/cvt-shaders.stamp)
        add_custom_command(
            OUTPUT ${CVT_SHADER_STAMP}
            COMMAND ${CVT_BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/compileshaders.sh
            COMMAND ${CMAKE_COMMAND} -E touch ${CVT_SHADER_STAMP}
            DEPENDS
                ${CMAKE_SOURCE_DIR}/compileshaders.sh
                ${CMAKE_SOURCE_DIR}/shaders/varying.def.sc
                ${CMAKE_SOURCE_DIR}/shaders/vs_instancing.sc
                ${CMAKE_SOURCE_DIR}/shaders/fs_instancing.sc
                ${CMAKE_SOURCE_DIR}/shaders/vs_picking.sc
                ${CMAKE_SOURCE_DIR}/shaders/fs_picking.sc
                ${CMAKE_SOURCE_DIR}/shaders/vs_lines.sc
                ${CMAKE_SOURCE_DIR}/shaders/fs_lines.sc
                ${CMAKE_SOURCE_DIR}/shaders/vs_structure_factor.sc
                ${CMAKE_SOURCE_DIR}/shaders/fs_structure_factor.sc
                ${CMAKE_SOURCE_DIR}/shaders/fs_structure_factor_color.sc
            BYPRODUCTS ${CVT_SHADER_OUTPUTS}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Compiling runtime shaders"
            VERBATIM
        )

        add_custom_target(cvt-shaders DEPENDS ${CVT_SHADER_STAMP})
        add_dependencies(cvt cvt-shaders)
    elseif(MSVC)
        # No POSIX shell found, but building with MSVC: use the PowerShell companion script.
        find_program(CVT_POWERSHELL_EXECUTABLE NAMES powershell.exe pwsh.exe
            PATHS "C:/Windows/System32/WindowsPowerShell/v1.0" "C:/Program Files/PowerShell"
            NO_DEFAULT_PATH)
        if(NOT CVT_POWERSHELL_EXECUTABLE)
            find_program(CVT_POWERSHELL_EXECUTABLE NAMES powershell.exe pwsh.exe)
        endif()

        if(CVT_POWERSHELL_EXECUTABLE)
            set(CVT_SHADER_STAMP ${CMAKE_CURRENT_BINARY_DIR}/cvt-shaders.stamp)
            add_custom_command(
                OUTPUT ${CVT_SHADER_STAMP}
                COMMAND ${CVT_POWERSHELL_EXECUTABLE}
                    -ExecutionPolicy Bypass
                    -NonInteractive
                    -File "${CMAKE_SOURCE_DIR}/compileshaders.ps1"
                COMMAND ${CMAKE_COMMAND} -E touch ${CVT_SHADER_STAMP}
                DEPENDS
                    ${CMAKE_SOURCE_DIR}/compileshaders.ps1
                    ${CMAKE_SOURCE_DIR}/shaders/varying.def.sc
                    ${CMAKE_SOURCE_DIR}/shaders/vs_instancing.sc
                    ${CMAKE_SOURCE_DIR}/shaders/fs_instancing.sc
                    ${CMAKE_SOURCE_DIR}/shaders/vs_picking.sc
                    ${CMAKE_SOURCE_DIR}/shaders/fs_picking.sc
                    ${CMAKE_SOURCE_DIR}/shaders/vs_lines.sc
                    ${CMAKE_SOURCE_DIR}/shaders/fs_lines.sc
                    ${CMAKE_SOURCE_DIR}/shaders/vs_structure_factor.sc
                    ${CMAKE_SOURCE_DIR}/shaders/fs_structure_factor.sc
                    ${CMAKE_SOURCE_DIR}/shaders/fs_structure_factor_color.sc
                BYPRODUCTS ${CVT_SHADER_OUTPUTS}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Compiling runtime shaders (PowerShell)"
                VERBATIM
            )
            add_custom_target(cvt-shaders DEPENDS ${CVT_SHADER_STAMP})
            add_dependencies(cvt cvt-shaders)
        else()
            message(WARNING
                "CVT_BUILD_SHADERS is ON but neither a POSIX shell nor PowerShell was found. "
                "Build will continue without shader compilation; run compileshaders.ps1 manually.")
        endif()
    else()
        message(WARNING
            "CVT_BUILD_SHADERS is ON but no POSIX shell executable was found in PATH. "
            "On Windows, ensure MSYS2/Git bash is in PATH and avoid using C:/Windows/System32/bash.exe or WindowsApps/bash.exe. "
            "You can force a specific shell by setting CVT_BASH_EXECUTABLE in the environment. "
            "Build will continue without shader compilation; run compileshaders.sh manually.")
    endif()
endif()