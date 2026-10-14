// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include "common/types.h"
#include "core/tlg/ui_textures.h"

// The Last Guardian draws its menus from pre-rendered textures. The renderer tells this module
// which of the known menu textures (ui_textures.h) each frame samples, so the PC rows of the
// Options screen know when that screen is up, and it can leave out draws that sample a texture
// the rows replace (the game's cursor and its Back line, moved below the PC rows).
namespace Tlg::Ui {

constexpr u32 Bit(Tag tag) {
    return 1u << tag;
}

bool Enabled();
/// Tag of a texture by the hash of its guest data, or -1.
int Lookup(u64 hash);
/// Called by the renderer for each draw with the tags of the textures it samples. Returns true
/// when the draw is to be left out.
bool OnDraw(u32 tags);
/// Called once per presented frame: the tags of the last finished frame become Seen().
void EndFrame();
u32 Seen();
/// Draws sampling any of these tags are left out.
void SetHidden(u32 tags);

} // namespace Tlg::Ui
