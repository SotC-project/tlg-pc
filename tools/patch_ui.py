# Hooks core/tlg/ui into the renderer: menu-texture tags per draw, hidden draws, frame end.
import sys

root = "D:/tlg-pc/runtime"


def patch(path, pairs):
    p = f"{root}/{path}"
    s = open(p, encoding="utf-8").read()
    for old, new in pairs:
        if new in s:
            continue
        if old not in s:
            sys.exit(f"{path}: not found: {old[:80]!r}")
        s = s.replace(old, new, 1)
    open(p, "w", encoding="utf-8", newline="\n").write(s)


patch("src/video_core/texture_cache/image.h", [
    ("    u64 hash{};\n",
     "    u64 hash{};\n    /// The Last Guardian: menu texture tag (core/tlg/ui.h), -1 none, -2 not looked up yet.\n    s8 tlg_tag = -2;\n"),
])
patch("src/video_core/texture_cache/texture_cache.cpp", [
    ("    runtime.UploadImage(&image, buffer, image_copies);\n",
     "    runtime.UploadImage(&image, buffer, image_copies);\n    image.tlg_tag = -2; // new contents: look the menu tag up again\n"),
])
patch("src/video_core/renderer_vulkan/vk_rasterizer.cpp", [
    ("bool Rasterizer::BindResources(const Pipeline* pipeline) {\n",
     "// The Last Guardian: menu textures (core/tlg/ui.h) sampled by the draw being bound.\nstatic u32 g_tlg_draw_tags = 0;\n\nbool Rasterizer::BindResources(const Pipeline* pipeline) {\n    g_tlg_draw_tags = 0;\n"),
    ("            auto& image = texture_cache.GetImage(image_id);\n            auto& image_view = texture_cache.FindTexture(image_id, desc);\n",
     """            auto& image = texture_cache.GetImage(image_id);
            auto& image_view = texture_cache.FindTexture(image_id, desc);
            if (Tlg::Ui::Enabled()) {
                if (image.tlg_tag == -2) {
                    const auto& ii = image.info;
                    image.tlg_tag = -1;
                    if (ii.guest_size <= 512_KB && ii.size.depth == 1 &&
                        memory->IsValidMapping(ii.guest_address, ii.guest_size)) {
                        image.tlg_tag = static_cast<s8>(Tlg::Ui::Lookup(
                            memory->HashSparseMemory(ii.guest_address, ii.guest_size)));
                    }
                }
                if (image.tlg_tag >= 0) {
                    g_tlg_draw_tags |= 1u << image.tlg_tag;
                }
            }
"""),
    ("""    if (!bound) {
        ++g_stats.bind_failed;
        return;
    }
    ++g_stats.drawn;""",
     """    if (!bound) {
        ++g_stats.bind_failed;
        return;
    }
    if (Tlg::Ui::OnDraw(g_tlg_draw_tags)) {
        ++g_stats.filtered;
        return;
    }
    ++g_stats.drawn;"""),
    ('#include "video_core/renderer_vulkan/vk_rasterizer.h"\n',
     '#include "video_core/renderer_vulkan/vk_rasterizer.h"\n#include "core/tlg/ui.h"\n'),
])
patch("src/core/devtools/layer.cpp", [
    ("    I3::Menu::Draw();\n", "    I3::Menu::Draw();\n    Tlg::Options::Draw();\n"),
])
patch("CMakeLists.txt", [
    ("              src/core/i3/menu.cpp\n",
     "              src/core/i3/menu.cpp\n              src/core/tlg/options.cpp\n              src/core/tlg/options.h\n              src/core/tlg/ui.cpp\n              src/core/tlg/ui.h\n              src/core/tlg/ui_textures.h\n"),
])
patch("src/core/devtools/layer.cpp", [
    ("""#include "core/i3/menu.h"
""", """#include "core/i3/menu.h"
#include "core/tlg/options.h"
"""),
])
patch("src/input/input_handler.cpp", [
    ("""            if (button == SDL_GAMEPAD_BUTTON_START && I3::Menu::OnOptionsButton(new_button_state)) {
                break;
            }""", """            if (button == SDL_GAMEPAD_BUTTON_START && I3::Menu::OnOptionsButton(new_button_state)) {
                break;
            }
            if (Tlg::Options::OnButton(SDLGamepadToOrbisButton(button), new_button_state)) {
                break;
            }"""),
    ("""        controller->Axis(c_axis, GetAxis(-0x80, 0x7f, *new_param * multiplier));
    }""", """        if (Tlg::Options::OnAxis(c_axis, *new_param)) {
            controller->Axis(c_axis, GetAxis(-0x80, 0x7f, 0));
            return;
        }
        controller->Axis(c_axis, GetAxis(-0x80, 0x7f, *new_param * multiplier));
    }"""),
    ("""#include "core/i3/menu.h"
""", """#include "core/i3/menu.h"
#include "core/tlg/options.h"
"""),
])
patch("src/sdl_window.cpp", [
    ("""    if (I3::Menu::OnEvent(event)) {
        return;
    }""", """    if (I3::Menu::OnEvent(event)) {
        return;
    }
    // The Last Guardian: a key being captured for a binding on the PC Controls page.
    if (Tlg::Options::OnEvent(event)) {
        return;
    }"""),
    ("""#include "core/i3/menu.h"
""", """#include "core/i3/menu.h"
#include "core/tlg/options.h"
"""),
])
patch("src/imgui/renderer/imgui_core.cpp", [
    ("""    // Big Picture""", """    // Face of the PC rows of The Last Guardian's Options screen, close to the game's own.
    for (const char* path : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"}) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            ImFontConfig tlg_cfg{};
            tlg_cfg.OversampleH = 2;
            Tlg::Options::SetFont(io.Fonts->AddFontFromFileTTF(path, 64.0f, &tlg_cfg));
            break;
        }
    }

    // Big Picture"""),
    ("""#include "core/i3/menu.h"
""", """#include "core/i3/menu.h"
#include "core/tlg/options.h"
"""),
])
print("patched")
