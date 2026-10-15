// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <imgui.h>

#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "core/i3/lang.h"
#include "core/i3/menu.h"
#include "core/tlg/options.h"
#include "core/tlg/ui.h"

namespace Overlay {
void ToggleSimpleFps();
}

namespace Tlg::Options {

namespace {

using I3::Lang::Tr;
using Libraries::Pad::OrbisPadButtonDataOffset;
using Ui::Bit;

// ---- What the game shows ----

// The game's top Options list: Camera, Screen, Audio, Copyright, Back. Its own pages each have
// a title texture of their own.
constexpr u32 ListTags = Bit(Ui::ItemCamera) | Bit(Ui::ItemCopyright);
constexpr u32 PageTags = Bit(Ui::CameraTitle) | Bit(Ui::ScreenTitle) | Bit(Ui::AudioTitle) |
                         Bit(Ui::CopyrightTitle) | Bit(Ui::GuideTitle);
constexpr u32 ListDrawTags = Bit(Ui::OptionTitle) | Bit(Ui::ItemCamera) | Bit(Ui::ItemScreen) |
                             Bit(Ui::ItemAudio) | Bit(Ui::ItemCopyright) | Bit(Ui::ItemBack) |
                             Bit(Ui::ItemGuide) | Bit(Ui::ItemTreasure) | Bit(Ui::Cursor);

// ---- Rows ----

enum class Page { None, Graphics, Controls };

struct Action {
    const char* pad;
    const char* label;
};
constexpr std::array<Action, 20> Actions{{
    {"axis_left_y_minus", "Move Forward"},
    {"axis_left_y_plus", "Move Back"},
    {"axis_left_x_minus", "Move Left"},
    {"axis_left_x_plus", "Move Right"},
    {"cross", "Jump"},
    {"circle", "Grab / Interact"},
    {"square", "Square Button"},
    {"triangle", "Triangle Button"},
    {"r1", "Call Trico"},
    {"l1", "L1 Button"},
    {"r2", "R2 Button"},
    {"l2", "L2 Button"},
    {"l3", "L3 Button"},
    {"r3", "Reset Camera"},
    {"options", "Pause"},
    {"touchpad_center", "Touch Pad"},
    {"pad_up", "D-pad Up"},
    {"pad_down", "D-pad Down"},
    {"pad_left", "D-pad Left"},
    {"pad_right", "D-pad Right"},
}};

constexpr std::array<std::pair<u32, u32>, 4> WindowSizes{
    {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}};
constexpr std::array<const char*, 4> SharpenNames{"Off", "Low", "Medium", "High"};
constexpr std::array<int, 4> SharpenValues{0, 600, 250, 100};
// The game's own languages and their console language ids.
constexpr std::array<const char*, 4> LanguageNames{"English", "Français", "Español", "Português"};
constexpr std::array<int, 4> LanguageIds{1, 2, 3, 7};
constexpr int GraphicsRows = 7;

// ---- State ----

std::mutex mutex;
bool list_shown = false;
// The selected line of the joined list: 0-3 the game's Camera..Copyright, 4 Graphics,
// 5 Controls, 6 Back. The game's own cursor stays on Copyright while 4 or 5 is selected.
int row = 0;
Page page = Page::None;
int page_row = 0;
bool capturing = false;
bool fps_shown = false;
// Stick direction last seen, to step once per push.
int stick_y = 0, stick_x = 0;
enum Nav : int { Up = 1, Down = 2, Left = 4, Right = 8, Confirm = 16, Back = 32 };
ImFont* font = nullptr;

template <typename Range, typename Value>
int IndexOf(const Range& range, const Value& value, int fallback) {
    const auto it = std::ranges::find(range, value);
    return it == range.end() ? fallback : static_cast<int>(it - range.begin());
}

int SharpenIndex() {
    if (!EmulatorSettings.IsRcasEnabled()) {
        return 0;
    }
    return IndexOf(SharpenValues, static_cast<int>(EmulatorSettings.GetRcasAttenuation()), 2);
}

int LanguageIndex() {
    const int stored = EmulatorSettings.GetConsoleLanguage();
    const int id =
        stored == 18 ? 1 : stored == 22 ? 2 : stored == 20 ? 3 : stored == 17 ? 7 : stored;
    return IndexOf(LanguageIds, id, 0);
}

SDL_Window* Window() {
    const auto id = static_cast<SDL_WindowID>(
        reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle));
    return SDL_GetWindowFromID(id);
}

struct Line {
    std::string label;
    std::string value;
    std::string other; // the alternative of a two-way setting, drawn dim like the game's
};

std::vector<Line> GraphicsLines() {
    const auto on_off = [](bool on) {
        return std::pair<std::string, std::string>{on ? Tr("On") : Tr("Off"),
                                                   on ? Tr("Off") : Tr("On")};
    };
    std::vector<Line> lines;
    auto [vs, vs_other] = on_off(EmulatorSettings.GetPresentMode() == "Fifo");
    lines.push_back({Tr("VSync"), vs, vs_other});
    auto [fs, fs_other] = on_off(EmulatorSettings.IsFullScreen());
    lines.push_back({Tr("Fullscreen"), fs, fs_other});
    lines.push_back({Tr("Window Size"),
                     fmt::format("{} x {}", EmulatorSettings.GetWindowWidth(),
                                 EmulatorSettings.GetWindowHeight()),
                     ""});
    auto [fsr, fsr_other] = on_off(EmulatorSettings.IsFsrEnabled());
    lines.push_back({Tr("Upscaling (FSR)"), fsr, fsr_other});
    lines.push_back({Tr("Sharpening"), Tr(SharpenNames[SharpenIndex()]), ""});
    auto [fps, fps_other] = on_off(fps_shown);
    lines.push_back({Tr("Frame Rate Counter"), fps, fps_other});
    lines.push_back({Tr("Language"), LanguageNames[LanguageIndex()], ""});
    return lines;
}

void AdjustGraphics(int line, int step) {
    switch (line) {
    case 0:
        EmulatorSettings.SetPresentMode(EmulatorSettings.GetPresentMode() == "Fifo" ? "Immediate"
                                                                                     : "Fifo");
        break;
    case 1: {
        const bool on = !EmulatorSettings.IsFullScreen();
        EmulatorSettings.SetFullScreen(on);
        EmulatorSettings.SetFullScreenMode(on ? "Fullscreen (Borderless)" : "Windowed");
        if (SDL_Window* window = Window()) {
            SDL_SetWindowFullscreen(window, on);
        }
        break;
    }
    case 2: {
        const std::pair<u32, u32> current{EmulatorSettings.GetWindowWidth(),
                                          EmulatorSettings.GetWindowHeight()};
        const int count = static_cast<int>(WindowSizes.size());
        const auto [width, height] =
            WindowSizes[(IndexOf(WindowSizes, current, 1) + step + count) % count];
        EmulatorSettings.SetWindowWidth(width);
        EmulatorSettings.SetWindowHeight(height);
        if (SDL_Window* window = Window(); window && !EmulatorSettings.IsFullScreen()) {
            SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height));
        }
        break;
    }
    case 3:
        EmulatorSettings.SetFsrEnabled(!EmulatorSettings.IsFsrEnabled());
        break;
    case 4: {
        const int count = static_cast<int>(SharpenValues.size());
        const int index = (SharpenIndex() + step + count) % count;
        EmulatorSettings.SetRcasEnabled(index != 0);
        if (index != 0) {
            EmulatorSettings.SetRcasAttenuation(SharpenValues[index]);
        }
        break;
    }
    case 5:
        fps_shown = !fps_shown;
        Overlay::ToggleSimpleFps();
        return;
    case 6: {
        const int count = static_cast<int>(LanguageIds.size());
        EmulatorSettings.SetConsoleLanguage(
            LanguageIds[(LanguageIndex() + step + count) % count]);
        break;
    }
    default:
        return;
    }
    EmulatorSettings.Save();
}

