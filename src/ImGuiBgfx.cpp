#include "ImGuiBgfx.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <dear-imgui/imgui.h>
#include <GLFW/glfw3.h>

#include "fs_ocornut_imgui.bin.h"
#include "imgui.h"
#include "roboto_regular.ttf.h"
#include "vs_ocornut_imgui.bin.h"

#include <array>
#include <cstring>
#include <cstdint>
#include <vector>

namespace
{

static const bgfx::EmbeddedShader s_embeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END(),
};

bool checkAvailTransientBuffers(uint32_t numVertices, const bgfx::VertexLayout &layout,
                                uint32_t numIndices)
{
    return numVertices == bgfx::getAvailTransientVertexBuffer(numVertices, layout)
           && (numIndices == 0u
               || numIndices == bgfx::getAvailTransientIndexBuffer(numIndices));
}

void *imguiAlloc(size_t size, void *userData)
{
    bx::AllocatorI *allocator = static_cast<bx::AllocatorI *>(userData);
    return bx::alloc(allocator, size);
}

void imguiFree(void *pointer, void *userData)
{
    bx::AllocatorI *allocator = static_cast<bx::AllocatorI *>(userData);
    bx::free(allocator, pointer);
}

ImGuiKey mapGlfwKey(int key)
{
    switch (key)
    {
    case GLFW_KEY_TAB:
        return ImGuiKey_Tab;
    case GLFW_KEY_LEFT:
        return ImGuiKey_LeftArrow;
    case GLFW_KEY_RIGHT:
        return ImGuiKey_RightArrow;
    case GLFW_KEY_UP:
        return ImGuiKey_UpArrow;
    case GLFW_KEY_DOWN:
        return ImGuiKey_DownArrow;
    case GLFW_KEY_PAGE_UP:
        return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return ImGuiKey_PageDown;
    case GLFW_KEY_HOME:
        return ImGuiKey_Home;
    case GLFW_KEY_END:
        return ImGuiKey_End;
    case GLFW_KEY_INSERT:
        return ImGuiKey_Insert;
    case GLFW_KEY_DELETE:
        return ImGuiKey_Delete;
    case GLFW_KEY_BACKSPACE:
        return ImGuiKey_Backspace;
    case GLFW_KEY_SPACE:
        return ImGuiKey_Space;
    case GLFW_KEY_ENTER:
        return ImGuiKey_Enter;
    case GLFW_KEY_ESCAPE:
        return ImGuiKey_Escape;
    case GLFW_KEY_APOSTROPHE:
        return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA:
        return ImGuiKey_Comma;
    case GLFW_KEY_MINUS:
        return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD:
        return ImGuiKey_Period;
    case GLFW_KEY_SLASH:
        return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON:
        return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL:
        return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET:
        return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH:
        return ImGuiKey_Backslash;
    case GLFW_KEY_RIGHT_BRACKET:
        return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT:
        return ImGuiKey_GraveAccent;
    case GLFW_KEY_CAPS_LOCK:
        return ImGuiKey_CapsLock;
    case GLFW_KEY_SCROLL_LOCK:
        return ImGuiKey_ScrollLock;
    case GLFW_KEY_NUM_LOCK:
        return ImGuiKey_NumLock;
    case GLFW_KEY_PRINT_SCREEN:
        return ImGuiKey_PrintScreen;
    case GLFW_KEY_PAUSE:
        return ImGuiKey_Pause;
    case GLFW_KEY_KP_0:
        return ImGuiKey_Keypad0;
    case GLFW_KEY_KP_1:
        return ImGuiKey_Keypad1;
    case GLFW_KEY_KP_2:
        return ImGuiKey_Keypad2;
    case GLFW_KEY_KP_3:
        return ImGuiKey_Keypad3;
    case GLFW_KEY_KP_4:
        return ImGuiKey_Keypad4;
    case GLFW_KEY_KP_5:
        return ImGuiKey_Keypad5;
    case GLFW_KEY_KP_6:
        return ImGuiKey_Keypad6;
    case GLFW_KEY_KP_7:
        return ImGuiKey_Keypad7;
    case GLFW_KEY_KP_8:
        return ImGuiKey_Keypad8;
    case GLFW_KEY_KP_9:
        return ImGuiKey_Keypad9;
    case GLFW_KEY_KP_DECIMAL:
        return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT:
        return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD:
        return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER:
        return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL:
        return ImGuiKey_KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT:
        return ImGuiKey_LeftShift;
    case GLFW_KEY_LEFT_CONTROL:
        return ImGuiKey_LeftCtrl;
    case GLFW_KEY_LEFT_ALT:
        return ImGuiKey_LeftAlt;
    case GLFW_KEY_LEFT_SUPER:
        return ImGuiKey_LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT:
        return ImGuiKey_RightShift;
    case GLFW_KEY_RIGHT_CONTROL:
        return ImGuiKey_RightCtrl;
    case GLFW_KEY_RIGHT_ALT:
        return ImGuiKey_RightAlt;
    case GLFW_KEY_RIGHT_SUPER:
        return ImGuiKey_RightSuper;
    case GLFW_KEY_MENU:
        return ImGuiKey_Menu;
    case GLFW_KEY_0:
        return ImGuiKey_0;
    case GLFW_KEY_1:
        return ImGuiKey_1;
    case GLFW_KEY_2:
        return ImGuiKey_2;
    case GLFW_KEY_3:
        return ImGuiKey_3;
    case GLFW_KEY_4:
        return ImGuiKey_4;
    case GLFW_KEY_5:
        return ImGuiKey_5;
    case GLFW_KEY_6:
        return ImGuiKey_6;
    case GLFW_KEY_7:
        return ImGuiKey_7;
    case GLFW_KEY_8:
        return ImGuiKey_8;
    case GLFW_KEY_9:
        return ImGuiKey_9;
    case GLFW_KEY_A:
        return ImGuiKey_A;
    case GLFW_KEY_B:
        return ImGuiKey_B;
    case GLFW_KEY_C:
        return ImGuiKey_C;
    case GLFW_KEY_D:
        return ImGuiKey_D;
    case GLFW_KEY_E:
        return ImGuiKey_E;
    case GLFW_KEY_F:
        return ImGuiKey_F;
    case GLFW_KEY_G:
        return ImGuiKey_G;
    case GLFW_KEY_H:
        return ImGuiKey_H;
    case GLFW_KEY_I:
        return ImGuiKey_I;
    case GLFW_KEY_J:
        return ImGuiKey_J;
    case GLFW_KEY_K:
        return ImGuiKey_K;
    case GLFW_KEY_L:
        return ImGuiKey_L;
    case GLFW_KEY_M:
        return ImGuiKey_M;
    case GLFW_KEY_N:
        return ImGuiKey_N;
    case GLFW_KEY_O:
        return ImGuiKey_O;
    case GLFW_KEY_P:
        return ImGuiKey_P;
    case GLFW_KEY_Q:
        return ImGuiKey_Q;
    case GLFW_KEY_R:
        return ImGuiKey_R;
    case GLFW_KEY_S:
        return ImGuiKey_S;
    case GLFW_KEY_T:
        return ImGuiKey_T;
    case GLFW_KEY_U:
        return ImGuiKey_U;
    case GLFW_KEY_V:
        return ImGuiKey_V;
    case GLFW_KEY_W:
        return ImGuiKey_W;
    case GLFW_KEY_X:
        return ImGuiKey_X;
    case GLFW_KEY_Y:
        return ImGuiKey_Y;
    case GLFW_KEY_Z:
        return ImGuiKey_Z;
    case GLFW_KEY_F1:
        return ImGuiKey_F1;
    case GLFW_KEY_F2:
        return ImGuiKey_F2;
    case GLFW_KEY_F3:
        return ImGuiKey_F3;
    case GLFW_KEY_F4:
        return ImGuiKey_F4;
    case GLFW_KEY_F5:
        return ImGuiKey_F5;
    case GLFW_KEY_F6:
        return ImGuiKey_F6;
    case GLFW_KEY_F7:
        return ImGuiKey_F7;
    case GLFW_KEY_F8:
        return ImGuiKey_F8;
    case GLFW_KEY_F9:
        return ImGuiKey_F9;
    case GLFW_KEY_F10:
        return ImGuiKey_F10;
    case GLFW_KEY_F11:
        return ImGuiKey_F11;
    case GLFW_KEY_F12:
        return ImGuiKey_F12;
    default:
        return ImGuiKey_None;
    }
}

