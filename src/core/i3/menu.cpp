// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/singleton.h"
#include "core/debug_state.h"
#include "core/file_sys/fs.h"
#include "core/libraries/pad/pad.h"
#include "emulator.h"
#include "input/controller.h"
#include <ctime>
#include "core/i3/achievements.h"
#include <algorithm>
#include <cctype>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <imgui.h>

#include "common/recording.h"
#include "core/i3/first_run.h"
#include "core/i3/lang.h"
#include "common/elf_info.h"
#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "core/i3/menu.h"
#include "common/path_util.h"
#include "input/input_handler.h"
#include "video_core/pass_filter.h"
#include "video_core/texture_cache/texture_cache.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Input {
std::filesystem::path GetInputConfigFile(const std::string& game_id);
}

// Set by the video output when the game presents its first frame.
extern std::atomic<u64> g_first_flip_ms;

namespace Overlay {
void ToggleSimpleFps();
}

namespace I3::Menu {

namespace {

// Texts are kept in English in the tables and rows below (lines are matched by their English
// label) and put into the console language where they are drawn or handed out for drawing.
using Lang::Tr;

enum class Page { Main, Controls, Graphics, Achievements, Start, Options };

std::atomic_bool open{false};
std::atomic_bool capturing{false};
Page page = Page::Main;
int selected = 0;
ImFont* menu_font = nullptr;
bool fps_shown = false;
// Set when an option that only takes effect at the next start was changed.
bool restart_note = false;
// Quit and Return to Main Menu ask once more: set by the first press, cleared when the
// selection moves.
bool quit_armed = false;
int quit_armed_row = -1;
// The page the sub-pages go back to: the start screen at launch, the pause menu during play.
Page home = Page::Main;
// Controller input for the menu, taken from the pad events themselves (ImGui's own gamepad
// navigation is not switched on in this program): bits up, down, left, right, confirm, back.
enum PadNav : int { NavUp = 1, NavDown = 2, NavLeft = 4, NavRight = 8, NavConfirm = 16, NavBack = 32 };
std::atomic<int> pad_nav{0};
int stick_direction = 0;
// The start screen, shown once per launch when the game presents its first frame.
enum class StartState { Unknown, Showing, Done };
StartState start_state = StartState::Unknown;
bool has_saves = false;
// The game is held (its threads paused) while a menu is up.
bool game_held = false;
u64 opened_ms = 0;
bool options_consumed = false;
// Pad presses sent to the game later: opening its own Options screen for the player.
struct TimedPress {
    u64 at_ms;
    Libraries::Pad::OrbisPadButtonDataOffset button;
    bool down;
};
std::vector<TimedPress> timed_presses;
std::mutex timed_mutex;
// New Game / Load Game on the start screen: the row of the game's Options screen to open at
// once the last save has loaded, -1 for none.
int pending_game_row = -1;
u64 pending_ready_ms = 0;

// The look of the game's own Options screen: black panel, yellow title, white condensed text.
constexpr ImU32 Yellow = IM_COL32(246, 190, 0, 255);
constexpr ImU32 White = IM_COL32(236, 236, 236, 255);
constexpr ImU32 Grey = IM_COL32(150, 150, 150, 255);
constexpr ImU32 Dim = IM_COL32(96, 96, 96, 255);

struct Action {
    const char* pad;   // output name in the binding file
    const char* label; // what the game calls it
};
constexpr std::array<Action, 27> Actions{{
    {"r2", "Main Attack"},
    {"l2", "Aim / Zoom"},
    {"r1", "Heavy Attack"},
    {"l1", "Special Power"},
    {"cross", "Jump"},
    {"circle", "Dash"},
    {"square", "Melee"},
    {"triangle", "Karmic Actions"},
    {"touchpad_center", "Touchpad Press (Drain, Interact)"},
    {"l3", "L3"},
    {"r3", "R3"},
    {"options", "Pause"},
    {"pad_up", "D-pad Up"},
    {"pad_down", "D-pad Down"},
    {"pad_left", "D-pad Left"},
    {"pad_right", "D-pad Right"},
    {"axis_left_y_minus", "Move Forward"},
    {"axis_left_y_plus", "Move Back"},
    {"axis_left_x_minus", "Move Left"},
    {"axis_left_x_plus", "Move Right"},
    {"motion_sideways", "Hold Controller Sideways (Spray Can)"},
    {"motion_shake", "Shake Controller"},
    {"touchpad_swipe_up", "Touchpad Swipe Up"},
    {"touchpad_swipe_down", "Touchpad Swipe Down"},
    {"touchpad_swipe_left", "Touchpad Swipe Left"},
    {"touchpad_swipe_right", "Touchpad Swipe Right"},
    {"touchpad_two_fingers", "Touchpad Both Thumbs (Move Car)"},
}};

bool IsMouseName(const std::string& name) {
    return name.ends_with("button") || name.starts_with("mousewheel");
}

std::filesystem::path BindingFile() {
    return Input::GetInputConfigFile(std::string(Common::ElfInfo::Instance().GameSerial()));
}

/// Splits "name = value" (comments and blank lines give an empty name).
std::pair<std::string, std::string> SplitLine(const std::string& line) {
    const auto strip = [](std::string text) {
        const auto first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            return std::string{};
        }
        return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    };
    const auto equals = line.find('=');
    if (equals == std::string::npos || strip(line).starts_with('#')) {
        return {};
    }
    return {strip(line.substr(0, equals)), strip(line.substr(equals + 1))};
}

/// A value that names a keyboard key or mouse input (not a controller pass-through line).
bool IsKeyboardValue(const std::string& value) {
    return Input::string_to_keyboard_key_map.contains(value);
}

std::vector<std::string> ReadLines() {
    std::vector<std::string> lines;
    std::ifstream file{BindingFile()};
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

/// How a binding-file key name is shown to the player.
std::string DisplayName(const std::string& key) {
    static const std::vector<std::pair<const char*, const char*>> names{
        {"leftbutton", "Left Mouse"},     {"rightbutton", "Right Mouse"},
        {"middlebutton", "Middle Mouse"}, {"sidebuttonback", "Mouse 4"},
        {"sidebuttonforward", "Mouse 5"}, {"mousewheelup", "Wheel Up"},
        {"mousewheeldown", "Wheel Down"}, {"lshift", "Left Shift"},
        {"rshift", "Right Shift"},        {"lctrl", "Left Ctrl"},
        {"rctrl", "Right Ctrl"},          {"lalt", "Left Alt"},
        {"ralt", "Right Alt"},            {"escape", "Esc"},
        {"space", "Space"},               {"enter", "Enter"},
        {"tab", "Tab"},                   {"backspace", "Backspace"},
        {"up", "Up"},                     {"down", "Down"},
        {"left", "Left"},                 {"right", "Right"},
        {"capslock", "Caps Lock"},
    };
    for (const auto& [name, shown] : names) {
        if (key == name) {
            return Tr(shown);
        }
    }
    std::string upper = key;
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return std::toupper(c); });
    return upper;
}

std::string KeysOf(const std::vector<std::string>& lines, const char* pad) {
    std::string keys;
    for (const auto& line : lines) {
        const auto [name, value] = SplitLine(line);
        if (name == pad && IsKeyboardValue(value)) {
            keys += (keys.empty() ? "" : ", ") + DisplayName(value);
        }
    }
    return keys.empty() ? "-" : keys;
}