int PageLines() {
    return page == Page::Graphics ? GraphicsRows : static_cast<int>(Actions.size());
}

/// The PC rows hold the input: one of their lines is selected or their page is open.
bool Holding() {
    return list_shown && (page != Page::None || row == 4 || row == 5);
}

/// Handles one navigation step. Returns true when the game must not see it.
bool Navigate(int step) {
    if (page != Page::None) {
        const int count = PageLines() + 1; // + Back
        if (step & Up) {
            page_row = (page_row + count - 1) % count;
        } else if (step & Down) {
            page_row = (page_row + 1) % count;
        } else if (step & Back || ((step & Confirm) && page_row == count - 1)) {
            page = Page::None;
        } else if (page == Page::Graphics && (step & (Left | Right | Confirm))) {
            AdjustGraphics(page_row, (step & Left) ? -1 : 1);
        } else if (page == Page::Controls && (step & Confirm)) {
            capturing = true;
        }
        return true;
    }
    switch (row) {
    case 3:
        if (step & Down) {
            row = 4;
            return true;
        }
        if (step & Up) {
            row = 2;
        }
        return false;
    case 4:
    case 5:
        if (step & Up) {
            row--;
        } else if (step & Down) {
            if (row == 5) {
                row = 6;
                return false; // the game moves from Copyright to Back
            }
            row = 5;
        } else if (step & Confirm) {
            page = row == 4 ? Page::Graphics : Page::Controls;
            page_row = 0;
        } else if (step & Back) {
            return false; // the game leaves its Options screen
        }
        return true;
    case 6:
        if (step & Up) {
            row = 5;
            return false; // the game moves from Back to Copyright
        }
        if (step & Down) {
            row = 0; // the game's list wraps from Back to Camera
        }
        return false;
    default:
        if (step & Down) {
            row++;
        } else if (step & Up) {
            row = row == 0 ? 6 : row - 1; // wraps from Camera to Back, like the title menu
        }
        return false;
    }
}