constexpr std::array kTrackedKeys = {
    GLFW_KEY_TAB,
    GLFW_KEY_LEFT,
    GLFW_KEY_RIGHT,
    GLFW_KEY_UP,
    GLFW_KEY_DOWN,
    GLFW_KEY_PAGE_UP,
    GLFW_KEY_PAGE_DOWN,
    GLFW_KEY_HOME,
    GLFW_KEY_END,
    GLFW_KEY_INSERT,
    GLFW_KEY_DELETE,
    GLFW_KEY_BACKSPACE,
    GLFW_KEY_SPACE,
    GLFW_KEY_ENTER,
    GLFW_KEY_ESCAPE,
    GLFW_KEY_APOSTROPHE,
    GLFW_KEY_COMMA,
    GLFW_KEY_MINUS,
    GLFW_KEY_PERIOD,
    GLFW_KEY_SLASH,
    GLFW_KEY_SEMICOLON,
    GLFW_KEY_EQUAL,
    GLFW_KEY_LEFT_BRACKET,
    GLFW_KEY_BACKSLASH,
    GLFW_KEY_RIGHT_BRACKET,
    GLFW_KEY_GRAVE_ACCENT,
    GLFW_KEY_CAPS_LOCK,
    GLFW_KEY_SCROLL_LOCK,
    GLFW_KEY_NUM_LOCK,
    GLFW_KEY_PRINT_SCREEN,
    GLFW_KEY_PAUSE,
    GLFW_KEY_KP_0,
    GLFW_KEY_KP_1,
    GLFW_KEY_KP_2,
    GLFW_KEY_KP_3,
    GLFW_KEY_KP_4,
    GLFW_KEY_KP_5,
    GLFW_KEY_KP_6,
    GLFW_KEY_KP_7,
    GLFW_KEY_KP_8,
    GLFW_KEY_KP_9,
    GLFW_KEY_KP_DECIMAL,
    GLFW_KEY_KP_DIVIDE,
    GLFW_KEY_KP_MULTIPLY,
    GLFW_KEY_KP_SUBTRACT,
    GLFW_KEY_KP_ADD,
    GLFW_KEY_KP_ENTER,
    GLFW_KEY_KP_EQUAL,
    GLFW_KEY_LEFT_SHIFT,
    GLFW_KEY_LEFT_CONTROL,
    GLFW_KEY_LEFT_ALT,
    GLFW_KEY_LEFT_SUPER,
    GLFW_KEY_RIGHT_SHIFT,
    GLFW_KEY_RIGHT_CONTROL,
    GLFW_KEY_RIGHT_ALT,
    GLFW_KEY_RIGHT_SUPER,
    GLFW_KEY_MENU,
    GLFW_KEY_0,
    GLFW_KEY_1,
    GLFW_KEY_2,
    GLFW_KEY_3,
    GLFW_KEY_4,
    GLFW_KEY_5,
    GLFW_KEY_6,
    GLFW_KEY_7,
    GLFW_KEY_8,
    GLFW_KEY_9,
    GLFW_KEY_A,
    GLFW_KEY_B,
    GLFW_KEY_C,
    GLFW_KEY_D,
    GLFW_KEY_E,
    GLFW_KEY_F,
    GLFW_KEY_G,
    GLFW_KEY_H,
    GLFW_KEY_I,
    GLFW_KEY_J,
    GLFW_KEY_K,
    GLFW_KEY_L,
    GLFW_KEY_M,
    GLFW_KEY_N,
    GLFW_KEY_O,
    GLFW_KEY_P,
    GLFW_KEY_Q,
    GLFW_KEY_R,
    GLFW_KEY_S,
    GLFW_KEY_T,
    GLFW_KEY_U,
    GLFW_KEY_V,
    GLFW_KEY_W,
    GLFW_KEY_X,
    GLFW_KEY_Y,
    GLFW_KEY_Z,
    GLFW_KEY_F1,
    GLFW_KEY_F2,
};