/// Redraws the in-game button icons for the new bindings (the same script Keys.bat runs),
/// then has the runtime load and apply them.
void RefreshIcons() {
#ifdef _WIN32
    std::thread([] {
        wchar_t module_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        const auto dir = std::filesystem::path(module_path).parent_path();
        std::wstring command = L"pwsh -NoProfile -ExecutionPolicy Bypass -File \"" +
                               (dir / "Keys.ps1").wstring() + L"\" -Icons";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, dir.wstring().c_str(), &startup, &process)) {
            WaitForSingleObject(process.hProcess, 15000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            VideoCore::g_pack_reload = true;
        } else {
            LOG_ERROR(Input, "PC menu: could not run Keys.ps1 to redraw the icons, error {}",
                      GetLastError());
        }
    }).detach();
#endif
}

/// Binds `pad` to the single input `key`, applies the bindings and redraws the icons.
void Bind(const char* pad, const std::string& key) {
    auto lines = ReadLines();
    size_t insert_at = lines.size();
    for (size_t i = lines.size(); i-- > 0;) {
        const auto [name, value] = SplitLine(lines[i]);
        if (name == pad && IsKeyboardValue(value)) {
            lines.erase(lines.begin() + i);
            insert_at = i;
        }
    }
    if (insert_at == lines.size()) {
        // Not bound before: ahead of the controller section.
        for (size_t i = 0; i < lines.size(); i++) {
            if (lines[i].starts_with("# --- Controller")) {
                insert_at = i;
                break;
            }
        }
    }
    lines.insert(lines.begin() + insert_at, fmt::format("{} = {}", pad, key));
    {
        std::ofstream file{BindingFile(), std::ios::trunc | std::ios::binary};
        for (const auto& line : lines) {
            file << line << "\n";
        }
    }
    Input::ParseInputConfig(std::string(Common::ElfInfo::Instance().GameSerial()));
    RefreshIcons();
}

/// Binding-file name of the input in `event`, or empty.
std::string NameOf(const SDL_Event& event) {
    u32 code = 0;
    bool mouse = false;
    if (event.type == SDL_EVENT_KEY_DOWN) {
        code = event.key.key;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        code = event.button.button;
        mouse = true;
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        return event.wheel.y > 0 ? "mousewheelup" : event.wheel.y < 0 ? "mousewheeldown" : "";
    } else {
        return {};
    }
    for (const auto& [name, value] : Input::string_to_keyboard_key_map) {
        if (value == code && IsMouseName(name) == mouse && !name.starts_with("mousewheel")) {
            return name;
        }
    }
    return {};
}

SDL_Window* Window() {
    const auto id = static_cast<SDL_WindowID>(
        reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle));
    return SDL_GetWindowFromID(id);
}

// ---- Graphics options ----

// Effects the engine has no switch for: each is one fullscreen pass of the renderer, found by
// skipping passes one at a time in a frozen scene, and is turned off by not issuing its draws.
struct Effect {
    const char* label;
    const char* key; // name in the settings file
    u64 fs_hash;     // fragment shader of the pass
    const char* description;
    bool on = true;
};
std::array<Effect, 3> effects{{
    {"Anti-Aliasing", "antialiasing", 0x961c93ba,
     "Edge smoothing pass. Off gives a sharper picture with visible stair-steps. Applies at once"},
    {"Wet Surface Reflections", "reflections", 0x9215a6e8,
     "Reflections on wet ground and puddles. Applies at once"},
    {"Light Glow", "glow", 0x6d29fb0f, "Glow around fires and bright lights. Applies at once"},
}};
// The game's own Options > Video > Frame Rate ("30fps limit") is stored in each save. On by
// default here: the limit is lifted whatever the save says.
bool unlock_frame_rate = true;
// How often the shadow maps are redrawn: [0] the sun's, [1] those of local lights.
std::array<int, 2> shadow_rate{};
constexpr std::array<const char*, 2> ShadowKeys{"sun_shadows", "light_shadows"};
constexpr std::array<const char*, 3> ShadowRateKeys{"full", "half", "off"};
constexpr std::array<const char*, 3> ShadowRateNames{"Every Frame", "Every 2nd Frame", "Off"};
// Shadow Detail: objects with fewer indices than this cast no sun shadow.
int shadow_detail = 0;
constexpr std::array<const char*, 3> ShadowDetailKeys{"full", "reduced", "low"};
constexpr std::array<const char*, 3> ShadowDetailNames{"Full", "Reduced", "Low"};
constexpr std::array<u32, 3> ShadowDetailIndices{0, 300, 1000};
// Shadow presets: sun rate, light rate, detail.
constexpr std::array<const char*, 4> ShadowPresetNames{"Original", "Balanced", "Fast", "Off"};
constexpr std::array<std::array<int, 3>, 4> ShadowPresets{{
    {0, 0, 0},
    {1, 1, 1},
    {1, 2, 2},
    {2, 2, 0},
}};

/// Index of the shadow preset matching the three options, or -1 for a custom mix.
int CurrentShadowPreset() {
    for (size_t p = 0; p < ShadowPresets.size(); p++) {
        if (ShadowPresets[p] == std::array<int, 3>{shadow_rate[0], shadow_rate[1], shadow_detail}) {
            return static_cast<int>(p);
        }
    }
    return -1;
}

constexpr std::array<const char*, 3> PresetNames{"Original", "Sharp", "Minimal"};
// Which effects each preset keeps on, in the order of `effects`.
constexpr std::array<std::array<bool, 3>, 3> Presets{{
    {true, true, true},
    {false, true, true},
    {false, false, false},
}};

std::filesystem::path EffectsFile() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "i3_graphics.txt";
}

void ApplyEffects() {
    VideoCore::PassFilter::Clear();
    for (const auto& effect : effects) {
        if (!effect.on) {
            VideoCore::PassFilter::Skip(effect.fs_hash);
        }
    }
    Common::g_unlock_flip_rate = unlock_frame_rate;
    for (size_t kind = 0; kind < shadow_rate.size(); kind++) {
        Common::g_shadow_rate[kind] = shadow_rate[kind];
    }
    Common::g_shadow_min_indices = ShadowDetailIndices[shadow_detail];
}

void SaveEffects() {
    std::ofstream file{EffectsFile(), std::ios::trunc};
    for (const auto& effect : effects) {
        file << effect.key << " = " << (effect.on ? "on" : "off") << "\n";
    }
    file << "unlock_frame_rate = " << (unlock_frame_rate ? "on" : "off") << "\n";
    for (size_t kind = 0; kind < shadow_rate.size(); kind++) {
        file << ShadowKeys[kind] << " = " << ShadowRateKeys[shadow_rate[kind]] << "\n";
    }
    file << "shadow_detail = " << ShadowDetailKeys[shadow_detail] << "\n";
}

void LoadEffects() {
    std::ifstream file{EffectsFile()};
    for (std::string line; std::getline(file, line);) {
        const auto [name, value] = SplitLine(line);
        for (auto& effect : effects) {
            if (name == effect.key) {
                effect.on = value != "off";
            }
        }
        if (name == "unlock_frame_rate") {
            unlock_frame_rate = value != "off";
        }
        if (name == "shadow_detail") {
            shadow_detail = value == ShadowDetailKeys[2] ? 2 : value == ShadowDetailKeys[1];
        }
        for (size_t kind = 0; kind < shadow_rate.size(); kind++) {
            if (name == ShadowKeys[kind]) {
                shadow_rate[kind] = value == ShadowRateKeys[2] ? 2 : value == ShadowRateKeys[1];
            }
        }
    }
    ApplyEffects();
}

