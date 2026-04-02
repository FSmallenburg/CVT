#pragma once

#include <bgfx/bgfx.h>

struct GLFWwindow;

namespace ImGuiBgfx
{

bool create(bgfx::ViewId viewId);
void destroy();
void beginFrame(GLFWwindow *window, uint16_t width, uint16_t height,
                double mouseX, double mouseY, float deltaTime,
                float scrollX, float scrollY);
void endFrame();
void addInputCharacter(unsigned int codePoint);
void addMouseButtonEvent(int button, bool pressed);
bool isAvailable();
bool wantsMouseCapture();
bool wantsKeyboardCapture();

} // namespace ImGuiBgfx