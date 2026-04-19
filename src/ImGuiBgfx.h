#pragma once

#include <bgfx/bgfx.h>

struct GLFWwindow;

/// Integration layer between Dear ImGui and bgfx. Must be initialised once
/// at startup with create() and shut down with destroy() before bgfx::shutdown().
namespace ImGuiBgfx
{

/// Creates the ImGui bgfx backend using @p viewId as the dedicated render view.
/// Returns false if initialisation fails.
bool create(bgfx::ViewId viewId);

/// Destroys all bgfx resources created by create().
void destroy();

/// Begins a new ImGui frame. Call once per render loop iteration before
/// constructing any ImGui widgets.
/// @param scrollX / @p scrollY  Accumulated scroll deltas since the last frame.
void beginFrame(GLFWwindow *window, uint16_t width, uint16_t height,
                double mouseX, double mouseY, float deltaTime,
                float scrollX, float scrollY);

/// Finalises the ImGui frame and submits the draw data to bgfx.
void endFrame();

/// Forwards a Unicode code-point typed by the user to ImGui's input queue.
void addInputCharacter(unsigned int codePoint);

/// Forwards a raw GLFW mouse-button event to ImGui's input queue.
void addMouseButtonEvent(int button, bool pressed);

/// Returns true after create() has succeeded and before destroy() is called.
bool isAvailable();

/// Returns true when ImGui wants to consume mouse events (pointer is over a
/// UI panel), so the 3-D scene should ignore mouse input.
bool wantsMouseCapture();

/// Returns true when ImGui wants to consume keyboard events (a text field is
/// focused), so the 3-D scene should ignore key input.
bool wantsKeyboardCapture();

} // namespace ImGuiBgfx