/// Index of the preset matching the current switches, or -1 for a custom mix.
int CurrentPreset() {
    for (size_t p = 0; p < Presets.size(); p++) {
        bool same = true;
        for (size_t e = 0; e < effects.size(); e++) {
            same &= effects[e].on == Presets[p][e];
        }
        if (same) {
            return static_cast<int>(p);
        }
    }
    return -1;
}

constexpr std::array<u32, 6> FrameCaps{30, 60, 90, 120, 144, 1000};
constexpr std::array<std::pair<u32, u32>, 4> WindowSizes{
    {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}};
constexpr std::array<const char*, 4> SharpenNames{"Off", "Low", "Medium", "High"};
// RCAS attenuation in thousandths: lower is sharper.
constexpr std::array<int, 4> SharpenValues{0, 600, 250, 100};

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

// The languages the game ships, under their own names (these are never translated), and the
// console language id of each.
constexpr std::array<const char*, 14> LanguageNames{
    "English", "Français", "Español", "Deutsch", "Italiano", "Nederlands", "Português",
    "Русский", "Suomi",    "Svenska", "Dansk",   "Norsk",    "Polski",     "Türkçe"};
constexpr std::array<int, 14> LanguageIds{1, 2, 3, 4, 5, 6, 7, 8, 12, 13, 14, 15, 16, 19};

/// Index of the stored console language among the game's. The regional variants count as the
/// language they are shown in; a language the game lacks counts as English.
int LanguageIndex() {
    const int stored = EmulatorSettings.GetConsoleLanguage();
    const int id =
        stored == 18 ? 1 : stored == 22 ? 2 : stored == 20 ? 3 : stored == 17 ? 7 : stored;
    return IndexOf(LanguageIds, id, 0);
}

struct Row {
    std::string label;
    std::string value;
    std::string description;
    bool adjustable = false;
};

std::vector<Row> BuildRows() {
    std::vector<Row> rows;
    if (page == Page::Main || page == Page::Start) {
        const std::string trophies =
            fmt::format("{} / {}", Achievements::UnlockedCount(), Achievements::Count());
        if (page == Page::Start) {
            if (has_saves) {
                rows.push_back({"CONTINUE", "", Tr("Carry on from your last save")});
                rows.push_back({"NEW GAME", "",
                                Tr("Your last save loads first; the game's Options screen then "
                                   "opens on New Game (after a cutscene, if the save starts in one: "
                                   "press Options yourself)")});
                rows.push_back({"LOAD GAME", "",
                                Tr("Your last save loads first; the game's Options screen then "
                                   "opens on Load Game (after a cutscene, if the save starts in one: "
                                   "press Options yourself)")});
            } else {
                rows.push_back({"NEW GAME", "", Tr("Start the game")});
            }
        } else {
            rows.push_back({"RESUME", "", Tr("Return to the game")});
        }
        rows.push_back({"OPTIONS", "",
                        page == Page::Start
                            ? Tr("Graphics, controls, gameplay, sound and interface")
                            : Tr("Graphics, controls, gameplay, sound, interface, save and load")});
        rows.push_back(
            {"ACHIEVEMENTS", trophies, Tr("Trophies of the game unlocked in this build")});
        if (page == Page::Start) {
            rows.push_back(
                {"QUIT", quit_armed ? Tr("Press again to quit") : "", Tr("Close the game")});
        } else {
            rows.push_back({"RETURN TO MAIN MENU", quit_armed ? Tr("Press again to confirm") : "",
                            quit_armed ? Tr("Progress since the last checkpoint is lost, and the game "
                                            "loads again (about 40 seconds)")
                                       : Tr("Back to the start screen. Quit is there")});
        }
    } else if (page == Page::Options) {
        // Gameplay, Sound and Interface are the game's own settings, kept in its saves: these
        // lines open the game's Options screen on that section instead of copying them.
        const char* how = home == Page::Main
                              ? Tr("Opens the game's own Options screen there")
                              : has_saves ? Tr("Your last save loads first; the game's Options screen "
                                               "then opens there")
                                          : Tr("Available from the pause menu once a game has started");
        rows.push_back({"GRAPHICS", "", Tr("Frame rate, display, upscaling, effects and shadows")});
        rows.push_back({"CONTROLS", "", Tr("Change the keyboard and mouse bindings")});
        rows.push_back({"GAMEPLAY", "",
                        fmt::format(fmt::runtime(Tr("Difficulty, aim sensitivity, photo mode. {}")),
                                    how)});
        rows.push_back(
            {"SOUND", "",
             fmt::format(fmt::runtime(Tr("Dialogue, music and effects volume, audio mix. {}")),
                         how)});
        rows.push_back(
            {"INTERFACE", "", fmt::format(fmt::runtime(Tr("Subtitles and the HUD. {}")), how)});
        if (home == Page::Main) {
            rows.push_back(
                {"SAVE AND LOAD", "",
                 Tr("Save Game, New Game and Load Game on the game's own Options screen")});
        }
        rows.push_back({"BACK", "", Tr("Return to the previous menu")});
    } else if (page == Page::Achievements) {
        for (int id = 0; id < Achievements::Count(); id++) {
            const u64 when = Achievements::UnlockedAt(id);
            std::string state = Tr("Locked");
            if (when != 0) {
                const std::time_t time = static_cast<std::time_t>(when);
                char text[32]{};
                std::strftime(text, sizeof(text), "%Y-%m-%d", std::localtime(&time));
                state = fmt::format(fmt::runtime(Tr("Unlocked {}")), text);
            }
            rows.push_back({Achievements::Name(id), state, Achievements::Description(id)});
        }
        rows.push_back({"BACK", "", Tr("Return to the previous menu")});
    } else if (page == Page::Controls) {
        const auto lines = ReadLines();
        for (const auto& action : Actions) {
            rows.push_back({action.label, KeysOf(lines, action.pad),
                            Tr("Confirm, then press the key or mouse button to use")});
        }
        rows.push_back({"BACK", "", Tr("Return to the previous menu")});
    } else {
        const u32 cap = EmulatorSettings.GetVblankFrequency();
        rows.push_back({"Frame Rate Cap", cap >= 1000 ? Tr("Uncapped") : fmt::format("{}", cap),
                        Tr("Applies the next time the game starts"), true});
        rows.push_back(
            {"VSync", EmulatorSettings.GetPresentMode() == "Fifo" ? Tr("On") : Tr("Off"),
             Tr("Applies the next time the game starts"), true});
        rows.push_back({"Fullscreen", EmulatorSettings.IsFullScreen() ? Tr("On") : Tr("Off"),
                        Tr("Applies at once"), true});
        rows.push_back({"Window Size",
                        fmt::format("{} x {}", EmulatorSettings.GetWindowWidth(),
                                    EmulatorSettings.GetWindowHeight()),
                        Tr("Applies at once when not in fullscreen"), true});
        rows.push_back({"Upscaling (FSR)", EmulatorSettings.IsFsrEnabled() ? Tr("On") : Tr("Off"),
                        Tr("Scales the 1080p picture to the window. Applies at once"), true});
        rows.push_back({"Sharpening", Tr(SharpenNames[SharpenIndex()]),
                        Tr("Sharpening pass of the upscaler. Applies at once"), true});
        rows.push_back({"Frame Rate Counter", fps_shown ? Tr("On") : Tr("Off"),
                        Tr("Applies at once"), true});
        const int preset = CurrentPreset();
        rows.push_back(
            {"Effects Preset", preset < 0 ? Tr("Custom") : Tr(PresetNames[preset]),
             Tr("Original keeps every effect; Sharp drops anti-aliasing; Minimal drops all"),
             true});
        for (const auto& effect : effects) {
            rows.push_back(
                {effect.label, effect.on ? Tr("On") : Tr("Off"), Tr(effect.description), true});
        }
        rows.push_back(
            {"Game Frame Limit", unlock_frame_rate ? Tr("Unlimited") : Tr("Game Setting"),
             Tr("Unlimited lifts the game's own 30 FPS limit (Options > Video > Frame "
                "Rate). Applies at once"),
             true});
        const int shadow_preset = CurrentShadowPreset();
        rows.push_back(
            {"Shadow Preset",
             shadow_preset < 0 ? Tr("Custom") : Tr(ShadowPresetNames[shadow_preset]),
             Tr("Sets the three shadow options below. Original keeps the game's picture"), true});
        rows.push_back({"Sun Shadows", Tr(ShadowRateNames[shadow_rate[0]]),
                        Tr("How often sun shadows are redrawn: the largest cost of a frame. Applies at once"),
                        true});
        rows.push_back({"Light Shadows", Tr(ShadowRateNames[shadow_rate[1]]),
                        Tr("The same for shadows cast by lamps and fires. Applies at once"), true});
        rows.push_back({"Shadow Detail", Tr(ShadowDetailNames[shadow_detail]),
                        Tr("Reduced and Low leave small objects out of the sun's shadows. Applies at once"),
                        true});
        // The game reads the language when it starts, and so do these menus.
        rows.push_back({"Language", LanguageNames[LanguageIndex()],
                        Tr("Applies the next time the game starts"), true});
        rows.push_back({"BACK", "", Tr("Return to the previous menu")});
    }
    return rows;
}

