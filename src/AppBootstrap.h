#pragma once

#include <bgfx/bgfx.h>
#include <GLFW/glfw3.h>

#include <memory>

struct ViewerState;

/// RAII handle for a GLFW window. Uses glfwDestroyWindow as the deleter.
using GlfwWindowHandle = std::unique_ptr<GLFWwindow, void (*)(GLFWwindow *)>;

/// GLFW input callback pointers grouped together for passing to
/// createViewerWindow().
struct ViewerWindowCallbacks
{
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcursorposfun cursorPosition = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWdropfun drop = nullptr;
};

/// RAII guard that calls glfwTerminate() on destruction when @p initialized is true.
struct GlfwLibraryGuard
{
    bool initialized = false;

    ~GlfwLibraryGuard();
};

/// RAII guard that calls bgfx::shutdown() on destruction when @p initialized is true.
struct BgfxLibraryGuard
{
    bool initialized = false;

    ~BgfxLibraryGuard();
};

/// RAII guard that shuts down the ImGui context on destruction when @p initialized is true.
struct ImGuiGuard
{
    bool initialized = false;

    ~ImGuiGuard();
};

/// Owns the bgfx shader programs and uniforms used throughout the application.
/// The destructor releases all handles.
struct AppProgramHandles
{
    bgfx::ProgramHandle mainProgram = BGFX_INVALID_HANDLE;  ///< Instanced particle shader.
    bgfx::ProgramHandle pickProgram = BGFX_INVALID_HANDLE;  ///< Off-screen picking shader.
    bgfx::ProgramHandle lineProgram = BGFX_INVALID_HANDLE;  ///< Line/bond rendering shader.
    bgfx::UniformHandle lightDirectionUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightScaleUniform     = BGFX_INVALID_HANDLE;

    ~AppProgramHandles();
};

/// Creates a GLFW window sized to 80% of the primary monitor, sets up all
/// callbacks from @p callbacks, and stores initial viewport dimensions in
/// @p viewerState. Returns an empty handle on failure.
[[nodiscard]] GlfwWindowHandle createViewerWindow(ViewerState &viewerState,
                                                  const ViewerWindowCallbacks &callbacks);

/// Populates @p init.platformData with the OS-specific window handles obtained
/// from @p window, so bgfx can create its rendering context.
void configureBgfxPlatformData(bgfx::Init &init, GLFWwindow *window);

/// Computes a smoothed FPS estimate from @p uiDeltaTime and stores it in
/// viewerState.currentFps.
void updateCurrentFps(ViewerState &viewerState, float uiDeltaTime);

/// Begins a new ImGui frame, feeding the current window size, mouse position,
/// delta time, and scroll state. Also computes the UI panel / render-viewport
/// widths and stores them in @p viewerState.
void beginViewerUiFrame(GLFWwindow *window, int width, int height,
                        ViewerState &viewerState, double &lastUiFrameTime,
                        float &uiScrollX, float &uiScrollY);
