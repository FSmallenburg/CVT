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

/// Calls glfwInit(). On Linux with GLFW 3.4+, first requests the X11 platform
/// when native Wayland is unavailable or disabled (CVT_FORCE_X11=1, CVT built
/// without Wayland support, or libwayland-egl missing), and falls back to
/// GLFW's own choice if X11 cannot be used. Returns false if GLFW fails.
[[nodiscard]] bool initializeGlfw();

/// Populates @p init.platformData with the OS-specific window handles obtained
/// from @p window, so bgfx can create its rendering context. On Linux this
/// passes Wayland handles when GLFW runs on Wayland, X11 handles otherwise.
/// Returns false (after logging) if GLFW provides no usable handle.
[[nodiscard]] bool configureBgfxPlatformData(bgfx::Init &init, GLFWwindow *window);

/// Stores @p argv (null-terminated, from main) for restartWithX11IfWaylandStartupFailed().
void setRestartArguments(char **argv);

/// Marks the span around bgfx::init(), during which a fatal bgfx error may
/// trigger restartWithX11IfWaylandStartupFailed().
void setBgfxStartupInProgress(bool inProgress);

/// If bgfx is starting up on native Wayland (and X11 was not already forced),
/// re-executes CVT with CVT_FORCE_X11=1 so it runs on X11/XWayland instead.
/// Does not return on success; returns without effect otherwise. Called on
/// bgfx init failure and from the bgfx fatal callback, since bgfx cannot
/// recover from a failed EGL surface within the process.
void restartWithX11IfWaylandStartupFailed();

/// Computes a smoothed FPS estimate from @p uiDeltaTime and stores it in
/// viewerState.currentFps.
void updateCurrentFps(ViewerState &viewerState, float uiDeltaTime);

/// Begins a new ImGui frame, feeding the current window size, mouse position,
/// delta time, and scroll state. Also computes the UI panel / render-viewport
/// widths and stores them in @p viewerState.
void beginViewerUiFrame(GLFWwindow *window, int width, int height,
                        ViewerState &viewerState, double &lastUiFrameTime,
                        float &uiScrollX, float &uiScrollY);