void AdjustGraphics(int row, int step) {
    switch (row) {
    case 0: {
        const int index = IndexOf(FrameCaps, EmulatorSettings.GetVblankFrequency(), 1);
        const int count = static_cast<int>(FrameCaps.size());
        EmulatorSettings.SetVblankFrequency(FrameCaps[(index + step + count) % count]);
        restart_note = true;
        break;
    }
    case 1:
        EmulatorSettings.SetPresentMode(EmulatorSettings.GetPresentMode() == "Fifo" ? "Immediate"
                                                                                     : "Fifo");
        restart_note = true;
        break;
    case 2: {
        const bool on = !EmulatorSettings.IsFullScreen();
        EmulatorSettings.SetFullScreen(on);
        EmulatorSettings.SetFullScreenMode(on ? "Fullscreen (Borderless)" : "Windowed");
        if (SDL_Window* window = Window()) {
            SDL_SetWindowFullscreen(window, on);
        }
        break;
    }
    case 3: {
        const std::pair<u32, u32> current{EmulatorSettings.GetWindowWidth(),
                                          EmulatorSettings.GetWindowHeight()};
        const int index = IndexOf(WindowSizes, current, 1);
        const int count = static_cast<int>(WindowSizes.size());
        const auto [width, height] = WindowSizes[(index + step + count) % count];
        EmulatorSettings.SetWindowWidth(width);
        EmulatorSettings.SetWindowHeight(height);
        if (SDL_Window* window = Window(); window && !EmulatorSettings.IsFullScreen()) {
            SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height));
        }
        break;
    }
    case 4:
        EmulatorSettings.SetFsrEnabled(!EmulatorSettings.IsFsrEnabled());
        break;
    case 5: {
        const int count = static_cast<int>(SharpenValues.size());
        const int index = (SharpenIndex() + step + count) % count;
        EmulatorSettings.SetRcasEnabled(index != 0);
        if (index != 0) {
            EmulatorSettings.SetRcasAttenuation(SharpenValues[index]);
        }
        break;
    }
    case 6:
        fps_shown = !fps_shown;
        Overlay::ToggleSimpleFps();
        return;
    case 7: {
        const int count = static_cast<int>(Presets.size());
        const int preset = (std::max(CurrentPreset(), 0) + step + count) % count;
        for (size_t e = 0; e < effects.size(); e++) {
            effects[e].on = Presets[preset][e];
        }
        ApplyEffects();
        SaveEffects();
        return;
    }
    default:
        if (row >= 8 && row < 8 + static_cast<int>(effects.size())) {
            effects[row - 8].on = !effects[row - 8].on;
            ApplyEffects();
            SaveEffects();
        } else if (row == 8 + static_cast<int>(effects.size())) {
            unlock_frame_rate = !unlock_frame_rate;
            ApplyEffects();
            SaveEffects();
        } else if (const int kind = row - 10 - static_cast<int>(effects.size()); kind == -1) {
            const int count = static_cast<int>(ShadowPresets.size());
            const auto& preset =
                ShadowPresets[(std::max(CurrentShadowPreset(), 0) + step + count) % count];
            shadow_rate = {preset[0], preset[1]};
            shadow_detail = preset[2];
            ApplyEffects();
            SaveEffects();
        } else if (kind >= 0 && kind < static_cast<int>(shadow_rate.size())) {
            shadow_rate[kind] = (shadow_rate[kind] + step + 3) % 3;
            ApplyEffects();
            SaveEffects();
        } else if (kind == static_cast<int>(shadow_rate.size())) {
            shadow_detail = (shadow_detail + step + 3) % 3;
            ApplyEffects();
            SaveEffects();
        } else if (kind == static_cast<int>(shadow_rate.size()) + 1) {
            // Language: kept in the emulator's settings, where the game reads it at its start.
            const int count = static_cast<int>(LanguageIds.size());
            EmulatorSettings.SetConsoleLanguage(
                LanguageIds[(LanguageIndex() + step + count) % count]);
            EmulatorSettings.Save();
        }
        return;
    }
    EmulatorSettings.Save();
}

/// The page a sub-page goes back to.
Page ParentOf(Page of) {
    return of == Page::Controls || of == Page::Graphics ? Page::Options : home;
}

// Holding the game stops its threads wherever they are. One of them can be stopped inside the
// runtime while it has a lock the picture needs (memory, the command queue): the menu would
// then never draw again. The watch below sees that the menu has stopped drawing while the game
// is held, lets the game run for a moment so the lock is given back, and holds it again.
std::mutex hold_mutex;
std::atomic<u64> last_draw_ms{0};

void WatchHold() {
    while (true) {
        SDL_Delay(100);
        std::scoped_lock lock{hold_mutex};
        if (!game_held || SDL_GetTicks() - last_draw_ms.load() < 500) {
            continue;
        }
        DebugState.ResumeGuestThreads();
        SDL_Delay(2);
        LOG_INFO(Frontend, "menu: picture stopped while the game was held, hold renewed");
        DebugState.PauseGuestThreads();
        last_draw_ms = SDL_GetTicks();
    }
}

void HoldGame(bool hold) {
    static std::once_flag watch_started;
    std::call_once(watch_started, [] { std::thread{WatchHold}.detach(); });
    last_draw_ms = SDL_GetTicks();
    std::scoped_lock lock{hold_mutex};
    if (hold && !game_held) {
        DebugState.PauseGuestThreads();
    } else if (!hold && game_held) {
        DebugState.ResumeGuestThreads();
    }
    game_held = hold;
}

