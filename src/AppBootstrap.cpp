#include "AppBootstrap.h"

#include "ImGuiBgfx.h"
#include "Log.h"
#include "Version.h"
#include "ViewerSupport.h"

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#if defined(CVT_GLFW_HAS_WAYLAND)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if BX_PLATFORM_LINUX
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace
{

constexpr int kDefaultWindowWidth = 1536;
constexpr int kDefaultWindowHeight = 768;

/// Environment variable that makes CVT use X11 (XWayland on a Wayland
/// session) instead of native Wayland. Also set by the automatic restart.
constexpr const char *kForceX11EnvironmentVariable = "CVT_FORCE_X11";

/// argv of this process, kept for restartWithX11IfWaylandStartupFailed().
char **s_restartArguments = nullptr;
bool s_bgfxStartupInProgress = false;

#if BX_PLATFORM_LINUX
[[maybe_unused]] bool x11ForcedByEnvironment()
{
    const char *value = std::getenv(kForceX11EnvironmentVariable);
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

#if defined(CVT_GLFW_HAS_WAYLAND)
/// bgfx dlopen()s this library for Wayland surfaces and aborts if it is missing.
bool waylandEglLibraryAvailable()
{
    void *handle = dlopen("libwayland-egl.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr)
    {
        return false;
    }
    dlclose(handle);
    return true;
}

bool glfwRunsOnWayland()
{
    return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
}
#endif
#endif

} // namespace

bool initializeGlfw()
{
#if BX_PLATFORM_LINUX && defined(GLFW_PLATFORM)
    // GLFW 3.4+ headers: GLFW picks Wayland on a Wayland session unless told
    // otherwise. Ask for X11 when native Wayland cannot or should not be used.
#if defined(CVT_GLFW_HAS_WAYLAND)
    const bool requestX11 = x11ForcedByEnvironment() || !waylandEglLibraryAvailable();
#else
    const bool requestX11 = true; // no Wayland window handles available to pass to bgfx
#endif
    if (requestX11)
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        if (glfwInit() == GLFW_TRUE)
        {
            return true;
        }
        // No X server (e.g. a Wayland session without XWayland): let GLFW choose.
        cvt::log::warn() << "GLFW could not use X11; letting GLFW choose the platform."
                         << std::endl;
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
    }
#endif
    return glfwInit() == GLFW_TRUE;
}

void setRestartArguments(char **argv)
{
    s_restartArguments = argv;
}

void setBgfxStartupInProgress(bool inProgress)
{
    s_bgfxStartupInProgress = inProgress;
}

void restartWithX11IfWaylandStartupFailed()
{
#if BX_PLATFORM_LINUX && defined(CVT_GLFW_HAS_WAYLAND)
    // bgfx cannot recover from a failed EGL surface/context in-process, so start
    // over with X11. The environment variable also prevents a restart loop.
    if (!s_bgfxStartupInProgress || s_restartArguments == nullptr || !glfwRunsOnWayland()
        || x11ForcedByEnvironment())
    {
        return;
    }
    cvt::log::error() << "Graphics initialization failed on native Wayland; restarting CVT "
                         "with X11 (XWayland). Set "
                      << kForceX11EnvironmentVariable << "=1 to skip this attempt."
                      << std::endl;
    setenv(kForceX11EnvironmentVariable, "1", 1);
    execv("/proc/self/exe", s_restartArguments);
    cvt::log::error() << "Restart failed: " << std::strerror(errno) << std::endl;
#endif
}

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
                         CVT_WINDOW_TITLE, nullptr, nullptr),
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

bool configureBgfxPlatformData(bgfx::Init &init, GLFWwindow *window)
{
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
#if defined(CVT_GLFW_HAS_WAYLAND)
    if (glfwRunsOnWayland())
    {
        init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
        init.platformData.ndt = glfwGetWaylandDisplay();
        init.platformData.nwh = glfwGetWaylandWindow(window);
        if (init.platformData.ndt == nullptr || init.platformData.nwh == nullptr)
        {
            cvt::log::error() << "GLFW returned no Wayland window handle." << std::endl;
            return false;
        }
        cvt::log::info() << "Using native Wayland window." << std::endl;
        return true;
    }
#endif
    init.platformData.type = bgfx::NativeWindowHandleType::Default;
    init.platformData.ndt = glfwGetX11Display();
    init.platformData.nwh =
        reinterpret_cast<void *>(static_cast<uintptr_t>(glfwGetX11Window(window)));
    if (init.platformData.ndt == nullptr || init.platformData.nwh == nullptr)
    {
        // E.g. a GLFW running on Wayland that CVT was not built to support.
        cvt::log::error() << "GLFW returned no X11 window handle. If you are on a Wayland "
                             "session, try running with "
                          << kForceX11EnvironmentVariable << "=1." << std::endl;
        return false;
    }
#elif BX_PLATFORM_OSX
    init.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
    init.platformData.nwh = glfwGetWin32Window(window);
#endif
    return true;
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
