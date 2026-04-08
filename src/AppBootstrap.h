#pragma once

#include <bgfx/bgfx.h>
#include <GLFW/glfw3.h>

#include <memory>

struct ViewerState;

using GlfwWindowHandle = std::unique_ptr<GLFWwindow, void (*)(GLFWwindow *)>;

struct ViewerWindowCallbacks
{
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcursorposfun cursorPosition = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWdropfun drop = nullptr;
};

struct GlfwLibraryGuard
{
    bool initialized = false;

    ~GlfwLibraryGuard();
};

struct BgfxLibraryGuard
{
    bool initialized = false;

    ~BgfxLibraryGuard();
};

struct ImGuiGuard
{
    bool initialized = false;

    ~ImGuiGuard();
};

struct AppProgramHandles
{
    bgfx::ProgramHandle mainProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle pickProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle lineProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightDirectionUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightScaleUniform = BGFX_INVALID_HANDLE;

    ~AppProgramHandles();
};

[[nodiscard]] GlfwWindowHandle createViewerWindow(ViewerState &viewerState,
                                                  const ViewerWindowCallbacks &callbacks);
void configureBgfxPlatformData(bgfx::Init &init, GLFWwindow *window);
void updateCurrentFps(ViewerState &viewerState, float uiDeltaTime);
void beginViewerUiFrame(GLFWwindow *window, int width, int height,
                        ViewerState &viewerState, double &lastUiFrameTime,
                        float &uiScrollX, float &uiScrollY);