void Press(u64 delay_ms, Libraries::Pad::OrbisPadButtonDataOffset button, u64 hold_ms = 150) {
    const u64 now = SDL_GetTicks();
    std::scoped_lock lock{timed_mutex};
    timed_presses.push_back({now + delay_ms, button, true});
    timed_presses.push_back({now + delay_ms + hold_ms, button, false});
}

void RunTimedPresses() {
    const u64 now = SDL_GetTicks();
    std::scoped_lock lock{timed_mutex};
    auto* controller = (*Common::Singleton<Input::GameControllers>::Instance())[0];
    std::erase_if(timed_presses, [&](const TimedPress& press) {
        if (now < press.at_ms) {
            return false;
        }
        controller->Button(press.button, press.down);
        return true;
    });
}

/// True when a save of the game exists for any user of this profile.
bool HasSaves() {
    std::error_code ec;
    const auto home_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "home";
    for (const auto& user : std::filesystem::directory_iterator(home_dir, ec)) {
        const auto saves = user.path() / "savedata" / "CUSA00004";
        for (const auto& entry : std::filesystem::directory_iterator(saves, ec)) {
            if (entry.is_directory(ec)) {
                return true;
            }
        }
    }
    return false;
}

/// True when the newest save is in Seattle. From there on the game's Options button opens the
/// map and Triangle on the map opens "Options and Saves"; in the village Options opens the
/// Options screen itself and Triangle would close it again.
bool OptionsOpensMap() {
    std::error_code ec;
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    const auto home_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "home";
    for (const auto& user : std::filesystem::directory_iterator(home_dir, ec)) {
        const auto saves = user.path() / "savedata" / "CUSA00004";
        for (const auto& entry : std::filesystem::directory_iterator(saves, ec)) {
            const auto file = entry.path() / "dis.dat";
            const auto time = std::filesystem::last_write_time(file, ec);
            if (!ec && (newest.empty() || time > newest_time)) {
                newest = file;
                newest_time = time;
            }
        }
    }
    if (newest.empty()) {
        return false;
    }
    std::string data;
    if (FILE* f = std::fopen(newest.string().c_str(), "rb")) {
        data.resize(1 << 20);
        data.resize(std::fread(data.data(), 1, data.size(), f));
        std::fclose(f);
    }
    return !data.empty() && data.find("world_village") == std::string::npos;
}

/// Opens the game's own Options screen and moves down to one of its rows.
void OpenGameOptions(u64 delay_ms, int game_row) {
    using Libraries::Pad::OrbisPadButtonDataOffset;
    Press(delay_ms, OrbisPadButtonDataOffset::Options);
    u64 rows_at = delay_ms + 1700;
    if (OptionsOpensMap()) {
        Press(delay_ms + 1500, OrbisPadButtonDataOffset::Triangle);
        rows_at = delay_ms + 3200;
    }
    for (int i = 0; i < game_row; i++) {
        Press(rows_at + 260 * i, OrbisPadButtonDataOffset::Down, 110);
    }
}

/// The game cannot go back to a menu by itself: the program starts again at the start screen.
void ReturnToMainMenu() {
    auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
    const auto eboot = mnt->GetHostPath("/app0/eboot.bin");
    LOG_INFO(Input, "PC menu: returning to the start screen");
    Common::Singleton<Core::Emulator>::Instance()->Restart(eboot);
}

void LeaveStartScreen(int game_row) {
    start_state = StartState::Done;
    home = Page::Main;
    open = false;
    pending_game_row = game_row;
    pending_ready_ms = 0;
}

/// Confirm on a row of the start screen or of the pause menu.
void ActivateTop(const std::string& label, int row) {
    using Libraries::Pad::OrbisPadButtonDataOffset;
    const bool needs_confirm = label == "QUIT" || label == "RETURN TO MAIN MENU";
    if (needs_confirm && !(quit_armed && quit_armed_row == row)) {
        quit_armed = true;
        quit_armed_row = row;
        return;
    }
    quit_armed = false;
    if (label == "CONTINUE") {
        LeaveStartScreen(-1);
    } else if (label == "NEW GAME") {
        LeaveStartScreen(has_saves ? 1 : -1);
    } else if (label == "LOAD GAME") {
        LeaveStartScreen(2);
    } else if (label == "RESUME") {
        open = false;
    } else if (label == "GAMEPLAY" || label == "SOUND" || label == "INTERFACE" ||
               label == "SAVE AND LOAD") {
        // Rows of the game's Options screen: Save Game, New Game, Load Game, Difficulty, Aim
        // Sensitivity, Vertical, Horizontal, Photo Mode, Dialogue, Music, Gameplay, Audio Mix,
        // Subtitles.
        const int game_row =
            label == "GAMEPLAY" ? 3 : label == "SOUND" ? 8 : label == "INTERFACE" ? 12 : 0;
        if (home == Page::Start) {
            if (has_saves) {
                LeaveStartScreen(game_row);
            }
        } else {
            open = false;
            OpenGameOptions(300, game_row);
        }
    } else if (label == "OPTIONS") {
        page = Page::Options;
        selected = 0;
    } else if (label == "BACK") {
        page = home;
        selected = 0;
    } else if (label == "CONTROLS") {
        page = Page::Controls;
        selected = 0;
    } else if (label == "GRAPHICS") {
        page = Page::Graphics;
        selected = 0;
    } else if (label == "ACHIEVEMENTS") {
        page = Page::Achievements;
        selected = 0;
    } else if (label == "QUIT") {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    } else if (label == "RETURN TO MAIN MENU") {
        ReturnToMainMenu();
    }
}