struct Context
{
    struct MouseButtonEvent
    {
        ImGuiMouseButton button;
        bool pressed;
    };

    bool available = false;
    bgfx::ViewId viewId = 0;
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bx::AllocatorI *allocator = nullptr;
    std::vector<MouseButtonEvent> mouseButtonEvents;

    bool create(bgfx::ViewId view)
    {
        IMGUI_CHECKVERSION();

        if (allocator == nullptr)
        {
            static bx::DefaultAllocator defaultAllocator;
            allocator = &defaultAllocator;
        }
        ImGui::SetAllocatorFunctions(imguiAlloc, imguiFree, allocator);
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset
                   | ImGuiBackendFlags_RendererHasTextures;
        io.BackendRendererName = "bgfx-minimal";
        io.BackendPlatformName = "glfw-manual";

        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;
        fontConfig.MergeMode = false;
        const ImWchar *glyphRanges = io.Fonts->GetGlyphRangesDefault();
        io.FontDefault = io.Fonts->AddFontFromMemoryTTF(
            const_cast<uint8_t *>(s_robotoRegularTtf),
            sizeof(s_robotoRegularTtf),
            18.0f,
            &fontConfig,
            glyphRanges);

        ImGui::StyleColorsDark();

        viewId = view;
        layout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();

        const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
        program = bgfx::createProgram(
            bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_ocornut_imgui"),
            bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_ocornut_imgui"),
            true);
        sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

        if (!bgfx::isValid(program) || !bgfx::isValid(sampler))
        {
            destroy();
            return false;
        }
        available = true;
        return true;
    }