// ---- Drawing, in the look of the game's own screens (1920 x 1080 layout) ----

constexpr ImU32 White = IM_COL32(240, 240, 240, 255);
constexpr ImU32 Dim = IM_COL32(128, 128, 128, 255);
constexpr ImU32 Shadow = IM_COL32(0, 0, 0, 150);

struct Canvas {
    ImDrawList* draw;
    float scale;
    float ox;
    float oy;
    ImVec2 At(float x, float y) const {
        return {ox + x * scale, oy + y * scale};
    }
};

ImFont* Font() {
    return font ? font : ImGui::GetFont();
}

/// Text with its vertical centre at y, like the game's labels.
void Text(const Canvas& c, float x, float y, float size, ImU32 color, const std::string& text,
          bool centred = false) {
    const float px = size * c.scale;
    const ImVec2 extent = Font()->CalcTextSizeA(px, FLT_MAX, 0.0f, text.c_str());
    ImVec2 pos = c.At(x, y);
    pos.y -= extent.y * 0.5f;
    if (centred) {
        pos.x -= extent.x * 0.5f;
    }
    c.draw->AddText(Font(), px, {pos.x + 1.5f * c.scale, pos.y + 2.0f * c.scale}, Shadow,
                    text.c_str());
    c.draw->AddText(Font(), px, pos, color, text.c_str());
}

void Arrow(const Canvas& c, float x, float y) {
    c.draw->AddTriangleFilled(c.At(x, y - 8.0f), c.At(x, y + 8.0f), c.At(x + 15.0f, y), White);
}

void DrawList(const Canvas& c) {
    // The game's rows sit at 468, 516, 564, 613; Back is drawn here, below the PC rows.
    constexpr float Pitch = 48.0f;
    const std::array<std::string, 3> labels{Tr("Graphics"), Tr("Controls"), Tr("Back")};
    const std::array<float, 3> ys{613.0f + Pitch, 613.0f + 2 * Pitch, 613.0f + 2 * Pitch + 94.0f};
    for (int i = 0; i < 3; i++) {
        Text(c, 148.0f, ys[i], 31.0f, White, labels[i]);
    }
    const float arrow_y = row <= 3 ? 468.0f + row * 48.3f : ys[row - 4];
    Arrow(c, 117.0f, arrow_y);
}

void DrawPage(const Canvas& c) {
    // The panel of the game's Camera page.
    c.draw->AddRectFilled(c.At(50.0f, 65.0f), c.At(1090.0f, 715.0f), IM_COL32(0, 0, 0, 110),
                          14.0f * c.scale);
    c.draw->AddRect(c.At(50.0f, 65.0f), c.At(1090.0f, 715.0f), IM_COL32(150, 150, 150, 200),
                    14.0f * c.scale, 0, 2.0f * c.scale);
    Text(c, 113.0f, 125.0f, 38.0f, White, page == Page::Graphics ? Tr("Graphics") : Tr("Controls"));
    const int count = PageLines();
    if (page == Page::Graphics) {
        const auto lines = GraphicsLines();
        for (int i = 0; i < count; i++) {
            const float y = 208.0f + i * 64.0f;
            Text(c, 153.0f, y, 29.0f, White, lines[i].label);
            Text(c, 620.0f, y, 29.0f, White, "[ " + lines[i].value + " ]", true);
            if (!lines[i].other.empty()) {
                Text(c, 889.0f, y, 29.0f, Dim, lines[i].other, true);
            }
        }
    } else {
        // Two columns of actions with their keys.
        const auto file_lines = I3::Menu::ReadLines();
        for (int i = 0; i < count; i++) {
            const float x = i < 10 ? 153.0f : 620.0f;
            const float y = 196.0f + (i % 10) * 43.0f;
            const bool current = page_row == i;
            std::string keys = current && capturing ? Tr("Press a key...")
                                                    : I3::Menu::KeysOf(file_lines, Actions[i].pad);
            Text(c, x, y, 25.0f, White, Tr(Actions[i].label));
            Text(c, x + 300.0f, y, 25.0f, current && capturing ? White : Dim, keys);
        }
    }
    Text(c, 153.0f, 657.0f, 29.0f, White, Tr("Back"));
    if (page_row == count) {
        Arrow(c, 122.0f, 657.0f);
    } else if (page == Page::Graphics) {
        Arrow(c, 122.0f, 208.0f + page_row * 64.0f);
    } else {
        Arrow(c, (page_row < 10 ? 153.0f : 620.0f) - 31.0f, 196.0f + (page_row % 10) * 43.0f);
    }
}

} // namespace