/// direction: 0 = confirm, -1 / +1 = left / right.
void Activate(int row, int direction, int count) {
    if (page == Page::Main) {
        if (direction != 0) {
            return;
        }
        if (row == 0) {
            page = Page::Controls;
            selected = 0;
        } else if (row == 1) {
            page = Page::Graphics;
            selected = 0;
        } else if (row == 2) {
            page = Page::Achievements;
            selected = 0;
        } else if (row == 3) {
            open = false;
        } else if (!quit_armed) {
            quit_armed = true;
            quit_armed_row = row;
        } else {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        if (row != quit_armed_row) {
            quit_armed = false;
        }
    } else if (row == count - 1) {
        if (direction == 0) {
            page = ParentOf(page);
            selected = 0;
        }
    } else if (page == Page::Controls) {
        if (direction == 0) {
            capturing = true;
        }
    } else if (page == Page::Achievements) {
        // A list to read: nothing to change.
    } else {
        AdjustGraphics(row, direction == 0 ? 1 : direction);
    }
}

ImFont* MenuFont() {
    return menu_font ? menu_font : ImGui::GetFont();
}

ImVec2 TextSize(float size, const std::string& text) {
    return MenuFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
}

void Text(ImDrawList* draw, float size, ImVec2 pos, ImU32 color, const std::string& text) {
    draw->AddText(MenuFont(), size, pos, color, text.c_str());
}

/// Text drawn narrower than the font: the game's screen uses condensed faces, the font at hand
/// (a Windows system font) is a regular one. `squeeze` scales the glyphs' width about `pos`.
void Narrow(ImDrawList* draw, float size, ImVec2 pos, ImU32 color, const std::string& text,
            float squeeze) {
    const int first = draw->VtxBuffer.Size;
    draw->AddText(MenuFont(), size, pos, color, text.c_str());
    for (int i = first; i < draw->VtxBuffer.Size; i++) {
        draw->VtxBuffer[i].pos.x = pos.x + (draw->VtxBuffer[i].pos.x - pos.x) * squeeze;
    }
}

constexpr float TitleSqueeze = 0.56f;
constexpr float HeadingSqueeze = 0.6f;
constexpr float EntrySqueeze = 0.86f;

/// Entries are written like the game writes them ("Save Game"): the labels that are kept in
/// capitals for matching get their words' first letters only.
std::string EntryCase(const std::string& label) {
    bool lower = false;
    for (const unsigned char c : label) {
        lower |= std::islower(c) != 0;
    }
    if (lower) {
        return label;
    }
    std::string out = label;
    bool start = true;
    for (char& c : out) {
        const unsigned char u = static_cast<unsigned char>(c);
        c = static_cast<char>(start ? std::toupper(u) : std::tolower(u));
        start = c == ' ';
    }
    return out;
}

/// A line's label as it is drawn: in the console language when there is a translation (the
/// table holds those in entry case already), else the English one through EntryCase.
std::string Shown(const std::string& label) {
    const std::string translated = Tr(label);
    return translated == label ? EntryCase(label) : translated;
}

/// A title or section heading as it is drawn. Headings are capitals: a translation is put in
/// capitals here, the English text is drawn as written ("inFAMOUS SECOND SON").
std::string Heading(const std::string& english) {
    const std::string translated = Tr(english);
    return translated == english ? english : Lang::Upper(translated);
}

/// The heading drawn over a line: empty for none, a single space for a gap without a heading.
std::string SectionOf(Page of, const std::string& label, int index) {
    if (label == "BACK") {
        return " ";
    }
    switch (of) {
    case Page::Start:
    case Page::Main:
        return index == 0 ? "GAME" : label == "OPTIONS" ? "SETUP" : label == "QUIT" ||
                                                                      label == "RETURN TO MAIN MENU"
                                                                      ? "EXIT"
                                                                      : "";
    case Page::Options:
        return label == "GRAPHICS" ? "PC" : label == "GAMEPLAY" ? "GAME"
                                          : label == "SAVE AND LOAD" ? "SAVE DATA" : "";
    case Page::Graphics:
        return index == 0 ? "DISPLAY"
               : label == "Upscaling (FSR)" ? "IMAGE"
               : label == "Effects Preset" ? "EFFECTS"
               : label == "Game Frame Limit" ? "FRAME RATE"
               : label == "Shadow Preset" ? "SHADOWS"
               : label == "Language" ? "LANGUAGE" : "";
    case Page::Controls:
        return index == 0 ? "ACTIONS"
               : label == "L3" ? "BUTTONS"
               : label == "Move Forward" ? "MOVEMENT"
               : label == "Hold Controller Sideways (Spray Can)" ? "MOTION AND TOUCHPAD" : "";
    case Page::Achievements:
        return index == 0 ? "TROPHIES" : "";
    }
    return "";
}

/// Width of one "[key] Label" hint.
float HintWidth(float scale, const char* keyboard, const char* pad, const char* label) {
    const bool keys = VideoCore::ActivePromptSet() == VideoCore::PromptSet::Keyboard;
    const float size = 26.0f * scale;
    const float cap_height = 30.0f * scale;
    const float cap_width =
        std::max(cap_height, TextSize(size * 0.8f, keys ? keyboard : pad).x + 18.0f * scale);
    return TextSize(size, label).x * EntrySqueeze + cap_width + 34.0f * scale;
}

/// One "[key] Label" hint, right aligned at `right_x`. Returns the width used.
float Hint(ImDrawList* draw, float right_x, float y, float scale, const char* keyboard,
           const char* pad, const char* label) {
    const bool keys = VideoCore::ActivePromptSet() == VideoCore::PromptSet::Keyboard;
    const char* name = keys ? keyboard : pad;
    const float size = 26.0f * scale;
    const float label_width = TextSize(size, label).x * EntrySqueeze;
    const float cap_height = 30.0f * scale;
    const float cap_width =
        std::max(cap_height, TextSize(size * 0.8f, name).x + 18.0f * scale);
    const float label_x = right_x - label_width;
    const float cap_x = label_x - 10.0f * scale - cap_width;
    draw->AddRectFilled({cap_x, y}, {cap_x + cap_width, y + cap_height}, White,
                        keys ? 6.0f * scale : cap_height * 0.5f);
    const ImVec2 name_size = TextSize(size * 0.8f, name);
    Text(draw, size * 0.8f,
         {cap_x + (cap_width - name_size.x) * 0.5f, y + (cap_height - name_size.y) * 0.5f},
         IM_COL32(16, 16, 16, 255), name);
    Narrow(draw, size, {label_x, y + (cap_height - TextSize(size, label).y) * 0.5f}, White, label,
           EntrySqueeze);
    return label_width + cap_width + 34.0f * scale;
}

} // namespace

void SetFont(ImFont* font) {
    menu_font = font;
}

ImFont* Font() {
    return MenuFont();
}

void Toggle() {
    if (start_state == StartState::Showing) {
        return; // the start screen closes through its own rows
    }
    if (!open) {
        page = Page::Main;
        home = Page::Main;
        selected = 0;
        opened_ms = SDL_GetTicks();
    }
    capturing = false;
    open = !open;
}

bool OnOptionsButton(bool pressed) {
    static const bool enabled = Common::ElfInfo::Instance().GameSerial() == "CUSA00004";
    if (!enabled) {
        return false;
    }
    if (FirstRun::IntroActive() || start_state == StartState::Showing) {
        return true;
    }
    if (pressed) {
        Toggle();
        options_consumed = true;
        return true;
    }
    if (options_consumed) {
        options_consumed = false;
        return true;
    }
    return false;
}

bool IsOpen() {
    return open;
}

bool OnEvent(const SDL_Event& event) {
    if (FirstRun::IntroActive()) {
        // Nothing reaches the game, or this menu, while the opening lines play.
        return event.type != SDL_EVENT_KEY_UP && event.type != SDL_EVENT_MOUSE_BUTTON_UP &&
               event.type != SDL_EVENT_GAMEPAD_BUTTON_UP && event.type != SDL_EVENT_QUIT;
    }
    if (!open) {
        return false;
    }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        // The key that opened the overlay closes it; it does not reach the hotkey layer while
        // the overlay holds the input.
        if (!capturing && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F2 &&
            !event.key.repeat) {
            Toggle();
            return true;
        }
        if (capturing) {
            capturing = false;
            // Escape leaves the binding as it was.
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                return true;
            }
            const std::string name = NameOf(event);
            if (!name.empty() && page == Page::Controls && selected >= 0 &&
                selected < static_cast<int>(Actions.size())) {
                Bind(Actions[selected].pad, name);
            }
        }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            pad_nav |= NavUp;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            pad_nav |= NavDown;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            pad_nav |= NavLeft;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            pad_nav |= NavRight;
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
            pad_nav |= NavConfirm;
            break;
        case SDL_GAMEPAD_BUTTON_START:
        case SDL_GAMEPAD_BUTTON_EAST:
            pad_nav |= NavBack;
            break;
        default:
            break;
        }
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // The left stick moves the selection too: one step each time it leaves the centre.
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            const int direction = event.gaxis.value < -20000 ? -1 : event.gaxis.value > 20000 ? 1 : 0;
            if (direction != stick_direction && direction != 0) {
                pad_nav |= direction < 0 ? NavUp : NavDown;
            }
            stick_direction = direction;
        }
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        return true;
    default:
        // Releases still reach the game, so nothing held when the menu opened stays held.
        return false;
    }
}

