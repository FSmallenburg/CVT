#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <dear-imgui/imgui.h>

namespace ImGui
{

void PushFont(Font::Enum, float)
{
    PushFont(GetFont(), 0.0f);
}

} // namespace ImGui