void SetFont(ImFont* f) {
    font = f;
}

void Draw() {
    if (!Ui::Enabled()) {
        return;
    }
    Ui::EndFrame();
    std::scoped_lock lock{mutex};
    const u32 seen = Ui::Seen();
    const bool shown = (seen & ListTags) == ListTags && (seen & PageTags) == 0;
    if (shown != list_shown) {
        // Entering the list from a game page keeps the game's cursor where it was; from the
        // title or pause menu the game starts at Camera.
        static u32 seen_before = 0;
        if (shown && (seen_before & PageTags) == 0) {
            row = 0;
        }
        if (!shown) {
            seen_before = seen;
            page = Page::None;
            capturing = false;
        }
        list_shown = shown;
    }
    if (!list_shown) {
        Ui::SetHidden(0);
        return;
    }
    // The game's cursor and Back line are drawn here instead; with a PC page open the whole
    // list makes way for it.
    Ui::SetHidden(page != Page::None ? ListDrawTags : Bit(Ui::Cursor) | Bit(Ui::ItemBack));

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = std::min(display.x / 1920.0f, display.y / 1080.0f);
    const Canvas canvas{ImGui::GetForegroundDrawList(), scale, (display.x - 1920.0f * scale) * 0.5f,
                        (display.y - 1080.0f * scale) * 0.5f};
    if (page == Page::None) {
        DrawList(canvas);
    } else {
        DrawPage(canvas);
    }
}

bool OnButton(OrbisPadButtonDataOffset button, bool pressed) {
    if (!Ui::Enabled()) {
        return false;
    }
    std::scoped_lock lock{mutex};
    if (!list_shown) {
        return false;
    }
    int step = 0;
    switch (button) {
    case OrbisPadButtonDataOffset::Up:
        step = Up;
        break;
    case OrbisPadButtonDataOffset::Down:
        step = Down;
        break;
    case OrbisPadButtonDataOffset::Left:
        step = Left;
        break;
    case OrbisPadButtonDataOffset::Right:
        step = Right;
        break;
    case OrbisPadButtonDataOffset::Cross:
        step = Confirm;
        break;
    case OrbisPadButtonDataOffset::Circle:
        step = Back;
        break;
    default:
        return Holding();
    }
    if (!pressed) {
        return Holding();
    }
    // Decided at once: the game's own cursor must move in step with the joined list.
    return Navigate(step);
}

bool OnAxis(Input::Axis axis, int value) {
    if (!Ui::Enabled()) {
        return false;
    }
    std::scoped_lock lock{mutex};
    if (!list_shown || (axis != Input::Axis::LeftX && axis != Input::Axis::LeftY)) {
        return false;
    }
    const int direction = value < -64 ? -1 : value > 64 ? 1 : 0;
    int& last = axis == Input::Axis::LeftY ? stick_y : stick_x;
    const bool fresh = direction != 0 && direction != last;
    last = direction;
    if (!fresh) {
        return Holding();
    }
    const int step = axis == Input::Axis::LeftY ? (direction < 0 ? Up : Down)
                                                : (direction < 0 ? Left : Right);
    return Navigate(step) || Holding();
}

bool OnEvent(const SDL_Event& event) {
    if (!Ui::Enabled()) {
        return false;
    }
    std::scoped_lock lock{mutex};
    if (!capturing) {
        return false;
    }
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.type != SDL_EVENT_MOUSE_WHEEL) {
        // Releases still reach the binding layer, so nothing stays held.
        return event.type == SDL_EVENT_MOUSE_MOTION;
    }
    capturing = false;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        return true; // keeps the binding as it was
    }
    if (const std::string name = I3::Menu::NameOf(event);
        !name.empty() && page == Page::Controls && page_row < static_cast<int>(Actions.size())) {
        I3::Menu::Bind(Actions[page_row].pad, name);
    }
    return true;
}

} // namespace Tlg::Options
