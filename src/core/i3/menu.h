// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

union SDL_Event;
struct ImFont;

// The menus of the inFAMOUS Second Son build. The game itself has no main menu (it loads the
// last save at once) and draws its Options screen itself, so lines cannot be added to it:
// - a start screen at launch: Continue, New Game, Load Game, Controls, Graphics,
//   Achievements, Quit;
// - one pause menu on the Options button (Esc), holding the game's own Options screen next to
//   Controls, Graphics, Achievements and Return to Main Menu.
namespace I3::Menu {

void Toggle();
/// The pad's Options button, after the binding layer. Returns true when the menu took it (it
/// opens or closes the pause menu) and the game must not see it.
bool OnOptionsButton(bool pressed);
bool IsOpen();
/// Draws the overlay; called once per frame inside the ImGui frame.
void Draw();
/// Lets the overlay see an input event before the game does. Returns true when the event
/// belongs to the overlay (it is open, or waiting for a key to bind) and must not reach the game.
bool OnEvent(const SDL_Event& event);
/// The condensed face used for the overlay text, loaded once with the other fonts.
void SetFont(ImFont* font);
ImFont* Font();

// The binding file of the running game: its lines, the keyboard keys bound to a pad output,
// binding one key (the file is rewritten, applied at once and the icons redrawn), the binding
// name of a key or mouse event, and how a key name is shown.
std::vector<std::string> ReadLines();
std::string KeysOf(const std::vector<std::string>& lines, const char* pad);
void Bind(const char* pad, const std::string& key);
std::string NameOf(const SDL_Event& event);
std::string DisplayName(const std::string& key);

} // namespace I3::Menu