    void destroy()
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            for (ImTextureData *textureData : ImGui::GetPlatformIO().Textures)
            {
                const uintptr_t handleValue = reinterpret_cast<uintptr_t>(textureData->BackendUserData);
                if (handleValue == 0u)
                {
                    continue;
                }

                ImGui::TextureBgfx texture = bx::bitCast<ImGui::TextureBgfx>(textureData->GetTexID());
                bgfx::TextureHandle textureHandle = texture.handle;
                if (bgfx::isValid(textureHandle))
                {
                    bgfx::destroy(textureHandle);
                }
                textureData->BackendUserData = nullptr;
                textureData->SetTexID(ImTextureID_Invalid);
                textureData->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        if (bgfx::isValid(sampler))
        {
            bgfx::destroy(sampler);
            sampler = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(program))
        {
            bgfx::destroy(program);
            program = BGFX_INVALID_HANDLE;
        }
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::DestroyContext();
        }
        mouseButtonEvents.clear();
        available = false;
    }

    void addMouseButtonEvent(int button, bool pressed)
    {
        if (!available)
        {
            return;
        }

        ImGuiMouseButton imguiButton;
        switch (button)
        {
        case GLFW_MOUSE_BUTTON_LEFT:
            imguiButton = ImGuiMouseButton_Left;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            imguiButton = ImGuiMouseButton_Right;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            imguiButton = ImGuiMouseButton_Middle;
            break;
        default:
            return;
        }

        mouseButtonEvents.push_back({imguiButton, pressed});
    }

    void beginFrame(GLFWwindow *window, uint16_t width, uint16_t height,
                    double mouseX, double mouseY, float deltaTime,
                    float scrollX, float scrollY)
    {
        if (!available)
        {
            return;
        }

        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);
        io.AddMousePosEvent(static_cast<float>(mouseX), static_cast<float>(mouseY));
        for (const MouseButtonEvent &event : mouseButtonEvents)
        {
            io.AddMouseButtonEvent(event.button, event.pressed);
        }
        mouseButtonEvents.clear();
        if (scrollX != 0.0f || scrollY != 0.0f)
        {
            io.AddMouseWheelEvent(scrollX, scrollY);
        }

        if (io.WantTextInput)
        {
            const bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                               || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                              || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            const bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                             || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            const bool super = glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                               || glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
            io.AddKeyEvent(ImGuiMod_Shift, shift);
            io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
            io.AddKeyEvent(ImGuiMod_Alt, alt);
            io.AddKeyEvent(ImGuiMod_Super, super);

            for (int key : kTrackedKeys)
            {
                const ImGuiKey imguiKey = mapGlfwKey(key);
                if (imguiKey == ImGuiKey_None)
                {
                    continue;
                }

                io.AddKeyEvent(imguiKey, glfwGetKey(window, key) == GLFW_PRESS);
            }
        }

        ImGui::NewFrame();
    }

    void endFrame()
    {
        if (!available)
        {
            return;
        }

        ImGui::Render();
        render(ImGui::GetDrawData());
    }

    void render(ImDrawData *drawData)
    {
        if (drawData == nullptr)
        {
            return;
        }

        if (drawData->Textures != nullptr)
        {
            for (ImTextureData *textureData : *drawData->Textures)
            {
                switch (textureData->Status)
                {
                case ImTextureStatus_WantCreate:
                    {
                        bgfx::TextureHandle textureHandle =
                            bgfx::createTexture2D(static_cast<uint16_t>(textureData->Width),
                                                  static_cast<uint16_t>(textureData->Height),
                                                  false, 1, bgfx::TextureFormat::BGRA8,
                                                  0);
                        bgfx::setName(textureHandle, "ImGui Font Atlas");
                        bgfx::updateTexture2D(textureHandle, 0, 0, 0, 0,
                                              static_cast<uint16_t>(textureData->Width),
                                              static_cast<uint16_t>(textureData->Height),
                                              bgfx::copy(textureData->GetPixels(),
                                                         textureData->GetSizeInBytes()));

                        ImGui::TextureBgfx texture = {
                            .handle = textureHandle,
                            .flags = IMGUI_FLAGS_ALPHA_BLEND,
                            .mip = 0,
                            .unused = 0,
                        };
                        textureData->BackendUserData = reinterpret_cast<void *>(1u);
                        textureData->SetTexID(bx::bitCast<ImTextureID>(texture));
                        textureData->SetStatus(ImTextureStatus_OK);
                    }
                    break;

                case ImTextureStatus_WantDestroy:
                    {
                        if (textureData->BackendUserData != nullptr)
                        {
                            ImGui::TextureBgfx texture =
                                bx::bitCast<ImGui::TextureBgfx>(textureData->GetTexID());
                            bgfx::TextureHandle textureHandle = texture.handle;
                            if (bgfx::isValid(textureHandle))
                            {
                                bgfx::destroy(textureHandle);
                            }
                        }
                        textureData->BackendUserData = nullptr;
                        textureData->SetTexID(ImTextureID_Invalid);
                        textureData->SetStatus(ImTextureStatus_Destroyed);
                    }
                    break;

                case ImTextureStatus_WantUpdates:
                    {
                        if (textureData->BackendUserData != nullptr)
                        {
                            ImGui::TextureBgfx texture =
                                bx::bitCast<ImGui::TextureBgfx>(textureData->GetTexID());
                            bgfx::TextureHandle textureHandle = texture.handle;
                            for (ImTextureRect &rect : textureData->Updates)
                            {
                                const bgfx::Memory *pixels = bgfx::alloc(
                                    rect.h * rect.w * textureData->BytesPerPixel);
                                bx::gather(pixels->data,
                                           textureData->GetPixelsAt(rect.x, rect.y),
                                           textureData->GetPitch(),
                                           rect.w * textureData->BytesPerPixel,
                                           rect.h);
                                bgfx::updateTexture2D(textureHandle, 0, 0,
                                                      static_cast<uint16_t>(rect.x),
                                                      static_cast<uint16_t>(rect.y),
                                                      static_cast<uint16_t>(rect.w),
                                                      static_cast<uint16_t>(rect.h),
                                                      pixels);
                            }
                        }
                        textureData->SetStatus(ImTextureStatus_OK);
                    }
                    break;

                default:
                    break;
                }
            }
        }

        const int32_t framebufferWidth =
            static_cast<int32_t>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
        const int32_t framebufferHeight =
            static_cast<int32_t>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            return;
        }

        float ortho[16];
        bx::mtxOrtho(ortho,
                     drawData->DisplayPos.x,
                     drawData->DisplayPos.x + drawData->DisplaySize.x,
                     drawData->DisplayPos.y + drawData->DisplaySize.y,
                     drawData->DisplayPos.y,
                     0.0f, 1000.0f, 0.0f,
                     bgfx::getCaps()->homogeneousDepth);

        bgfx::setViewName(viewId, "ImGui");
        bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(viewId, nullptr, ortho);
        bgfx::setViewRect(viewId, 0, 0,
                          static_cast<uint16_t>(drawData->DisplaySize.x),
                          static_cast<uint16_t>(drawData->DisplaySize.y));

        const ImVec2 clipPosition = drawData->DisplayPos;
        const ImVec2 clipScale = drawData->FramebufferScale;

        for (int drawListIndex = 0; drawListIndex < drawData->CmdListsCount; ++drawListIndex)
        {
            const ImDrawList *drawList = drawData->CmdLists[drawListIndex];
            const uint32_t numVertices = static_cast<uint32_t>(drawList->VtxBuffer.size());
            const uint32_t numIndices = static_cast<uint32_t>(drawList->IdxBuffer.size());
            if (!checkAvailTransientBuffers(numVertices, layout, numIndices))
            {
                break;
            }

            bgfx::TransientVertexBuffer vertexBuffer;
            bgfx::TransientIndexBuffer indexBuffer;
            bgfx::allocTransientVertexBuffer(&vertexBuffer, numVertices, layout);
            bgfx::allocTransientIndexBuffer(&indexBuffer, numIndices,
                                            sizeof(ImDrawIdx) == sizeof(uint32_t));

            std::memcpy(vertexBuffer.data, drawList->VtxBuffer.Data,
                        numVertices * sizeof(ImDrawVert));
            std::memcpy(indexBuffer.data, drawList->IdxBuffer.Data,
                        numIndices * sizeof(ImDrawIdx));

            bgfx::Encoder *encoder = bgfx::begin();
            for (const ImDrawCmd *command = drawList->CmdBuffer.Data;
                 command != drawList->CmdBuffer.Data + drawList->CmdBuffer.Size;
                 ++command)
            {
                if (command->UserCallback != nullptr)
                {
                    command->UserCallback(drawList, command);
                    continue;
                }

                if (command->ElemCount == 0)
                {
                    continue;
                }

                const ImVec4 clipRect = {
                    (command->ClipRect.x - clipPosition.x) * clipScale.x,
                    (command->ClipRect.y - clipPosition.y) * clipScale.y,
                    (command->ClipRect.z - clipPosition.x) * clipScale.x,
                    (command->ClipRect.w - clipPosition.y) * clipScale.y,
                };
                if (clipRect.x >= framebufferWidth || clipRect.y >= framebufferHeight
                    || clipRect.z <= 0.0f || clipRect.w <= 0.0f)
                {
                    continue;
                }

                const uint16_t scissorX = static_cast<uint16_t>(bx::max(clipRect.x, 0.0f));
                const uint16_t scissorY = static_cast<uint16_t>(bx::max(clipRect.y, 0.0f));
                const uint16_t scissorWidth = static_cast<uint16_t>(
                    bx::min(clipRect.z, 65535.0f) - scissorX);
                const uint16_t scissorHeight = static_cast<uint16_t>(
                    bx::min(clipRect.w, 65535.0f) - scissorY);
                encoder->setScissor(scissorX, scissorY, scissorWidth, scissorHeight);
                uint64_t state = BGFX_STATE_WRITE_RGB
                                 | BGFX_STATE_WRITE_A
                                 | BGFX_STATE_MSAA;
                bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
                const ImTextureID textureId = command->GetTexID();
                if (textureId != ImTextureID_Invalid)
                {
                    ImGui::TextureBgfx texture = bx::bitCast<ImGui::TextureBgfx>(textureId);
                    textureHandle = texture.handle;
                    if ((texture.flags & IMGUI_FLAGS_ALPHA_BLEND) != 0)
                    {
                        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                       BGFX_STATE_BLEND_INV_SRC_ALPHA);
                    }
                }
                else
                {
                    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                   BGFX_STATE_BLEND_INV_SRC_ALPHA);
                }
                encoder->setState(state);
                encoder->setTexture(0, sampler, textureHandle);
                encoder->setVertexBuffer(0, &vertexBuffer, command->VtxOffset, numVertices);
                encoder->setIndexBuffer(&indexBuffer, command->IdxOffset, command->ElemCount);
                encoder->submit(viewId, program);
            }
            bgfx::end(encoder);
        }
    }
};

Context s_context;

} // namespace

namespace ImGuiBgfx
{

bool create(bgfx::ViewId viewId)
{
    return s_context.create(viewId);
}

void destroy()
{
    s_context.destroy();
}

void beginFrame(GLFWwindow *window, uint16_t width, uint16_t height,
                double mouseX, double mouseY, float deltaTime,
                float scrollX, float scrollY)
{
    s_context.beginFrame(window, width, height, mouseX, mouseY, deltaTime, scrollX, scrollY);
}

void endFrame()
{
    s_context.endFrame();
}

void addInputCharacter(unsigned int codePoint)
{
    if (!s_context.available || !ImGui::GetIO().WantTextInput)
    {
        return;
    }

    ImGui::GetIO().AddInputCharacter(codePoint);
}

void addMouseButtonEvent(int button, bool pressed)
{
    s_context.addMouseButtonEvent(button, pressed);
}

bool isAvailable()
{
    return s_context.available;
}

bool wantsMouseCapture()
{
    return s_context.available && ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboardCapture()
{
    return s_context.available && ImGui::GetIO().WantTextInput;
}

} // namespace ImGuiBgfx