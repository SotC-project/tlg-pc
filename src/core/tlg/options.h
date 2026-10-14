// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "input/input_handler.h"

union SDL_Event;
struct ImFont;

// The PC settings of The Last Guardian build, as rows of the game's own Options screen:
// Camera, Screen, Audio, Copyright, then Graphics and Controls, then Back. The game draws its
// rows from textures and keeps its own cursor; this module sees that screen through the
// textures it samples (core/tlg/ui.h), keeps the cursor across both sets of rows, draws the PC
// rows and their pages in the screen's style, and holds the pad input back from the game while
// a PC row or page has it.
namespace Tlg::Options {

void Draw();
/// A pad button after the binding layer. True when the PC rows took it.
bool OnButton(Libraries::Pad::OrbisPadButtonDataOffset button, bool pressed);
/// A stick axis after the binding layer (value -128..127). True when the PC rows took it; the
/// game then sees the stick centred.
bool OnAxis(Input::Axis axis, int value);
/// Raw input, for capturing a key to bind. True when it belongs to the PC rows.
bool OnEvent(const SDL_Event& event);
void SetFont(ImFont* font);

} // namespace Tlg::Options
