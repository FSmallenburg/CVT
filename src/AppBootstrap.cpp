#include "AppBootstrap.h"

#include "ImGuiBgfx.h"
#include "ViewerSupport.h"

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include <cmath>
#include <cstdint>

namespace
{

constexpr int kDefaultWindowWidth = 1536;
constexpr int kDefaultWindowHeight = 768;

} // namespace

GlfwLibraryGuard::~GlfwLibraryGuard()
{
    if (initialized)
    {
        glfwTerminate();
    }
}

BgfxLibraryGuard::~BgfxLibraryGuard()
{
    if (initialized)
    {
        bgfx::shutdown();
    }
}

ImGuiGuard::~ImGuiGuard()
{
    if (initialized)
    {
        ImGuiBgfx::destroy();
    }
}

AppProgramHandles::~AppProgramHandles()
{
    if (bgfx::isValid(pickProgram))
    {
        bgfx::destroy(pickProgram);
    }
    if (bgfx::isValid(lineProgram))
    {
        bgfx::destroy(lineProgram);
    }
    if (bgfx::isValid(mainProgram))
    {
        bgfx::destroy(mainProgram);
    }
    if (bgfx::isValid(lightDirectionUniform))
    {
        bgfx::destroy(lightDirectionUniform);
    }
    if (bgfx::isValid(lightScaleUniform))
    {
        bgfx::destroy(lightScaleUniform);
    }
}

GlfwWindowHandle createViewerWindow(ViewerState &viewerState,
                                    const ViewerWindowCallbacks &callbacks)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GlfwWindowHandle window(
        glfwCreateWindow(kDefaultWindowWidth, kDefaultWindowHeight,
                         "Colloid Visualization Tool", nullptr, nullptr),
        &glfwDestroyWindow);
    if (!window)
    {
        return {nullptr, &glfwDestroyWindow};
    }

    glfwSetWindowUserPointer(window.get(), &viewerState);
    glfwSetKeyCallback(window.get(), callbacks.key);
    glfwSetCharCallback(window.get(), callbacks.character);
    glfwSetCursorPosCallback(window.get(), callbacks.cursorPosition);
    glfwSetMouseButtonCallback(window.get(), callbacks.mouseButton);
    glfwSetScrollCallback(window.get(), callbacks.scroll);
    glfwSetDropCallback(window.get(), callbacks.drop);
    return window;
}

void configureBgfxPlatformData(bgfx::Init &init, GLFWwindow *window)
{
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
    init.platformData.ndt = glfwGetX11Display();
    init.platformData.nwh =
        reinterpret_cast<void *>(static_cast<uintptr_t>(glfwGetX11Window(window)));
#elif BX_PLATFORM_OSX
    init.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
    init.platformData.nwh = glfwGetWin32Window(window);
#endif
}

void updateCurrentFps(ViewerState &viewerState, float uiDeltaTime)
{
    const float instantaneousFps = uiDeltaTime > 1.0e-6f ? 1.0f / uiDeltaTime : 0.0f;
    if (viewerState.currentFps <= 0.0f)
    {
        viewerState.currentFps = instantaneousFps;
        return;
    }

    constexpr float kFpsSmoothingTimeConstant = 0.25f;
    const float smoothingAlpha =
        1.0f - std::exp(-uiDeltaTime / kFpsSmoothingTimeConstant);
    viewerState.currentFps += smoothingAlpha * (instantaneousFps - viewerState.currentFps);
}

void beginViewerUiFrame(GLFWwindow *window, int width, int height,
                        ViewerState &viewerState, double &lastUiFrameTime,
                        float &uiScrollX, float &uiScrollY)
{
    const double currentUiFrameTime = glfwGetTime();
    const float uiDeltaTime = static_cast<float>(currentUiFrameTime - lastUiFrameTime);
    lastUiFrameTime = currentUiFrameTime;

    updateCurrentFps(viewerState, uiDeltaTime);
    ImGuiBgfx::beginFrame(window, static_cast<uint16_t>(width),
                          static_cast<uint16_t>(height),
                          viewerState.mouseX, viewerState.mouseY,
                          uiDeltaTime, uiScrollX, uiScrollY);
    uiScrollX = 0.0f;
    uiScrollY = 0.0f;
}
