// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include "common/elf_info.h"
#include "common/logging/log.h"
#include "core/tlg/ui.h"

namespace Tlg::Ui {

namespace {
std::atomic<u32> frame_tags{0};
std::atomic<u32> seen{0};
std::atomic<u32> hidden{0};
} // namespace

bool Enabled() {
    static const bool tlg = Common::ElfInfo::Instance().GameSerial() == "CUSA03627";
    return tlg;
}

int Lookup(u64 hash) {
    for (const auto& entry : TextureTags) {
        if (entry.hash == hash) {
            return static_cast<int>(entry.tag);
        }
    }
    return -1;
}

bool OnDraw(u32 tags) {
    if (tags == 0) {
        return false;
    }
    frame_tags.fetch_or(tags, std::memory_order_relaxed);
    return (tags & hidden.load(std::memory_order_relaxed)) != 0;
}

void EndFrame() {
    const u32 tags = frame_tags.exchange(0, std::memory_order_relaxed);
    // Test aid: TLG_UILOG=1 logs the set of menu textures whenever it changes.
    static const bool log = std::getenv("TLG_UILOG") != nullptr;
    if (log && tags != seen.load(std::memory_order_relaxed)) {
        LOG_ERROR(Frontend, "menu textures: {:#x}", tags);
    }
    seen.store(tags, std::memory_order_relaxed);
}

u32 Seen() {
    return seen.load(std::memory_order_relaxed);
}

void SetHidden(u32 tags) {
    hidden.store(tags, std::memory_order_relaxed);
}

} // namespace Tlg::Ui