void Draw() {
    using namespace ImGui;

    static const bool enabled = Common::ElfInfo::Instance().GameSerial() == "CUSA00004";
    if (!enabled) {
        return;
    }
    static const bool effects_loaded = (LoadEffects(), true);
    FirstRun::DrawIntro();
    if (FirstRun::IntroActive()) {
        return;
    }
    if (start_state == StartState::Unknown) {
        // Test runs go straight into the game unless they ask for the start screen.
        static const bool skip = std::getenv("SOTC_HEADLESS") != nullptr &&
                                 std::getenv("I3_START_SCREEN") == nullptr;
        if (skip) {
            start_state = StartState::Done;
        } else if (g_first_flip_ms.load(std::memory_order_relaxed) != 0) {
            start_state = StartState::Showing;
            has_saves = HasSaves();
            page = Page::Start;
            home = Page::Start;
            selected = 0;
            opened_ms = SDL_GetTicks();
            open = true;
        }
    }
    RunTimedPresses();
    if (pending_game_row >= 0 && Common::g_save_mounts.load() >= 3) {
        // The last save has loaded (the game mounts its saves again once it is in play): open
        // the game's Options screen and move to the row that was asked for.
        const u64 now = SDL_GetTicks();
        if (pending_ready_ms == 0) {
            pending_ready_ms = now;
        } else if (now - pending_ready_ms > 5000) {
            using Libraries::Pad::OrbisPadButtonDataOffset;
            OpenGameOptions(0, pending_game_row);
            pending_game_row = -1;
        }
    }
    const bool pad_combo =
        (IsKeyDown(ImGuiKey_GamepadL3) && IsKeyPressed(ImGuiKey_GamepadR3, false)) ||
        (IsKeyDown(ImGuiKey_GamepadR3) && IsKeyPressed(ImGuiKey_GamepadL3, false));
    if (pad_combo) {
        Toggle();
    }
    HoldGame(open);
    if (!open) {
        return;
    }

    const std::vector<Row> rows = BuildRows();
    const int count = static_cast<int>(rows.size());
    selected = std::clamp(selected, 0, count - 1);
    if (quit_armed && ((page != Page::Main && page != Page::Start) || selected != quit_armed_row)) {
        quit_armed = false;
    }
    int activate_row = -1, activate_direction = 0;
    bool back = false;
    // While a key is being captured the keys below must not act on the menu as well.
    static bool was_capturing = false;
    // The key press that opened the menu must not act on it as well.
    const bool skip_input = capturing || was_capturing || SDL_GetTicks() - opened_ms < 250;
    was_capturing = capturing;
    const int nav = pad_nav.exchange(0);
    if (!skip_input) {
        if (IsKeyPressed(ImGuiKey_DownArrow) || IsKeyPressed(ImGuiKey_GamepadDpadDown) ||
            IsKeyPressed(ImGuiKey_S) || (nav & NavDown)) {
            selected = (selected + 1) % count;
        }
        if (IsKeyPressed(ImGuiKey_UpArrow) || IsKeyPressed(ImGuiKey_GamepadDpadUp) ||
            IsKeyPressed(ImGuiKey_W) || (nav & NavUp)) {
            selected = (selected + count - 1) % count;
        }
        if (IsKeyPressed(ImGuiKey_Enter, false) || IsKeyPressed(ImGuiKey_Space, false) ||
            IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || (nav & NavConfirm)) {
            activate_row = selected;
        } else if (IsKeyPressed(ImGuiKey_LeftArrow) || IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
                   IsKeyPressed(ImGuiKey_A) || (nav & NavLeft)) {
            activate_row = selected;
            activate_direction = -1;
        } else if (IsKeyPressed(ImGuiKey_RightArrow) || IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
                   IsKeyPressed(ImGuiKey_D) || (nav & NavRight)) {
            activate_row = selected;
            activate_direction = 1;
        }
        back = IsKeyPressed(ImGuiKey_Escape, false) || IsKeyPressed(ImGuiKey_Backspace, false) ||
               IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
               IsKeyPressed(ImGuiKey_GamepadStart, false) || (nav & NavBack);
    }

    // Laid out like the game's Options screen: a narrow framed panel on the left with a tall
    // yellow title, section headings with indented entries under them, values on the right
    // edge, and the text for the selected line beside the panel.
    const ImVec2 display = GetIO().DisplaySize;
    const float scale = display.y / 1080.0f;
    const float ox = (display.x - 1920.0f * scale) * 0.5f;
    auto* draw = GetForegroundDrawList();
    if (home == Page::Start) {
        // No scene is loaded behind the start screen: a plain dark backdrop drawn here.
        draw->AddRectFilledMultiColor({0.0f, 0.0f}, display, IM_COL32(46, 42, 40, 255),
                                      IM_COL32(30, 28, 27, 255), IM_COL32(6, 6, 6, 255),
                                      IM_COL32(12, 11, 11, 255));
    } else {
        // The game's own screen greys and blurs the picture; this one greys it with a veil.
        draw->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(34, 30, 30, 168));
    }
    const ImVec2 panel_min{ox + 316.0f * scale, 92.0f * scale};
    const ImVec2 panel_max{ox + 1057.0f * scale, 988.0f * scale};
    draw->AddRectFilled({panel_max.x + 3.0f * scale, panel_min.y + 3.0f * scale},
                        {panel_max.x + 16.0f * scale, panel_max.y}, IM_COL32(120, 120, 120, 70));
    draw->AddRectFilled(panel_min, panel_max, IM_COL32(17, 17, 17, 240));
    for (float x = panel_min.x + 12.0f * scale; x < panel_max.x; x += 12.0f * scale) {
        draw->AddLine({x, panel_min.y}, {x, panel_max.y}, IM_COL32(255, 255, 255, 7));
    }
    for (float line_y = panel_min.y + 12.0f * scale; line_y < panel_max.y; line_y += 12.0f * scale) {
        draw->AddLine({panel_min.x, line_y}, {panel_max.x, line_y}, IM_COL32(255, 255, 255, 7));
    }
    draw->AddRect(panel_min, panel_max, White, 0.0f, 0, 2.0f * scale);
    // Corner marks of the frame: a square top left, a thick bar with a slanted end top right.
    draw->AddRectFilled({panel_min.x - 6.0f * scale, panel_min.y - 6.0f * scale},
                        {panel_min.x + 5.0f * scale, panel_min.y + 5.0f * scale}, White);
    draw->AddQuadFilled({panel_max.x - 134.0f * scale, panel_min.y},
                        {panel_max.x - 124.0f * scale, panel_min.y - 7.0f * scale},
                        {panel_max.x, panel_min.y - 7.0f * scale}, {panel_max.x, panel_min.y},
                        White);

    const char* title = page == Page::Main           ? "PAUSED"
                        : page == Page::Start        ? "inFAMOUS SECOND SON"
                        : page == Page::Options      ? "OPTIONS"
                        : page == Page::Controls     ? "CONTROLS"
                        : page == Page::Achievements ? "ACHIEVEMENTS"
                                                     : "GRAPHICS";
    Narrow(draw, 94.0f * scale, {panel_min.x + 32.0f * scale, panel_min.y + 24.0f * scale}, Yellow,
           Heading(title), TitleSqueeze);

    const float heading_x = panel_min.x + 71.0f * scale;
    const float left = panel_min.x + 99.0f * scale;
    const float right = panel_min.x + 668.0f * scale;
    const float row_height = 35.0f * scale;
    const float text_size = 28.0f * scale;
    const float heading_size = 42.0f * scale;
    const float heading_height = 44.0f * scale;
    const float section_gap = 22.0f * scale;
    const float view_top = panel_min.y + 148.0f * scale;
    const float view_bottom = panel_max.y - 14.0f * scale;
    // Where each line sits in the list, headings included.
    std::vector<float> block_top(count), row_top(count);
    float content = 0.0f;
    for (int i = 0; i < count; i++) {
        const std::string section = SectionOf(page, rows[i].label, i);
        block_top[i] = content;
        if (!section.empty()) {
            content += i == 0 ? 0.0f : section_gap;
            if (section != " ") {
                content += heading_height;
            }
        }
        row_top[i] = content;
        content += row_height;
    }
    // Keep the selection, and the heading over it, in view on long pages.
    static float scroll = 0.0f;
    const float view = view_bottom - view_top;
    scroll = std::min(scroll, block_top[selected]);
    scroll = std::max(scroll, row_top[selected] + row_height - view);
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, content - view));

    const ImVec2 mouse = GetIO().MousePos;
    const bool mouse_moved = GetIO().MouseDelta.x != 0.0f || GetIO().MouseDelta.y != 0.0f;
    draw->PushClipRect({panel_min.x, view_top}, {panel_max.x, view_bottom}, true);
    for (int i = 0; i < count; i++) {
        const Row& row = rows[i];
        const float row_y = view_top + row_top[i] - scroll;
        if (row_y + row_height < view_top || block_top[i] - scroll > view) {
            continue;
        }
        const std::string section = SectionOf(page, row.label, i);
        // A heading cut by the top edge of the list is left out rather than drawn in part.
        if (!section.empty() && section != " " && row_y - heading_height >= view_top - 1.0f) {
            Narrow(draw, heading_size, {heading_x, row_y - heading_height}, White,
                   Heading(section), HeadingSqueeze);
        }
        const bool hovered = mouse.x >= left && mouse.x <= right && mouse.y >= row_y &&
                             mouse.y < row_y + row_height && mouse.y >= view_top &&
                             mouse.y < view_bottom;
        if (hovered && !skip_input) {
            if (mouse_moved) {
                selected = i;
            }
            if (IsMouseClicked(ImGuiMouseButton_Left)) {
                selected = i;
                activate_row = i;
                activate_direction = 0;
            }
        }
        const bool current = i == selected;
        const ImU32 color = current ? Yellow : White;
        const std::string label = Shown(row.label);
        const float text_y = row_y + (row_height - TextSize(text_size, label).y) * 0.5f;
        Narrow(draw, text_size, {left, text_y}, color, label, EntrySqueeze);
        std::string value = row.value;
        if (current && capturing) {
            value = Tr("Press a key...");
        } else if (row.adjustable && current) {
            value = "<  " + value + "  >";
        }
        if (!value.empty()) {
            Narrow(draw, text_size,
                   {right - TextSize(text_size, value).x * EntrySqueeze, text_y}, color, value,
                   EntrySqueeze);
        }
    }
    draw->PopClipRect();
    if (content > view) {
        // Scroll position, where the game's screen has it.
        const float bar_x = panel_min.x + 710.0f * scale;
        const float bar_height = view * view / content;
        const float bar_y = view_top + (view - bar_height) * scroll / (content - view);
        draw->AddRectFilled({bar_x, view_top}, {bar_x + 14.0f * scale, view_bottom},
                            IM_COL32(0, 0, 0, 120));
        draw->AddRectFilled({bar_x, bar_y}, {bar_x + 14.0f * scale, bar_y + bar_height},
                            IM_COL32(70, 70, 70, 255));
    }

    // Beside the panel, where the game shows its controller picture: the selected line as a
    // heading over a rule, and what it does under it.
    const Row& current_row = rows[selected];
    const float note_x = panel_max.x + 52.0f * scale;
    const float note_right = std::min(display.x - 60.0f * scale, note_x + 700.0f * scale);
    float note_y = 400.0f * scale;
    // In capitals, through the language's own letters (std::toupper only knows ASCII).
    const std::string note_title = Lang::Upper(Tr(current_row.label));
    Narrow(draw, 34.0f * scale, {note_x, note_y}, White, note_title, HeadingSqueeze);
    note_y += 40.0f * scale;
    draw->AddLine({note_x, note_y}, {note_right, note_y}, IM_COL32(236, 236, 236, 150), scale);
    note_y += 14.0f * scale;
    const std::string note =
        capturing ? Tr("Press the key or mouse button for this action. Esc keeps the current one.")
                  : current_row.description;
    const float note_size = 26.0f * scale;
    draw->AddText(MenuFont(), note_size, {note_x, note_y}, White, note.c_str(), nullptr,
                  note_right - note_x);
    note_y += MenuFont()->CalcTextSizeA(note_size, FLT_MAX, note_right - note_x, note.c_str()).y;
    if (restart_note && page == Page::Graphics) {
        draw->AddText(MenuFont(), note_size, {note_x, note_y + 18.0f * scale}, Yellow,
                      Tr("Frame rate cap and VSync changes take effect the next time the game "
                         "starts."),
                      nullptr, note_right - note_x);
    }

    // Key hints on a dark strip at the bottom right, as on the game's screen.
    const float hint_y = 952.0f * scale;
    const bool on_top = page == Page::Main || page == Page::Start || page == Page::Options;
    const char* back_label = page == Page::Main ? Tr("Resume") : Tr("Back");
    const bool show_back = page != Page::Start;
    const char* act_label = current_row.adjustable ? Tr("Change") : Tr("Select");
    const char* act_keys = current_row.adjustable ? "A  D" : "ENTER";
    const char* act_pad = current_row.adjustable ? "D-PAD" : "X";
    (void)on_top;
    float strip = 40.0f * scale + HintWidth(scale, act_keys, act_pad, act_label);
    if (show_back) {
        strip += HintWidth(scale, "ESC", "O", back_label);
    }
    strip = std::max(strip, 274.0f * scale);
    draw->AddRectFilled({display.x - strip, hint_y - 12.0f * scale},
                        {display.x, hint_y + 43.0f * scale}, IM_COL32(12, 12, 12, 215));
    float hint_x = display.x - 28.0f * scale;
    if (show_back) {
        hint_x -= Hint(draw, hint_x, hint_y, scale, "ESC", "O", back_label);
    }
    Hint(draw, hint_x, hint_y, scale, act_keys, act_pad, act_label);

    if (activate_row >= 0) {
        if (page == Page::Main || page == Page::Start || page == Page::Options) {
            if (activate_direction == 0) {
                ActivateTop(rows[activate_row].label, activate_row);
            }
        } else {
            Activate(activate_row, activate_direction, count);
        }
    } else if (back) {
        if (page == Page::Main) {
            open = false;
        } else if (page != Page::Start) {
            page = ParentOf(page);
            selected = 0;
        }
    }
}

} // namespace I3::Menu
