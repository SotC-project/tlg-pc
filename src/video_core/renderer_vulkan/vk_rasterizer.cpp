// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <set>
#include <map>
#include <chrono>
#include "common/recording.h"
#include <chrono>
#include <intrin.h>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <string>
#include "video_core/pass_filter.h"
#include "common/debug.h"
#include "common/elf_info.h"
#include "common/path_util.h"
#include "video_core/op_trace.h"
#include "video_core/thread_sampler.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "shader_recompiler/resource.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "core/tlg/ui.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_hle.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/texture_cache.h"

// The Last Guardian: menu textures (core/tlg/ui.h) sampled by the draw being bound.
static u32 g_tlg_draw_tags = 0;

namespace Vulkan {

struct PassStat {
    u64 color, depth;
    u32 cw, ch, dw, dh;
    u32 draws;
    u64 indices;
    int vx, vy, vw;
    // How the draws of the pass look: up to 36 and up to 300 indices, drawn with the pipeline
    // of the draw before, with depth writes on, the depth test of the last one, and how many
    // different vertex shaders (counted up to eight).
    u32 tiny{}, small{}, same_pipeline{}, depth_write{}, depth_func{};
    u64 vs_seen[8]{};
    u32 vs_count{};
};
static std::vector<PassStat> g_pass_stat;
// Test aid (I3_PASSSTAT): direct draws per shader pair, to tell which shaders issue the many
// small draws of a scene. Key: vertex and fragment shader hash.
struct PairStat {
    u32 draws{};
    u64 indices{};
    u32 tiny{};
};
static std::map<std::pair<u64, u64>, PairStat> g_pair_stat;
// Test aid (I3_PASSSTAT): for direct draws that use the pipeline of the draw before, which
// kinds of commands ran in between (Liverpool::change_mask_since_draw).
static std::map<u32, u32> g_change_stat;
struct CsWriter {
    u64 hash;
    u32 size, x, y, z;
};
static std::map<u64, CsWriter> g_cs_writers;

// Test aid (I3_PASSSTAT): which compute shader last wrote which buffer, whatever the buffer
// kind and whether the dispatch was direct or indirect (see DrawIndirect).
static void NoteCsWriters(const Shader::Info& cs, u32 x, u32 y, u32 z) {
    for (const auto& desc : cs.buffers) {
        if (!desc.is_written || desc.buffer_type != Shader::BufferType::Guest) {
            continue;
        }
        const auto sharp = desc.GetSharp(cs);
        if (sharp.base_address != 0) {
            g_cs_writers[sharp.base_address] = {cs.pgm_hash, u32(sharp.GetSize()), x, y, z};
        }
    }
}

// Shadow map options of the inFAMOUS Second Son build. About 70% of the game's draws go into
// shadow maps, so how often those are redrawn is the largest single cost the player can trade.
// Shadow maps are the square depth targets: the sun's is the 2048x2048 one that received the
// most draws in the frame before (a second 2048x2048 target is left alone: dropping it changes
// neither the picture nor the cost), the 1024x1024 ones belong to local lights. A map that gets
// no draws in a frame is not cleared either, so it keeps what the frame before drew into it.
struct ShadowMaps {
    bool enabled = false; // known once the game is loaded, see NextFrame
    u64 frame = 0;
    u64 sun = 0;
    struct Count {
        u64 address;
        u32 draws;
    };
    boost::container::small_vector<Count, 4> large;

    u32 seen[2][2]{};
    u32 clears = 0;
    u32 sizes[6]{};
    u64 logged_frame = 0;
    bool Skip(const AmdGpu::DepthBuffer& depth, bool clear, u64 indices) {
        const u32 size = depth.Pitch();
        if (size < 1024 || size != depth.Height() || !depth.DepthValid()) {
            return false;
        }
        const u64 address = depth.DepthAddress();
        int kind = 1;
        if (size >= 2048) {
            auto it = std::ranges::find(large, address, &Count::address);
            if (it == large.end()) {
                large.push_back({address, 0});
                it = std::prev(large.end());
            }
            ++it->draws;
            if (address != sun) {
                return false;
            }
            kind = 0;
        }
        const int rate = Common::g_shadow_rate[kind].load(std::memory_order_relaxed);
        // The two kinds take turns, which keeps the cost of consecutive frames even.
        // Off still lets the game's clear of the map through: an empty map casts no shadow, while
        // one that is never cleared would keep the shadows of the last frame drawn into it.
        bool skip = rate == 2 ? !clear : (rate == 1 && ((frame + kind) & 1) != 0);
        // The sun's map is redrawn every frame even at half rate. It follows the camera, so a
        // map kept from the frame before does not fit a frame in which the camera has moved:
        // the scene flashed dark with the Balanced and Fast presets. Keeping it only while the
        // camera rests was tried (by the constants of the first draw into the map) and flashed
        // more, because those constants also change for other reasons.
        if (kind == 0 && rate == 1) {
            skip = false;
        }
        // Shadow Detail: small objects are left out of the sun's map (never the clear).
        skip |= kind == 0 && !clear &&
                indices < Common::g_shadow_min_indices.load(std::memory_order_relaxed);
        ++seen[kind][skip];
        clears += clear;
        return skip;
    }

    void NextFrame() {
        if (frame++ == 0) {
            enabled = Common::ElfInfo::Instance().GameSerial() == "CUSA00004";
        }
        if (!large.empty()) {
            sun = std::ranges::max_element(large, {}, &Count::draws)->address;
            large.clear();
        }
    }
};
static ShadowMaps g_shadow_maps;
static const std::vector<u64> g_skip_depth = [] {
    std::vector<u64> list;
    if (const char* env = std::getenv("I3_SKIPDEPTH")) {
        char* end = nullptr;
        for (const char* at = env; *at; at = *end ? end + 1 : end) {
            list.push_back(std::strtoull(at, &end, 16));
        }
    }
    return list;
}();
static const bool g_pass_stat_on = std::getenv("I3_PASSSTAT") != nullptr;

static Shader::PushData MakeUserData(const AmdGpu::Regs& regs) {
    // TODO(roamic): Add support for multiple viewports and geometry shaders when ViewportIndex
    // is encountered and implemented in the recompiler.
    Shader::PushData push_data{};
    push_data.xoffset = regs.viewport_control.xoffset_enable ? regs.viewports[0].xoffset : 0.f;
    push_data.xscale = regs.viewport_control.xscale_enable ? regs.viewports[0].xscale : 1.f;
    push_data.yoffset = regs.viewport_control.yoffset_enable ? regs.viewports[0].yoffset : 0.f;
    push_data.yscale = regs.viewport_control.yscale_enable ? regs.viewports[0].yscale : 1.f;
    return push_data;
}

Rasterizer::Rasterizer(const Instance& instance_, Scheduler& scheduler_, Runtime& runtime_,
                       AmdGpu::Liverpool* liverpool_)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_}, page_manager{this},
      buffer_cache{instance, scheduler, runtime, liverpool_, texture_cache, page_manager},
      texture_cache{instance, scheduler, runtime, liverpool_, buffer_cache, page_manager},
      liverpool{liverpool_}, memory{Core::Memory::Instance()},
      pipeline_cache{instance, scheduler, liverpool, buffer_cache.GetSparsePageShift()},
      host_markers_enabled{EmulatorSettings.IsVkHostMarkersEnabled()},
      guest_markers_enabled{EmulatorSettings.IsVkGuestMarkersEnabled()} {
    if (!EmulatorSettings.IsNullGPU()) {
        liverpool->BindRasterizer(this);
    }
    memory->SetRasterizer(this);

    scheduler.SetSubmitCallback([this](Vulkan::SubmitInfo& info) {
        runtime.FlushBarriers();
        buffer_cache.SubmitPendingArenaBinds(info);
    });
}

Rasterizer::~Rasterizer() = default;

bool Rasterizer::FilterDraw() {
    const auto& regs = liverpool->regs;
    if (regs.color_control.mode == AmdGpu::ColorControl::OperationMode::EliminateFastClear) {
        // Clears the render target if FCE is launched before any draws
        EliminateFastClear();
        return false;
    }
    if (regs.color_control.mode == AmdGpu::ColorControl::OperationMode::FmaskDecompress) {
        // TODO: check for a valid MRT1 to promote the draw to the resolve pass.
        LOG_TRACE(Render_Vulkan, "FMask decompression pass skipped");
        ScopedMarkerInsert("FmaskDecompress");
        EliminateFastClear();
        return false;
    }
    if (regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Resolve) {
        LOG_TRACE(Render_Vulkan, "Resolve pass");
        Resolve();
        return false;
    }
    if (regs.primitive_type == AmdGpu::PrimitiveType::None) {
        LOG_TRACE(Render_Vulkan, "Primitive type 'None' skipped");
        ScopedMarkerInsert("PrimitiveTypeNone");
        return false;
    }

    const bool cb_disabled =
        regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Disable;
    const auto depth_copy =
        regs.depth_render_override.force_z_dirty && regs.depth_render_override.force_z_valid &&
        regs.depth_buffer.DepthValid() && regs.depth_buffer.DepthWriteValid() &&
        regs.depth_buffer.DepthAddress() != regs.depth_buffer.DepthWriteAddress();
    const auto stencil_copy =
        regs.depth_render_override.force_stencil_dirty &&
        regs.depth_render_override.force_stencil_valid && regs.depth_buffer.StencilValid() &&
        regs.depth_buffer.StencilWriteValid() &&
        regs.depth_buffer.StencilAddress() != regs.depth_buffer.StencilWriteAddress();
    if (cb_disabled && (depth_copy || stencil_copy)) {
        // Games may disable color buffer and enable force depth/stencil dirty and valid to
        // do a copy from one depth-stencil surface to another, without a pixel shader.
        // We need to detect this case and perform the copy, otherwise it will have no effect.
        LOG_TRACE(Render_Vulkan, "Performing depth-stencil override copy");
        DepthStencilCopy(depth_copy, stencil_copy);
        return false;
    }

    return true;
}

void Rasterizer::PrepareRenderState(const GraphicsPipeline* pipeline) {
    // Prefetch render targets to handle overlaps with bound textures (e.g. mipgen)
    const auto& key = pipeline->GetGraphicsKey();
    const auto& regs = liverpool->regs;
    if (regs.color_control.degamma_enable) {
        LOG_WARNING(Render_Vulkan, "Color buffers require gamma correction");
    }

    const bool skip_cb_binding =
        regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Disable;
    for (s32 cb = 0; cb < std::bit_width(key.mrt_mask); ++cb) {
        auto& [image_id, desc] = cb_descs[cb];
        const auto& col_buf = regs.color_buffers[cb];
        const u32 target_mask = regs.color_target_mask.GetMask(cb);
        if (skip_cb_binding || !col_buf || !target_mask || (key.mrt_mask & (1 << cb)) == 0) {
            image_id = {};
            continue;
        }
        const auto& hint = liverpool->last_cb_extent[cb];
        std::construct_at(&desc, col_buf, hint);
        image_id = bound_images.emplace_back(texture_cache.FindImage(desc));
        auto& image = texture_cache.GetImage(image_id);
        image.binding.is_target = 1u;
    }

    if ((regs.depth_control.depth_enable && regs.depth_buffer.DepthValid()) ||
        (regs.depth_control.stencil_enable && regs.depth_buffer.StencilValid())) {
        const auto htile_address = regs.depth_htile_data_base.GetAddress();
        const auto& hint = liverpool->last_db_extent;
        auto& [image_id, desc] = db_desc;
        std::construct_at(&desc, regs.depth_buffer, regs.depth_view, regs.depth_control,
                          htile_address, hint);
        image_id = bound_images.emplace_back(texture_cache.FindImage(desc));
        auto& image = texture_cache.GetImage(image_id);
        image.binding.is_target = 1u;
    } else {
        db_desc.image_id = {};
    }
}

static std::pair<u32, u32> GetDrawOffsets(const AmdGpu::Regs& regs, const Shader::Info& info,
                                          const Shader::Gcn::FetchShaderData& fetch_shader) {
    u32 vertex_offset = regs.index_offset;
    u32 instance_offset = 0;
    if (!fetch_shader.Empty()) {
        if (vertex_offset == 0 && fetch_shader.vertex_offset_sgpr != -1) {
            vertex_offset = info.user_data[fetch_shader.vertex_offset_sgpr];
        }
        if (fetch_shader.instance_offset_sgpr != -1) {
            instance_offset = info.user_data[fetch_shader.instance_offset_sgpr];
        }
    }
    return {vertex_offset, instance_offset};
}

void Rasterizer::EliminateFastClear() {
    auto& col_buf = liverpool->regs.color_buffers[0];
    if (!col_buf || !col_buf.info.fast_clear) {
        return;
    }
    VideoCore::TextureCache::ImageDesc desc(col_buf, liverpool->last_cb_extent[0]);
    const auto image_id = texture_cache.FindImage(desc);
    const auto& image_view = texture_cache.FindRenderTarget(image_id, desc);
    if (!texture_cache.IsMetaCleared(col_buf.CmaskAddress(), col_buf.view.slice_start)) {
        return;
    }
    for (u32 slice = col_buf.view.slice_start; slice <= col_buf.view.slice_max; ++slice) {
        texture_cache.TouchMeta(col_buf.CmaskAddress(), slice, false);
    }
    auto& image = texture_cache.GetImage(image_id);
    const auto clear_value = LiverpoolToVK::ColorBufferClearValue(col_buf);

    ScopeMarkerBegin(fmt::format("EliminateFastClear:MRT={:#x}:M={:#x}", col_buf.Address(),
                                 col_buf.CmaskAddress()));
    runtime.ClearImage(&image, desc.view_info.range, clear_value);
    ScopeMarkerEnd();
}

// Temporary render statistics, printed every few seconds.
struct DrawStats {
    u32 filtered, no_pipeline, bind_failed, drawn, indirect, dispatch, verts_max, mirrors, frames;
    u64 ns_pipeline{}, ns_bind{}, ns_draw_total{}, ns_dispatch_total{}, ns_mirror{};
    u32 flushes{};
    u32 flush_source[6]{};
    // Completion signals (end of sequence, release, fence) that passed while compute results
    // still waited for their copy back, and how many outputs waited in all.
    u32 unflushed_signal[6]{};
    u32 forced_flushes{};
    u32 deferred_waits{};
    u64 unflushed_outputs{};
    u32 same_pipeline{}, same_inputs{};
    u64 ns_stage[8]{};
    u64 ns_bind_buffers{}, ns_bind_textures{};
    u64 tsc_stage3{};
    // Direct draws by stage: filters, pipeline, render state, resources, begin, vertex and
    // index buffers, descriptors, dynamic state, scheduler begin, draw, reset.
    u64 ns_direct[11]{};
    u64 tsc_reset{};
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    u64 last_tsc = __rdtsc();
    void Tick() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(3)) {
            return;
        }
        // Ticks per nanosecond over the interval that just ended.
        const u64 tsc_now = __rdtsc();
        const double scale =
            double(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count()) /
            double(tsc_now - last_tsc);
        last_tsc = tsc_now;
        for (u64* value : {&ns_pipeline, &ns_bind, &ns_draw_total, &ns_dispatch_total, &ns_mirror,
                           &ns_bind_buffers, &ns_bind_textures, &ns_stage[0], &ns_stage[1],
                           &ns_stage[2], &ns_stage[4], &ns_stage[5], &ns_stage[6], &ns_stage[7],
                           &tsc_stage3}) {
            *value = u64(double(*value) * scale);
        }
        for (u64& value : ns_direct) {
            value = u64(double(value) * scale);
        }
        tsc_reset = u64(double(tsc_reset) * scale);
        last = now;
#ifdef _WIN32
        {
            // CPU time of this (the GPU command) thread and of the whole process over the
            // interval: tells a saturated render thread from one waiting on the game.
            static u64 last_thread = 0, last_process = 0;
            const auto total = [](HANDLE handle, bool thread) {
                FILETIME creation, exit, kernel, user;
                (thread ? GetThreadTimes : GetProcessTimes)(handle, &creation, &exit, &kernel, &user);
                const auto ticks = [](const FILETIME& t) {
                    return (u64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
                };
                return (ticks(kernel) + ticks(user)) / 10000;
            };
            const u64 thread_ms = total(GetCurrentThread(), true);
            const u64 process_ms = total(GetCurrentProcess(), false);
            LOG_ERROR(Render, "cpu/3s ms: render thread {} process {}", thread_ms - last_thread,
                      process_ms - last_process);
            LOG_ERROR(Render, "wait/3s ms: stream {} fault {} all {}",
                      Common::g_wait_us[0].exchange(0) / 1000,
                      Common::g_wait_us[1].exchange(0) / 1000,
                      Common::g_wait_us[2].exchange(0) / 1000);
            LOG_ERROR(Render, "shadow/3s: frames {} sun drawn {} skipped {} local drawn {} skipped {} clears {}",
                      g_shadow_maps.frame - g_shadow_maps.logged_frame, g_shadow_maps.seen[0][0],
                      g_shadow_maps.seen[0][1], g_shadow_maps.seen[1][0], g_shadow_maps.seen[1][1],
                      g_shadow_maps.clears);
            g_shadow_maps.logged_frame = g_shadow_maps.frame;
            std::memset(g_shadow_maps.seen, 0, sizeof(g_shadow_maps.seen));
            g_shadow_maps.clears = 0;
            if (g_pass_stat_on) {
                LOG_ERROR(Render, "sunsizes/3s: <100 {} <300 {} <1000 {} <3000 {} <10000 {} more {}",
                          g_shadow_maps.sizes[0], g_shadow_maps.sizes[1], g_shadow_maps.sizes[2],
                          g_shadow_maps.sizes[3], g_shadow_maps.sizes[4], g_shadow_maps.sizes[5]);
                std::memset(g_shadow_maps.sizes, 0, sizeof(g_shadow_maps.sizes));
            }
            if (g_pass_stat_on) {
                std::ranges::sort(g_pass_stat, std::greater{}, &PassStat::draws);
                for (size_t i = 0; i < std::min<size_t>(g_pass_stat.size(), 24); ++i) {
                    const auto& e = g_pass_stat[i];
                    LOG_ERROR(Render, "pass/3s: draws {} indices {} color {:#x} {}x{} depth {:#x} {}x{} vp {},{} w{}",
                              e.draws, e.indices, e.color, e.cw, e.ch, e.depth, e.dw, e.dh, e.vx,
                              e.vy, e.vw);
                    LOG_ERROR(Render, "pass/3s:    up to 36 indices {} up to 300 {} same pipeline {} "
                                      "depth write {} depth func {} vertex shaders {}{}",
                              e.tiny, e.small, e.same_pipeline, e.depth_write, e.depth_func,
                              e.vs_count, e.vs_count == 8 ? "+" : "");
                }
                g_pass_stat.clear();
                std::vector<std::pair<std::pair<u64, u64>, PairStat>> pairs(g_pair_stat.begin(),
                                                                            g_pair_stat.end());
                std::ranges::sort(pairs, [](const auto& a, const auto& b) {
                    return a.second.draws > b.second.draws;
                });
                for (size_t i = 0; i < std::min<size_t>(pairs.size(), 16); ++i) {
                    const auto& [key, e] = pairs[i];
                    LOG_ERROR(Render, "pair/3s: draws {} indices {} up to 36 indices {} vs {:#x} "
                                      "fs {:#x} (of {} pairs)",
                              e.draws, e.indices, e.tiny, key.first, key.second, pairs.size());
                }
                g_pair_stat.clear();
                for (const auto& [mask, n] : g_change_stat) {
                    LOG_ERROR(Render, "change/3s: mask {:#x} draws {}", mask, n);
                }
                g_change_stat.clear();
            }
            last_thread = thread_ms;
            last_process = process_ms;
        }
#endif
        LOG_ERROR(Render, "stats/3s: drawn {} indirect {} dispatch {} filtered {} no_pipeline {} "
                          "bind_failed {} max_indices {} mirrors {} submits {}",
                  drawn, indirect, dispatch, filtered, no_pipeline, bind_failed, verts_max, mirrors,
                  frames);
        LOG_ERROR(Render, "time/3s ms: pipeline {} bind {} draws {} dispatches {} mirror {} "
                          "flushes {}",
                  ns_pipeline / 1000000, ns_bind / 1000000, ns_draw_total / 1000000,
                  ns_dispatch_total / 1000000, ns_mirror / 1000000, flushes);
        flushes = 0;
        LOG_ERROR(Render, "flush sources: eop {} submit {} immediate {}", flush_source[1],
                  flush_source[3], flush_source[5]);
        LOG_ERROR(Render, "signals with results pending: eos {} release {} fence {} outputs {}, "
                          "flushes forced for argument windows {}",
                  unflushed_signal[0], unflushed_signal[2], unflushed_signal[4],
                  unflushed_outputs, forced_flushes);
        forced_flushes = 0;
        LOG_ERROR(Render, "deferred labels: blocking completions {}", deferred_waits);
        deferred_waits = 0;
        for (auto& v : unflushed_signal) {
            v = 0;
        }
        unflushed_outputs = 0;
        for (auto& v : flush_source) {
            v = 0;
        }
        LOG_ERROR(Render, "indirect stages ms: prepare {} begin {} vertex {} args {} "
                          "descriptors {} dynstate {} sched-begin {} draw+reset {}",
                  ns_stage[0] / 1000000, ns_stage[1] / 1000000, ns_stage[2] / 1000000,
                  tsc_stage3 / 1000000, ns_stage[4] / 1000000, ns_stage[5] / 1000000,
                  ns_stage[6] / 1000000, ns_stage[7] / 1000000);
        LOG_ERROR(Render, "bind split ms: buffers {} textures {} repeated {}",
                  ns_bind_buffers / 1000000, ns_bind_textures / 1000000, same_inputs);
        same_pipeline = same_inputs = 0;
        LOG_ERROR(Render, "direct stages ms: filters {} pipeline {} state {} resources {} begin {} "
                          "vertex {} index {} descriptors {} dynstate {} sched-begin {} draw {} "
                          "reset {}",
                  ns_direct[0] / 1000000, ns_direct[1] / 1000000, ns_direct[2] / 1000000,
                  ns_direct[3] / 1000000, ns_direct[4] / 1000000, ns_direct[5] / 1000000,
                  ns_direct[10] / 1000000, ns_direct[6] / 1000000, ns_direct[7] / 1000000,
                  ns_direct[8] / 1000000, ns_direct[9] / 1000000, tsc_reset / 1000000);
        for (u64& value : ns_direct) {
            value = 0;
        }
        tsc_reset = 0;
        ns_bind_buffers = ns_bind_textures = 0;
        for (auto& v : ns_stage) {
            v = 0;
        }
        tsc_stage3 = 0;
        ns_pipeline = ns_bind = ns_draw_total = ns_dispatch_total = ns_mirror = 0;
        filtered = no_pipeline = bind_failed = drawn = indirect = dispatch = verts_max = mirrors = frames = 0;
    }
};
static DrawStats g_stats;

// Temporary: SOTC_BISECT_AT=<seconds> then withholds one compute shader's mirror at a time for
// a few frames and logs the indirect draw count, to find the outputs the game reads on the CPU.
struct MirrorBisect {
    std::vector<u64> hashes;
    size_t index{};
    u64 frame{};       // frames since the current step began
    u64 draws{};       // indirect draws in the measured frames of the step
    u64 total_draws{}; // indirect draws this frame
    bool active{};
    u64 Skipped() const {
        return active && index < hashes.size() && frame < 5 ? hashes[index] : 0;
    }
};
static MirrorBisect g_bisect;

// Set when a frame submits far more draws than the game ever intends, which is what stale
// culling results look like: some other shader's output is being read back, so mirror them all.
static bool g_mirror_everything = false;

// Accumulates time-stamp counter ticks (a clock query per scope is measurable at this call
// rate); DrawStats::Tick scales them to nanoseconds.
struct ScopedNs {
    u64& sink;
    u64 start = __rdtsc();
    ~ScopedNs() {
        sink += __rdtsc() - start;
    }
};
static bool CoversViewBlock(VAddr addr, u64 size);

// Debug aid: SOTC_DRAWLOG_AT=<seconds> logs every draw for 50 ms.
static bool DrawLogActive() {
    static const char* at = std::getenv("SOTC_DRAWLOG_AT");
    if (!at) {
        return false;
    }
    static const auto start = std::chrono::steady_clock::now();
    const auto t = std::chrono::steady_clock::now() - start;
    return t >= std::chrono::seconds(std::atoi(at)) &&
           t < std::chrono::seconds(std::atoi(at)) + std::chrono::milliseconds(100);
}

// A draw that writes no colour, depth or stencil and has no shader side effects can only feed
// occlusion queries, and those are answered without rendering. Shadow of the Colossus issues
// over a thousand such bounding box draws per frame.
static bool DrawHasNoEffect(const GraphicsPipeline* pipeline, const AmdGpu::Regs& regs) {
    if (pipeline->GetGraphicsKey().mrt_mask != 0) {
        return false;
    }
    const auto& depth = regs.depth_control;
    if ((depth.depth_enable && depth.depth_write_enable) || depth.stencil_enable) {
        return false;
    }
    for (const auto* stage : pipeline->GetStages()) {
        if (!stage) {
            continue;
        }
        const auto written = [](const auto& desc) { return desc.is_written; };
        if (std::ranges::any_of(stage->buffers, written) ||
            std::ranges::any_of(stage->images, written)) {
            return false;
        }
    }
    return true;
}

// Hands recorded work to the GPU during the frame, so that it executes while later commands are
// still being recorded instead of all at once when the frame ends.
static void SubmitPeriodically(Scheduler& scheduler) {
    static const u32 interval = [] {
        const char* value = std::getenv("SOTC_SUBMIT_EVERY");
        return value ? u32(std::atoi(value)) : 512u;
    }();
    static u32 pending = 0;
    if (interval && ++pending >= interval) {
        pending = 0;
        scheduler.Flush();
    }
}

void Rasterizer::Draw(bool is_indexed, u32 index_offset) {
    RENDERER_TRACE;
    Common::FreezeTimer freeze_timer{3};

    scheduler.PopPendingOperations();
    SubmitPeriodically(scheduler);
    g_stats.Tick();
    VideoCore::StartThreadSampler();
    const u32 change_mask = std::exchange(liverpool->change_mask_since_draw, 0u);
    u64 stage_start = __rdtsc();
    const auto stage_done = [&stage_start](u32 stage) {
        const u64 now = __rdtsc();
        g_stats.ns_direct[stage] += now - stage_start;
        stage_start = now;
    };

    if (!FilterDraw()) {
        ++g_stats.filtered;
        return;
    }

    const auto& regs = liverpool->regs;
    if (g_shadow_maps.enabled && g_shadow_maps.Skip(regs.depth_buffer, regs.depth_render_control.depth_clear_enable,
                           u64(regs.num_indices) * regs.num_instances.NumInstances())) {
        ++g_stats.filtered;
        return;
    }
    if (!g_skip_depth.empty() &&
        std::ranges::find(g_skip_depth, regs.depth_buffer.DepthAddress()) != g_skip_depth.end()) {
        ++g_stats.filtered;
        return;
    }
    stage_done(0);
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline();
    stage_done(1);
    if (!pipeline) {
        ++g_stats.no_pipeline;
        return;
    }
    if (DrawHasNoEffect(pipeline, regs)) {
        ++g_stats.filtered;
        return;
    }
    {
        const u64 fs_hash = pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash;
        if (VideoCore::PassFilter::skipped_count.load(std::memory_order_relaxed) != 0 &&
            VideoCore::PassFilter::IsSkipped(fs_hash)) {
            ++g_stats.filtered;
            return;
        }
        // Test aid: script action "listfs" collects the fullscreen passes (3 or 4 vertices)
        // for a while and writes them, in first-use order, to fs_list.txt.
        if (VideoCore::PassFilter::listing_frames.load(std::memory_order_relaxed) > 0 &&
            regs.num_indices <= 4) {
            static std::vector<std::pair<u64, u32>> seen;
            auto it = std::ranges::find(seen, fs_hash, &std::pair<u64, u32>::first);
            if (it == seen.end()) {
                seen.emplace_back(fs_hash, 1);
            } else {
                ++it->second;
            }
            if (VideoCore::PassFilter::listing_frames.fetch_sub(1) == 1) {
                if (std::FILE* file = std::fopen("fs_list.txt", "w")) {
                    for (const auto& [hash, count] : seen) {
                        std::fprintf(file, "%llx %u\n", static_cast<unsigned long long>(hash),
                                     count);
                    }
                    std::fclose(file);
                }
                seen.clear();
            }
        }
    }
    if (g_pass_stat_on && regs.blend_control[0].enable) {
        // Test aid: each shader pair that draws with blending, once, with a draw count, so
        // effects (fire, smoke, glows) can be told apart and skipped with "skipfs=".
        static std::map<std::pair<u64, u64>, u32> blended;
        const u64 vs_hash = pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash;
        const u64 fs_hash = pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash;
        if (++blended[{vs_hash, fs_hash}] == 1) {
            const auto& blend = regs.blend_control[0];
            LOG_WARNING(Render, "blended pair vs {:#x} fs {:#x} src {} dst {} prim {} indices {}",
                        vs_hash, fs_hash, u32(blend.color_src_factor),
                        u32(blend.color_dst_factor), u32(regs.primitive_type), regs.num_indices);
        }
    }
    if (g_pass_stat_on && regs.depth_buffer.DepthAddress() == g_shadow_maps.sun) {
        // Test aid: sizes of the draws into the sun's shadow map.
        const u64 n = u64(regs.num_indices) * regs.num_instances.NumInstances();
        ++g_shadow_maps.sizes[n < 100 ? 0 : n < 300 ? 1 : n < 1000 ? 2 : n < 3000 ? 3 : n < 10000 ? 4 : 5];
    }
    if (g_pass_stat_on) {
        // Test aid: draws per render-target combination, logged with the other statistics.
        const bool color = pipeline->GetStage(Shader::SwStage::Fragment).mrt_mask != 0;
        const auto& cb = regs.color_buffers[0];
        const u64 color_addr = color ? cb.Address() : 0;
        const u64 depth_addr = regs.depth_buffer.DepthValid() ? regs.depth_buffer.DepthAddress() : 0;
        const auto& vp = regs.viewports[0];
        const int vw = int(vp.xscale * 2), vx = int(regs.depth_view.slice_start),
                  vy = int(vp.yoffset - std::abs(vp.yscale));
        auto it = std::ranges::find_if(g_pass_stat, [&](const PassStat& e) {
            return e.color == color_addr && e.depth == depth_addr && e.vx == vx && e.vy == vy &&
                   e.vw == vw;
        });
        if (it == g_pass_stat.end()) {
            g_pass_stat.push_back({color_addr, depth_addr, color ? cb.Pitch() : 0,
                                   color ? cb.Height() : 0, regs.depth_buffer.Pitch(),
                                   regs.depth_buffer.Height(), 0, 0, vx, vy, vw});
            it = std::prev(g_pass_stat.end());
        }
        ++it->draws;
        const u64 n = u64(regs.num_indices) * regs.num_instances.NumInstances();
        it->indices += n;
        it->tiny += n <= 36;
        it->small += n <= 300;
        static const GraphicsPipeline* previous = nullptr;
        it->same_pipeline += pipeline == previous;
        if (pipeline == previous) {
            ++g_change_stat[change_mask];
        }
        previous = pipeline;
        it->depth_write += regs.depth_control.depth_write_enable;
        {
            auto& pair = g_pair_stat[{pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                                      pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash}];
            ++pair.draws;
            pair.indices += n;
            pair.tiny += n <= 36;
        }
        it->depth_func = u32(regs.depth_control.depth_func);
        const u64 vs_hash = pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash;
        if (it->vs_count < 8 &&
            std::find(it->vs_seen, it->vs_seen + it->vs_count, vs_hash) == it->vs_seen + it->vs_count) {
            it->vs_seen[it->vs_count++] = vs_hash;
        }
    }
    VideoCore::OpTrace::Add('D', pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                            pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash,
                            regs.num_indices, regs.num_instances.NumInstances());
    {
        // Debug aid: SOTC_SKIP_VS=<hex hash> drops draws using that vertex shader, to see what
        // it renders.
        static const u64 skip_vs = [] {
            const char* v = std::getenv("SOTC_SKIP_VS");
            return v ? std::strtoull(v, nullptr, 16) : 0ULL;
        }();
        if (skip_vs && pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash == skip_vs) {
            return;
        }
    }
    // The game rasterizes over a thousand occluder boxes per frame into a depth buffer its
    // culling shaders test against. Leaving that buffer empty only makes culling less strict,
    // and the per-draw cost here far outweighs what the extra visible objects cost a PC GPU.
    // SOTC_OCCLUDERS=1 renders them again.
    // ponytail: recognised by shape (36 indices, depth only); match the occluder depth target
    // instead if another depth-only box pass ever turns up.
    static const bool skip_occluders =
        std::getenv("SOTC_OCCLUDERS") == nullptr &&
        Common::ElfInfo::Instance().GameSerial() == "CUSA08809";
    if (skip_occluders && regs.num_indices == 36 && pipeline->GetGraphicsKey().mrt_mask == 0) {
        ++g_stats.filtered;
        return;
    }

    stage_start = __rdtsc();
    PrepareRenderState(pipeline);
    stage_done(2);
    const bool bound = BindResources(pipeline);
    stage_done(3);
    if (!bound) {
        ++g_stats.bind_failed;
        return;
    }
    if (Tlg::Ui::OnDraw(g_tlg_draw_tags)) {
        ++g_stats.filtered;
        return;
    }
    ++g_stats.drawn;
    g_stats.verts_max = std::max<u32>(g_stats.verts_max, regs.num_indices);
    if (DrawLogActive()) {
        const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
        std::string bufs;
        for (const auto& desc : vs.buffers) {
            if (desc.buffer_type == Shader::BufferType::Guest) {
                const auto sharp = desc.GetSharp(vs);
                bufs += fmt::format("[{:#x} st{} n{:#x}]", u64(sharp.base_address),
                                    sharp.GetStride(), u32(sharp.num_records));
            }
        }
        LOG_ERROR(Render, "draw vs {:#x} fs {:#x} prim {} n {} inst {} idx {} cb0 {:#x} mrt {:#x} "
                          "vsbuf {}",
                  vs.pgm_hash, 0, u32(regs.primitive_type), regs.num_indices,
                  regs.num_instances.NumInstances(), is_indexed,
                  u64(regs.color_buffers[0].Address()), pipeline->GetGraphicsKey().mrt_mask, bufs);
        LOG_ERROR(Render, "   dstate mrt {:#x} cmode {} depth {:#x} dctl {:#x} vp {} {}",
                  pipeline->GetGraphicsKey().mrt_mask, u32(regs.color_control.mode),
                  u64(regs.depth_buffer.DepthAddress()),
                  *reinterpret_cast<const u32*>(&regs.depth_control), regs.viewports[0].xscale,
                  regs.viewports[0].yscale);
    }
    stage_start = __rdtsc();
    const auto state = BeginRendering(pipeline);
    stage_done(4);

    BindVertexBuffers(pipeline);
    stage_done(5);
    if (is_indexed) {
        BindIndexBuffer(index_offset);
    }
    stage_done(10);

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    pipeline->BindResources(set_writes, push_data);
    stage_done(6);
    UpdateDynamicState(pipeline, is_indexed);
    stage_done(7);
    scheduler.BeginRendering(state);
    stage_done(8);

    const auto& vs_info = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto& fetch_shader = pipeline->GetFetchShader();
    const auto [vertex_offset, instance_offset] = GetDrawOffsets(regs, vs_info, fetch_shader);

    auto& cmdbuf = scheduler.Stream();
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Handle());

    if (is_indexed) {
        cmdbuf.drawIndexed(regs.num_indices, regs.num_instances.NumInstances(), 0,
                           s32(vertex_offset), instance_offset);
    } else {
        cmdbuf.draw(regs.num_indices, regs.num_instances.NumInstances(), vertex_offset,
                    instance_offset);
    }
    DebugState.IncDrawCall();
    stage_done(9);

    ResetBindings(false);
    g_stats.tsc_reset += __rdtsc() - stage_start;
}

// Indirect draw arguments read from guest memory and checked (see DrawIndirect); empty when
// the GPU is to read the argument buffer itself.
static boost::container::small_vector<std::array<u32, 5>, 4> checked_args;

template <typename Stream>
static void EmitCheckedDraws(Stream& cmdbuf, bool is_indexed) {
    for (const auto& args : checked_args) {
        if (args[0] == 0 || args[1] == 0) {
            continue;
        }
        if (is_indexed) {
            cmdbuf.drawIndexed(args[0], args[1], args[2], s32(args[3]), args[4]);
        } else {
            cmdbuf.draw(args[0], args[1], args[2], args[3]);
        }
    }
}

// Compute outputs that hold the argument slots of indirect draws. The game reads the counts in
// them on the CPU as soon as it is told the compute work is done (a crash in long fire fights
// when they arrive later: a negative count indexes an array), and they are larger than the
// copy-back limit. Such an output is learned when a draw first takes its arguments from it;
// from then on it is copied back whatever its size, at the next completion signal.
// I3_NO_ARG_WINDOWS=1 switches this off.
struct ArgWindows {
    std::vector<std::pair<VAddr, u32>> large_outputs; // seen in dispatches, above the limit
    std::vector<VAddr> learned;                       // bases of outputs draws took arguments from
    bool urgent{};
    bool Enabled() const {
        static const bool on = Common::ElfInfo::Instance().GameSerial() == "CUSA00004" &&
                               std::getenv("I3_NO_ARG_WINDOWS") == nullptr;
        return on;
    }
    void NoteOutput(VAddr base, u32 size) {
        for (const auto& [b, n] : large_outputs) {
            if (b == base && n == size) {
                return;
            }
        }
        if (large_outputs.size() < 4096) {
            large_outputs.emplace_back(base, size);
        }
    }
    void NoteArgument(VAddr address) {
        for (const auto& [base, size] : large_outputs) {
            if (address >= base && address < base + size &&
                std::ranges::find(learned, base) == learned.end()) {
                learned.push_back(base);
                LOG_INFO(Render, "argument window learned: {:#x} size {:#x} ({} so far)", base,
                         size, learned.size());
            }
        }
    }
    bool IsLearned(VAddr base) const {
        return std::ranges::find(learned, base) != learned.end();
    }
};
static ArgWindows g_arg_windows;

// Last frame's results of the small compute outputs the game reads on the CPU (see
// DispatchDirect), by shader and place in the frame.
struct Predict {
    std::unordered_map<u64, std::vector<u8>> last;
    std::unordered_map<u64, u32> ordinal;
    u64 written{};
};
static Predict g_predict;
// Outputs of those shaders waiting for the next completion signal.
static std::vector<std::pair<VAddr, u32>> g_urgent_mirrors;

// Test aid: which compute shader wrote a piece of guest memory last (outputs of every dispatch,
// newest first on lookup). Used to name the writer when the game reads GPU-written memory.
struct CsOutput {
    u64 hash;
    VAddr base;
    u32 size;
};
static CsOutput g_cs_outputs[8192];
static std::atomic<u32> g_cs_output_count{0};
static void NoteCsOutput(u64 hash, VAddr base, u32 size) {
    const u32 total = g_cs_output_count.load(std::memory_order_relaxed);
    for (u32 i = 0; i < std::min<u32>(total, 64); ++i) {
        const auto& e = g_cs_outputs[(total - 1 - i) % 8192];
        if (e.hash == hash && e.base == base && e.size == size) {
            return;
        }
    }
    g_cs_outputs[total % 8192] = {hash, base, size};
    g_cs_output_count.store(total + 1, std::memory_order_relaxed);
}

static bool is_infamous_args_mirror() {
    return Common::ElfInfo::Instance().GameSerial() == "CUSA00004" &&
           std::getenv("I3_NO_ARG_MIRROR") == nullptr;
}

void Rasterizer::DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 stride,
                              u32 max_count, VAddr count_address, u16 vertex_sgpr_offset,
                              u16 instance_sgpr_offset) {
    RENDERER_TRACE;
    ++g_stats.indirect;
    ++g_bisect.total_draws;
    VideoCore::OpTrace::Add('I', arg_address, offset, max_count, count_address);
    {
        // inFAMOUS Second Son now and then issues an indirect draw whose argument slot holds
        // unrelated float data (instance count in the billions), which hangs the GPU until the
        // driver resets the device. No real draw comes near these limits, so drop such draws.
        // SotC is left alone: its arguments are written on the GPU and its frame is draw-bound.
        // Every slot of a multi draw is checked and the draws are issued from the checked
        // values instead of letting the GPU read the slot: what the GPU would read can differ
        // from what was checked.
        static const bool check_args = Common::ElfInfo::Instance().GameSerial() != "CUSA08809";
        checked_args.clear();
        if (check_args) {
            constexpr u32 max_elements = 0x400000;
            constexpr u32 max_instances = 0x10000;
            constexpr u32 max_slots = 256;
            // The draws are issued from the checked values whether or not a compute shader
            // wrote the slot. Trusting the GPU's own copy of compute-written slots lost the
            // device within half a minute (I3_TRUST_GPU_ARGS=1 reproduces it): that copy can
            // hold garbage while guest memory, which receives compute results a frame later,
            // holds sane counts. The price is particle counts that are a frame old.
            static const bool trust_gpu_args = std::getenv("I3_TRUST_GPU_ARGS") != nullptr;
            const bool gpu_written = buffer_cache.IsRegionGpuModified(arg_address + offset,
                                                                      u64(stride) * max_count);
            const u32 slots = gpu_written && trust_gpu_args
                                  ? 0
                                  : count_address == 0 ? std::min(max_count, max_slots) : 1;
            const bool direct = count_address == 0 && max_count <= max_slots && stride >= 16 &&
                                !(gpu_written && trust_gpu_args);
            // Slots a compute shader writes reach guest memory only through the copy back, and
            // that leaves out outputs over its size limit: inFAMOUS declares each slot as a
            // 128 KB window. Such a slot was then read from whatever the game's heap held
            // there, zeros (the draw silently missing) or old floats (dropped draws, and now
            // and then a count that passed the check). The slots themselves are copied back,
            // just their bytes. I3_NO_ARG_MIRROR=1 turns this off.
            static const bool mirror_args = is_infamous_args_mirror();
            if (gpu_written && g_arg_windows.Enabled()) {
                g_arg_windows.NoteArgument(arg_address + offset);
            }
            if (mirror_args && gpu_written && slots != 0) {
                const u32 bytes = stride * slots;
                pending_mirrors.emplace_back(arg_address + offset, bytes);
                buffer_cache.AddPendingMirror(arg_address + offset, bytes);
            }
            for (u32 slot = 0; slot < slots; ++slot) {
                std::array<u32, 5> args{};
                memory->CopySparseMemory(arg_address + offset + u64(slot) * stride,
                                         reinterpret_cast<u8*>(args.data()),
                                         std::min<u64>(stride, sizeof(args)));
                if (args[0] > max_elements || args[1] > max_instances) {
                    LOG_WARNING(Render,
                                "Dropped indirect draw at {:#x}: count {:#x}, instances {:#x}, "
                                "gpu-written {}, slots {}, stride {}, indexed {}",
                                arg_address + offset + u64(slot) * stride, args[0], args[1],
                                buffer_cache.IsRegionGpuModified(arg_address + offset,
                                                                 u64(stride) * max_count),
                                max_count, stride, is_indexed);
                    if (g_pass_stat_on) {
                        LOG_WARNING(Render, "   bad args base {:#x} off {:#x} vs {:#x} fs {:#x}",
                                    arg_address, offset,
                                    liverpool->regs.vs_program.Address<u64>(),
                                    liverpool->regs.ps_program.Address<u64>());
                        const u64 at = arg_address + offset + u64(slot) * stride;
                        auto it = g_cs_writers.upper_bound(at);
                        if (it != g_cs_writers.begin() && (--it, at < it->first + it->second.size)) {
                            u32 raw[8]{};
                            memory->CopySparseMemory(at, reinterpret_cast<u8*>(raw), sizeof(raw));
                            LOG_WARNING(Render, "   writer cs {:#x} base {:#x} size {} groups {}x{}x{} "
                                                "off {} guest {:08x} {:08x} {:08x} {:08x} {:08x} {:08x}",
                                        it->second.hash, it->first, it->second.size, it->second.x,
                                        it->second.y, it->second.z, at - it->first, raw[0], raw[1],
                                        raw[2], raw[3], raw[4], raw[5]);
                        } else {
                            LOG_WARNING(Render, "   writer cs unknown");
                        }
                    }
                    ++g_stats.filtered;
                    checked_args.clear();
                    return;
                }
                if (g_pass_stat_on) {
                    // Test aid: a sample of accepted argument slots, to compare with dropped ones.
                    static std::set<u64> seen;
                    const u64 at = arg_address + offset + u64(slot) * stride;
                    if (seen.size() < 40 && seen.insert(at).second) {
                        LOG_WARNING(Render, "   good args at {:#x} base {:#x} off {:#x}: {:08x} {:08x} "
                                            "{:08x} {:08x} {:08x} gpu-written {} indexed {}",
                                    at, arg_address, offset, args[0], args[1], args[2], args[3],
                                    args[4], gpu_written, is_indexed);
                        auto it = g_cs_writers.upper_bound(at);
                        if (it != g_cs_writers.begin() && (--it, at < it->first + it->second.size)) {
                            LOG_WARNING(Render, "      writer cs {:#x} base {:#x} size {}",
                                        it->second.hash, it->first, it->second.size);
                        }
                    }
                }
                if (direct) {
                    checked_args.push_back(args);
                }
            }
        }
    }
    ScopedNs total_timer{g_stats.ns_draw_total};
    Common::FreezeTimer freeze_timer{3};

    scheduler.PopPendingOperations();
    SubmitPeriodically(scheduler);

    // The engine issues long runs of indirect draws that differ only in their argument slot.
    // When no other command ran since the previous one and its pipeline, bindings and render
    // pass are still current on the command buffer, only the draw itself has to be recorded.
    struct RepeatDraw {
        bool valid;
        bool is_indexed;
        u32 stride;
        u16 vertex_sgpr_offset;
        u16 instance_sgpr_offset;
        vk::CommandBuffer cmdbuf;
    };
    static RepeatDraw repeat{};
    const bool can_repeat =
        repeat.valid && !liverpool->state_changed_since_draw && count_address == 0 &&
        repeat.is_indexed == is_indexed && repeat.stride == stride &&
        repeat.vertex_sgpr_offset == vertex_sgpr_offset &&
        repeat.instance_sgpr_offset == instance_sgpr_offset && scheduler.IsRendering() &&
        repeat.cmdbuf == scheduler.RawCommandBuffer();
    repeat.valid = false;
    if (can_repeat) {
        const auto size = stride * max_count;
        const auto [buffer, base] = buffer_cache.ObtainBuffer(arg_address + offset, size, false);
        // Obtaining the arguments may have uploaded data, which ends the render pass.
        if (scheduler.IsRendering() && repeat.cmdbuf == scheduler.RawCommandBuffer() &&
            !runtime.IsBufferAccessed(buffer, base, size)) {
            if (is_indexed) {
                if (!checked_args.empty()) {
                    EmitCheckedDraws(scheduler.Stream(), true);
                } else {
                    scheduler.Stream().drawIndexedIndirect(buffer->Handle(), base, max_count,
                                                           stride);
                }
            } else {
                if (!checked_args.empty()) {
                    EmitCheckedDraws(scheduler.Stream(), false);
                } else {
                    scheduler.Stream().drawIndirect(buffer->Handle(), base, max_count, stride);
                }
            }
            DebugState.IncDrawCall();
            ++g_stats.same_inputs;
            repeat.valid = true;
            return;
        }
    }

    if (!FilterDraw()) {
        return;
    }

    const DrawIndirectParams params = {
        .vertex_sgpr_offset = vertex_sgpr_offset,
        .instance_sgpr_offset = instance_sgpr_offset,
    };
    const GraphicsPipeline* pipeline = [&] {
        ScopedNs timer{g_stats.ns_pipeline};
        return pipeline_cache.GetGraphicsPipeline(params);
    }();
    if (!pipeline) {
        return;
    }
    if (DrawHasNoEffect(pipeline, liverpool->regs)) {
        ++g_stats.filtered;
        return;
    }
    {
        // Test aids: the script action "skipfs=<hash>" also drops indirect draws with that
        // fragment shader, and I3_PASSSTAT logs each shader pair indirect draws use, once.
        const u64 fs_hash = pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash;
        if (VideoCore::PassFilter::skipped_count.load(std::memory_order_relaxed) != 0 &&
            VideoCore::PassFilter::IsSkipped(fs_hash)) {
            ++g_stats.filtered;
            return;
        }
        if (g_pass_stat_on) {
            static std::set<std::pair<u64, u64>> seen;
            const u64 vs_hash = pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash;
            if (seen.emplace(vs_hash, fs_hash).second) {
                const auto& blend = liverpool->regs.blend_control[0];
                LOG_WARNING(Render, "indirect pair vs {:#x} fs {:#x} indexed {} blend {} src {} dst {}",
                            vs_hash, fs_hash, is_indexed, u32(blend.enable),
                            u32(blend.color_src_factor),
                            u32(blend.color_dst_factor));
            }
        }
    }
    {
        // Debug aid: SOTC_SKIP_VS=<hex hash> drops draws using that vertex shader, to see what
        // it renders.
        static const u64 skip_vs = [] {
            const char* v = std::getenv("SOTC_SKIP_VS");
            return v ? std::strtoull(v, nullptr, 16) : 0ULL;
        }();
        if (skip_vs && pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash == skip_vs) {
            return;
        }
    }

    {
        ScopedNs t{g_stats.ns_stage[0]};
        PrepareRenderState(pipeline);
    }
    if (ScopedNs timer{g_stats.ns_bind}; !BindResources(pipeline)) {
        return;
    }
    const auto state = [&] {
        ScopedNs t{g_stats.ns_stage[1]};
        return BeginRendering(pipeline);
    }();

    if (DrawLogActive()) {
        const auto& regs = liverpool->regs;
        const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
        const auto& fs = pipeline->GetStage(Shader::SwStage::Fragment);
        std::string bufs;
        std::string inst;
        const u32* args = reinterpret_cast<const u32*>(arg_address + offset);
        for (const auto& desc : vs.buffers) {
            if (desc.buffer_type == Shader::BufferType::Guest) {
                const auto sharp = desc.GetSharp(vs);
                bufs += fmt::format("[{:#x} st{} n{:#x} gpu{}]", u64(sharp.base_address),
                                    sharp.GetStride(), u32(sharp.num_records),
                                    buffer_cache.IsRegionGpuModified(sharp.base_address,
                                                                     sharp.GetSize()));
                if (sharp.GetStride() == 128) {
                    // CPU-side view of the instance record this draw selects.
                    const u32 index = args[4] & 0xffff;
                    if (index < sharp.num_records &&
                        memory->IsValidMapping(sharp.base_address + index * 128ull, 128)) {
                        const float* rec =
                            reinterpret_cast<const float*>(sharp.base_address + index * 128ull);
                        for (u32 k = 0; k < 16; ++k) {
                            inst += fmt::format("{:.3g} ", rec[k]);
                        }
                    }
                }
            }
        }
        LOG_ERROR(Render, "indirect vs {:#x} fs {:#x} prim {} idx {} max {} stride {} count_addr "
                          "{:#x} gpu_args {} args {} {} {} {} {} sgpr {} {} cb0 {:#x} vsbuf {}",
                  vs.pgm_hash, fs.pgm_hash, u32(regs.primitive_type), is_indexed, max_count,
                  stride, count_address,
                  buffer_cache.IsRegionGpuModified(arg_address + offset, stride * max_count),
                  args[0], args[1], args[2], args[3], args[4], vertex_sgpr_offset,
                  instance_sgpr_offset, u64(regs.color_buffers[0].Address()), bufs);
        LOG_ERROR(Render, "   inst {}", inst);
        {
            // Textures the fragment shader samples and whether guest memory holds data for them.
            const auto& fsi = pipeline->GetStage(Shader::SwStage::Fragment);
            std::string tex;
            for (const auto& desc : fsi.images) {
                const auto tsharp = desc.GetSharp(fsi);
                const u64 taddr = tsharp.Address();
                u32 nonzero = 0;
                tex += fmt::format("[{:#x} {}x{} fmt{} nz{}]", taddr, tsharp.width + 1,
                                   tsharp.height + 1, u32(tsharp.data_format), nonzero);
            }
            LOG_ERROR(Render, "   tex fs {:#x} {}", fsi.pgm_hash, tex);
            if (fsi.pgm_hash == 0x972ea312) {
                const u64 f1 = u64(fsi.user_data[0]) | (u64(fsi.user_data[1]) << 32);
                if (memory->IsValidMapping(f1, 32)) {
                    const u64 mat = *reinterpret_cast<const u64*>(f1 + 24) & 0xffffffffffffULL;
                    if (memory->IsValidMapping(mat, 100 * 4)) {
                        const u32* m = reinterpret_cast<const u32*>(mat);
                        LOG_ERROR(Render, "   matblock root {:#x} mat {:#x} gpu {} t0 {:08x} {:08x} "
                                          "{:08x} {:08x} {:08x} {:08x} {:08x} {:08x} head {:08x} "
                                          "{:08x} {:08x} {:08x}",
                                  f1, mat, buffer_cache.IsRegionGpuModified(mat, 400), m[92], m[93],
                                  m[94], m[95], m[96], m[97], m[98], m[99], m[0], m[1], m[2], m[3]);
                    } else {
                        LOG_ERROR(Render, "   matblock root {:#x} mat {:#x} unmapped", f1, mat);
                    }
                }
            }
        }
        {
            // CPU view of the per-view constants (view-projection rows at dwords 32..47).
            const u64 p1 = u64(vs.user_data[0]) | (u64(vs.user_data[1]) << 32);
            if (memory->IsValidMapping(p1, 16)) {
                const u64 p2 = *reinterpret_cast<const u64*>(p1 + 8) & 0xffffffffffffULL;
                if (memory->IsValidMapping(p2, 82 * 4)) {
                    const float* c = reinterpret_cast<const float*>(p2);
                    LOG_ERROR(Render, "   srt p1 {:#x} p2 {:#x} gpu2 {} vp [{:.3g} {:.3g} {:.3g} "
                                      "{:.3g}] [{:.3g} {:.3g} {:.3g} {:.3g}] [{:.3g} {:.3g} {:.3g} "
                                      "{:.3g}] [{:.3g} {:.3g} {:.3g} {:.3g}]",
                              p1, p2, buffer_cache.IsRegionGpuModified(p2, 328), c[32], c[33],
                              c[34], c[35], c[36], c[37], c[38], c[39], c[40], c[41], c[42], c[43],
                              c[44], c[45], c[46], c[47]);
                }
            }
        }
        {
            const auto& vp = regs.viewports[0];
            const auto& sc = regs.screen_scissor;
            const auto& gs = regs.generic_scissor;
            LOG_ERROR(Render, "   state mrt {:#x} cmode {} mask {:#x} depth {:#x} dctl {:#x} "
                              "poly {:#x} clip {:#x} vpc {:#x} vp {} {} {} {} {} {} gsc {} {} {} "
                              "{} ssc {} {} {} {}",
                      pipeline->GetGraphicsKey().mrt_mask, u32(regs.color_control.mode),
                      regs.color_target_mask.raw, u64(regs.depth_buffer.DepthAddress()),
                      *reinterpret_cast<const u32*>(&regs.depth_control), *reinterpret_cast<const u32*>(&regs.polygon_control), *reinterpret_cast<const u32*>(&regs.clipper_control),
                      *reinterpret_cast<const u32*>(&regs.viewport_control), vp.xscale, vp.xoffset, vp.yscale, vp.yoffset,
                      vp.zscale, vp.zoffset, u32(gs.top_left_x), u32(gs.top_left_y),
                      u32(gs.bottom_right_x), u32(gs.bottom_right_y), s32(sc.top_left_x),
                      s32(sc.top_left_y), s32(sc.bottom_right_x), s32(sc.bottom_right_y));
        }
        static int compared = 0;
        if (compared < 3) {
            ++compared;
            for (const auto& desc : vs.buffers) {
                if (desc.buffer_type != Shader::BufferType::Guest) {
                    continue;
                }
                const auto sharp = desc.GetSharp(vs);
                if (memory->IsValidMapping(sharp.base_address, sharp.GetSize())) {
                    LOG_ERROR(Render, "   compare {:#x} size {:#x} mismatched bytes {}",
                              u64(sharp.base_address), sharp.GetSize(),
                              buffer_cache.DebugMismatch(sharp.base_address, sharp.GetSize()));
                }
            }
        }
    }

    {
        ScopedNs t{g_stats.ns_stage[2]};
        BindVertexBuffers(pipeline);
        if (is_indexed) {
            BindIndexBuffer();
        }
    }
    u64 stage3_start = __rdtsc();

    const auto size = stride * max_count;
    const auto [buffer, base] = buffer_cache.ObtainBuffer(arg_address + offset, size, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, base, size);
    bound_buffers.emplace_back(buffer, base, size, vk::AccessFlagBits2::eIndirectCommandRead);

    const VideoCore::Buffer* count_buffer;
    u64 count_offset;
    if (count_address != 0) {
        std::tie(count_buffer, count_offset) = buffer_cache.ObtainBuffer(count_address, 4, false);
        needs_barrier |= runtime.IsBufferAccessed(count_buffer, count_offset, 4);
        bound_buffers.emplace_back(count_buffer, count_offset, 4,
                                   vk::AccessFlagBits2::eIndirectCommandRead);
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    g_stats.tsc_stage3 += __rdtsc() - stage3_start;
    {
        ScopedNs t{g_stats.ns_stage[4]};
        pipeline->BindResources(set_writes, push_data);
    }
    {
        ScopedNs t{g_stats.ns_stage[5]};
        UpdateDynamicState(pipeline, is_indexed);
    }
    {
        ScopedNs t{g_stats.ns_stage[6]};
        scheduler.BeginRendering(state);
    }
    ScopedNs tail_timer{g_stats.ns_stage[7]};

    auto& cmdbuf = scheduler.Stream();
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Handle());

    if (is_indexed) {
        ASSERT(sizeof(VkDrawIndexedIndirectCommand) == stride);

        if (count_address != 0) {
            cmdbuf.drawIndexedIndirectCount(buffer->Handle(), base, count_buffer->Handle(),
                                            count_offset, max_count, stride);
        } else if (!checked_args.empty()) {
            EmitCheckedDraws(cmdbuf, true);
        } else {
            cmdbuf.drawIndexedIndirect(buffer->Handle(), base, max_count, stride);
        }
        DebugState.IncDrawCall();
    } else {
        ASSERT(sizeof(VkDrawIndirectCommand) == stride);

        if (count_address != 0) {
            cmdbuf.drawIndirectCount(buffer->Handle(), base, count_buffer->Handle(), count_offset,
                                     max_count, stride);
        } else if (!checked_args.empty()) {
            EmitCheckedDraws(cmdbuf, false);
        } else {
            cmdbuf.drawIndirect(buffer->Handle(), base, max_count, stride);
        }
        DebugState.IncDrawCall();
    }

    ResetBindings(false);
    repeat = {true,   is_indexed,          stride,
              vertex_sgpr_offset, instance_sgpr_offset, scheduler.RawCommandBuffer()};
    liverpool->state_changed_since_draw = false;
}

void Rasterizer::DispatchDirect() {
    RENDERER_TRACE;
    ++g_stats.dispatch;
    ScopedNs total_timer{g_stats.ns_dispatch_total};
    Common::FreezeTimer freeze_timer{4};

    scheduler.PopPendingOperations();
    SubmitPeriodically(scheduler);

    const auto& cs_program = liverpool->GetCsRegs();
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        return;
    }

    const auto& cs = pipeline->GetStage(Shader::SwStage::Compute);
    {
        // Debug aid: SOTC_SKIP_CS=<hex hash> drops dispatches of that compute shader.
        static const u64 skip_cs = [] {
            const char* v = std::getenv("SOTC_SKIP_CS");
            return v ? std::strtoull(v, nullptr, 16) : 0ULL;
        }();
        if (skip_cs && cs.pgm_hash == skip_cs) {
            return;
        }
        // Test aid: I3_SKIP_ALL_CS=1 drops every dispatch (cost measurement; breaks the picture).
        static const bool skip_all = std::getenv("I3_SKIP_ALL_CS") != nullptr;
        if (skip_all) {
            return;
        }
    }
    // The compute bookkeeping below (histograms, mirroring of small outputs into guest memory)
    // exists for SotC, whose CPU reads compute results; other titles skip its cost.
    static const bool is_sotc = Common::ElfInfo::Instance().GameSerial() == "CUSA08809";
    if (is_sotc || g_pass_stat_on) {
        // Temporary: which compute shaders dominate, with their dispatch sizes.
        static std::unordered_map<u64, std::pair<u32, u64>> histogram;
        static auto last = std::chrono::steady_clock::now();
        auto& entry = histogram[cs.pgm_hash];
        ++entry.first;
        entry.second += u64(cs_program.dim_x) * cs_program.dim_y * cs_program.dim_z;
        static u32 calls = 0;
        if (const auto now = (++calls & 0xFFF) ? last : std::chrono::steady_clock::now();
            now - last >= std::chrono::seconds(6)) {
            last = now;
            std::vector<std::pair<u64, std::pair<u32, u64>>> top(histogram.begin(),
                                                                 histogram.end());
            std::ranges::sort(top, [](auto& x, auto& y) { return x.second.first > y.second.first; });
            std::string line;
            for (size_t i = 0; i < std::min<size_t>(top.size(), 10); ++i) {
                line += fmt::format("{:#x}:{}x(groups {}) ", top[i].first, top[i].second.first,
                                    top[i].second.second / std::max(top[i].second.first, 1u));
            }
            LOG_ERROR(Render, "dispatch/6s {} shaders: {}", top.size(), line);
            histogram.clear();
        }
    }
    if (ExecuteShaderHLE(cs, liverpool->regs, cs_program, *this)) {
        return;
    }

    if (ScopedNs timer{g_stats.ns_bind}; !BindResources(pipeline)) {
        return;
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    scheduler.EndRendering();
    pipeline->BindResources(set_writes, push_data);

    auto& cmdbuf = scheduler.Stream();
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->Handle());
    VideoCore::OpTrace::Add('C', cs.pgm_hash, cs_program.dim_x, cs_program.dim_y, cs_program.dim_z);
    if (g_pass_stat_on) {
        NoteCsWriters(cs, cs_program.dim_x, cs_program.dim_y, cs_program.dim_z);
    }
    cmdbuf.dispatch(cs_program.dim_x, cs_program.dim_y, cs_program.dim_z);
    DebugState.IncDispatch();

    // Small compute outputs are constant blocks that later shaders read through SRT pointers,
    // which are resolved on the CPU; keep guest memory in step with them.
    static const u32 mirror_limit = [] {
        const char* kb = std::getenv("SOTC_MIRROR_KB");
        const char* bytes = std::getenv("SOTC_MIRROR_BYTES");
        return bytes ? u32(std::atoi(bytes)) : (kb ? u32(std::atoi(kb)) : 16u) << 10;
    }();
    // 0x50784801 builds the per-view camera block that the very next draws read through the SRT
    // walker, so it is mirrored immediately; everything else waits for the next sync point.
    const bool mirror_now = cs.pgm_hash == 0x50784801;
    boost::container::small_vector<std::pair<VAddr, u32>, 4> mirrors;
    // Experiment: SOTC_MIRROR_SKIP=hash,hash leaves those shaders' outputs on the GPU only.
    // SOTC_MIRROR_ONLY=hash,hash mirrors nothing but those shaders' outputs.
    static const auto hash_list = [](const char* name) {
        std::vector<u64> list;
        const char* value = std::getenv(name);
        for (const char* cur = value; cur && *cur;) {
            char* end = nullptr;
            list.push_back(std::strtoull(cur, &end, 16));
            cur = *end ? end + 1 : end;
        }
        return list;
    };
    static const std::vector<u64> mirror_skip = hash_list("SOTC_MIRROR_SKIP");
    // Every small output is mirrored: a page it shares with CPU-written data would otherwise be
    // re-uploaded with stale bytes over it (frames stalling for seconds, device loss). But only
    // 0x4defcf7f and 0x2e0f474b (culling) are read back by the game right after an end-of-pipe
    // event, so only those make the next such event wait for the GPU; the rest ride along with
    // that flush instead of costing a round trip each (17 per frame before).
    // SOTC_MIRROR_ALL=1 flushes at every end-of-pipe event again; SOTC_MIRROR_ONLY=hash,hash
    // mirrors nothing but those shaders' outputs.
    static const std::vector<u64> mirror_only = hash_list("SOTC_MIRROR_ONLY");
    static const bool flush_always = std::getenv("SOTC_MIRROR_ALL") != nullptr;
    const bool urgent = flush_always || g_mirror_everything || cs.pgm_hash == 0x4defcf7f ||
                        cs.pgm_hash == 0x2e0f474b;
    static std::unordered_map<u64, std::pair<u32, u64>> mirror_histogram;
    if (is_sotc && !g_bisect.active &&
        std::ranges::find(g_bisect.hashes, u64(cs.pgm_hash)) == g_bisect.hashes.end()) {
        g_bisect.hashes.push_back(cs.pgm_hash);
    }
    // I3_NO_MIRROR=1 leaves compute outputs on the GPU only (test switch).
    static const bool mirror_outputs = is_sotc || std::getenv("I3_NO_MIRROR") == nullptr;
    const bool skip_mirror =
        !mirror_outputs ||
        g_bisect.Skipped() == cs.pgm_hash ||
        std::ranges::find(mirror_skip, u64(cs.pgm_hash)) != mirror_skip.end() ||
        (!mirror_only.empty() && !g_mirror_everything &&
         std::ranges::find(mirror_only, u64(cs.pgm_hash)) == mirror_only.end());
    for (const auto& desc : cs.buffers) {
        if (skip_mirror) {
            break;
        }

        if (desc.buffer_type != Shader::BufferType::Guest || !desc.is_written) {
            continue;
        }
        const auto sharp = desc.GetSharp(cs);
        if (sharp.base_address != 0 && sharp.GetSize() != 0) {
            NoteCsOutput(cs.pgm_hash, sharp.base_address, sharp.GetSize());
        }
        if (sharp.base_address != 0 && sharp.GetSize() != 0 && sharp.GetSize() <= mirror_limit) {
            mirrors.emplace_back(sharp.base_address, sharp.GetSize());
        } else if (sharp.base_address != 0 && sharp.GetSize() > mirror_limit &&
                   sharp.GetSize() <= (1u << 19) && g_arg_windows.Enabled()) {
            g_arg_windows.NoteOutput(sharp.base_address, sharp.GetSize());
            if (g_arg_windows.IsLearned(sharp.base_address)) {
                mirrors.emplace_back(sharp.base_address, sharp.GetSize());
                g_arg_windows.urgent = true;
            }
        }
    }
    ResetBindings(true);
    static const bool log_mirrors = is_sotc || std::getenv("I3_MIRROR_LOG") != nullptr;
    if (log_mirrors) {
        auto& entry = mirror_histogram[cs.pgm_hash];
        for (const auto& [address, size] : mirrors) {
            ++entry.first;
            entry.second += size;
        }
        static auto last = std::chrono::steady_clock::now();
        static u32 calls = 0;
        if (const auto now = (++calls & 0xFFF) ? last : std::chrono::steady_clock::now();
            now - last >= std::chrono::seconds(6)) {
            last = now;
            std::string line;
            for (const auto& [hash, value] : mirror_histogram) {
                if (value.first) {
                    line += fmt::format("{:#x}:{}x{}B ", hash, value.first, value.second / value.first);
                }
            }
            LOG_ERROR(Render, "mirrors/6s: {}", line);
            mirror_histogram.clear();
        }
    }
    g_stats.mirrors += u32(mirrors.size());
    pending_mirrors.insert(pending_mirrors.end(), mirrors.begin(), mirrors.end());
    mirrors_urgent |= urgent && !mirrors.empty();
    {
        // Compute shaders whose small outputs the game reads on the CPU right after its
        // completion signal (found with on-demand readback switched on: the game read these
        // five about nine times a frame in the first boss arena, and nothing else that is
        // copied back). The outputs sit in memory the game hands out anew every frame, so until
        // the result lands the game finds whatever lay there before: garbage counts, dropped
        // draws that grow in number, and after about two minutes of firing a crash (one soak
        // in three). Their results are therefore copied back at the next completion signal,
        // and only theirs (see FlushMirrors). That is a blocking wait for the GPU about three
        // times a frame; measured cost 4 to 8 FPS in a fire fight and about 8 FPS at the crash
        // site. I3_NO_READ_URGENT=1 switches it off. Giving the memory last frame's result
        // instead (I3_PREDICT=1, with the flush switched off) was tried and made things worse:
        // three soaks of four died.
        static const bool known_title = Common::ElfInfo::Instance().GameSerial() == "CUSA00004";
        static const bool flush_at_signal = std::getenv("I3_NO_READ_URGENT") == nullptr;
        static const bool predict = std::getenv("I3_PREDICT") != nullptr;
        constexpr std::array<u64, 5> cpu_read{0x2c6b674a, 0x63e538c1, 0x47a670fd, 0x3ea96db0,
                                              0xa8b68717};
        if (known_title && !mirrors.empty() &&
            std::ranges::find(cpu_read, u64(cs.pgm_hash)) != cpu_read.end()) {
            if (flush_at_signal) {
                g_arg_windows.urgent = true;
                g_urgent_mirrors.insert(g_urgent_mirrors.end(), mirrors.begin(), mirrors.end());
            } else if (predict) {
                for (const auto& [address, size] : mirrors) {
                    if (size > 4096) {
                        continue;
                    }
                    // The same dispatch of the same shader a frame earlier: by its place among
                    // that shader's dispatches of the frame.
                    const u64 key = (u64(cs.pgm_hash) << 20) | (g_predict.ordinal[cs.pgm_hash]++ & 0xfffff);
                    if (const auto it = g_predict.last.find(key);
                        it != g_predict.last.end() && it->second.size() == size) {
                        memory->TryWriteBacking(std::bit_cast<u8*>(address), it->second.data(),
                                                size);
                        ++g_predict.written;
                    }
                    buffer_cache.RequestCapture(address, size, key);
                }
            }
        }
    }
    for (const auto& [address, size] : mirrors) {
        buffer_cache.AddPendingMirror(address, size);
    }
    if (mirror_now) {
        FlushMirrors(32);
    }
    return;

    ResetBindings(true);
}

void Rasterizer::DispatchIndirect(VAddr address, u32 offset, u32 size) {
    RENDERER_TRACE;
    ++g_stats.dispatch;
    ScopedNs total_timer{g_stats.ns_dispatch_total};
    Common::FreezeTimer freeze_timer{4};

    scheduler.PopPendingOperations();
    SubmitPeriodically(scheduler);

    const auto& cs_program = liverpool->GetCsRegs();
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        return;
    }

    if (ScopedNs timer{g_stats.ns_bind}; !BindResources(pipeline)) {
        return;
    }

    // Group counts are read from guest memory and checked, like indirect draw arguments: a
    // slot holding unrelated data would ask the GPU for billions of groups.
    static const bool check_args = Common::ElfInfo::Instance().GameSerial() == "CUSA00004";
    u32 groups[3]{};
    if (check_args || g_pass_stat_on) {
        Core::Memory::Instance()->CopySparseMemory(address + offset,
                                                   reinterpret_cast<u8*>(groups), sizeof(groups));
        if (g_pass_stat_on) {
            static u32 logged = 0;
            const bool bad = groups[0] > 0x10000 || groups[1] > 0x10000 || groups[2] > 0x10000;
            if (bad || logged++ < 20) {
                LOG_WARNING(Render, "indirect dispatch cs {:#x} at {:#x}: groups {:#x} {:#x} {:#x} "
                                    "gpu-written {}",
                            pipeline->GetStage(Shader::SwStage::Compute).pgm_hash, address + offset,
                            groups[0], groups[1], groups[2],
                            buffer_cache.IsRegionGpuModified(address + offset, size));
            }
            NoteCsWriters(pipeline->GetStage(Shader::SwStage::Compute), groups[0], groups[1],
                          groups[2]);
        }
        if (check_args && (groups[0] > 0x10000 || groups[1] > 0x10000 || groups[2] > 0x10000)) {
            LOG_WARNING(Render, "Dropped indirect dispatch at {:#x}: groups {:#x} {:#x} {:#x}",
                        address + offset, groups[0], groups[1], groups[2]);
            ResetBindings(true);
            return;
        }
    }

    const auto [buffer, base] = buffer_cache.ObtainBuffer(address + offset, size, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, base, size);
    bound_buffers.emplace_back(buffer, base, size, vk::AccessFlagBits2::eIndirectCommandRead);

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    scheduler.EndRendering();
    pipeline->BindResources(set_writes, push_data);

    auto& cmdbuf = scheduler.Stream();
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->Handle());
    VideoCore::OpTrace::Add('X', pipeline->GetStage(Shader::SwStage::Compute).pgm_hash,
                            address + offset);
    cmdbuf.dispatchIndirect(buffer->Handle(), base);
    DebugState.IncDispatch();

    ResetBindings(true);
}

u64 Rasterizer::Flush() {
    const u64 current_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return current_tick;
}

void Rasterizer::Finish() {
    scheduler.Finish();
}

void Rasterizer::FlushMirrors(u32 source) {
    // Sources: 1 EOS, 2 EOP, 4 ReleaseMem, 8 submit, 16 fence, 32 immediate. The game consumes
    // compute results after end-of-pipe events; flushing at EOS, ReleaseMem and fences only adds
    // blocking GPU round trips (measured: same draw counts, about half the flushes).
    // SOTC_FLUSH_MASK overrides the set for experiments.
    static const u32 mask = [] {
        const char* value = std::getenv("SOTC_FLUSH_MASK");
        return value ? u32(std::atoi(value)) : (2u | 8u | 32u);
    }();
    // A completion signal with an argument window pending: the game reads it next.
    const bool forced = g_arg_windows.urgent && (source & (1u | 4u | 16u)) != 0;
    if (!pending_mirrors.empty() && !(mask & source) && !forced) {
        ++g_stats.unflushed_signal[std::countr_zero(source)];
        g_stats.unflushed_outputs += pending_mirrors.size();
    }
    if (pending_mirrors.empty() || (!(mask & source) && !forced)) {
        return;
    }
    if (forced) {
        ++g_stats.forced_flushes;
    }
    if (forced && !(mask & source) && !g_urgent_mirrors.empty()) {
        // Only what the game is about to read goes back now; the several hundred other pending
        // outputs keep waiting for the end of the frame.
        ScopedNs timer{g_stats.ns_mirror};
        std::ranges::sort(g_urgent_mirrors);
        const auto dup = std::ranges::unique(g_urgent_mirrors);
        g_urgent_mirrors.erase(dup.begin(), dup.end());
        buffer_cache.MirrorToGuest(g_urgent_mirrors, true);
        g_urgent_mirrors.clear();
        g_arg_windows.urgent = false;
        return;
    }
    // An end-of-pipe event waits only for outputs the game reads; the rest ride along with the
    // next such flush, or go at the end of a frame that had none.
    if (source == 2 && !mirrors_urgent) {
        return;
    }
    if (source == 8 && mirrors_flushed_this_frame) {
        mirrors_flushed_this_frame = false;
        return;
    }
    mirrors_urgent = false;
    g_arg_windows.urgent = false;
    g_urgent_mirrors.clear();
    mirrors_flushed_this_frame = source != 8;
    ScopedNs timer{g_stats.ns_mirror};
    ++g_stats.flushes;
    ++g_stats.flush_source[std::countr_zero(source)];
    std::ranges::sort(pending_mirrors);
    const auto dup = std::ranges::unique(pending_mirrors);
    pending_mirrors.erase(dup.begin(), dup.end());
    buffer_cache.MirrorToGuest(pending_mirrors);
    for (auto& [key, bytes] : buffer_cache.TakeCaptured()) {
        g_predict.last[key] = std::move(bytes);
    }
    pending_mirrors.clear();
}

// inFAMOUS binds its geometry heaps whole, read-only: 0.5 and 2.5 GB, indexed by a per-instance
// offset, so no part of them can be left out. The first bind used to upload all of it in one
// frame (a freeze of 2 to 3 seconds). The ranges are remembered per title, and in later sessions
// they are uploaded a slice a frame from the start, so the first bind finds only what the game
// wrote since the last pass. Uploading early is sound for memory only the game writes (a later
// write marks the page again); ranges the GPU has written are left out.
static constexpr u64 HugeBindSize = 256ull << 20;
static constexpr u64 HugeWarmSlice = 16ull << 20;

struct HugeBind {
    VAddr base;
    u64 size;
    u64 cursor{};
    bool bound{};
};

static bool HugeWarmEnabled() {
    static const bool enabled = Common::ElfInfo::Instance().GameSerial() == "CUSA00004" &&
                                std::getenv("I3_NO_HEAP_WARM") == nullptr;
    return enabled;
}

static std::filesystem::path HugeBindFile() {
    return Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
           std::string{Common::ElfInfo::Instance().GameSerial()} / "heap_binds.txt";
}

static std::vector<HugeBind>& HugeBinds() {
    static std::vector<HugeBind> binds = [] {
        std::vector<HugeBind> loaded;
        if (FILE* f = std::fopen(HugeBindFile().string().c_str(), "r")) {
            unsigned long long base, size;
            while (loaded.size() < 16 && std::fscanf(f, "%llx %llx", &base, &size) == 2) {
                if (size >= HugeBindSize && size <= (4ull << 30)) {
                    loaded.push_back({VAddr(base), u64(size)});
                }
            }
            std::fclose(f);
        }
        if (loaded.empty()) {
            // Nothing remembered yet (first session): the two heaps version 1.07 bound in
            // every measured run. A wrong guess only uploads mapped memory nobody reads.
            loaded.push_back({VAddr(0x271258600), 0x9bbc9c94});
            loaded.push_back({VAddr(0x248002000), 0x21dfe000});
        }
        return loaded;
    }();
    return binds;
}

static void NoteHugeBind(VAddr base, u64 size) {
    if (!HugeWarmEnabled()) {
        return;
    }
    auto& binds = HugeBinds();
    for (auto& bind : binds) {
        if (bind.base == base && bind.size == size) {
            bind.bound = true;
            return;
        }
    }
    if (binds.size() >= 16) {
        return;
    }
    binds.push_back({base, size, 0, true});
    if (FILE* f = std::fopen(HugeBindFile().string().c_str(), "a")) {
        std::fprintf(f, "%llx %llx\n", static_cast<unsigned long long>(base),
                     static_cast<unsigned long long>(size));
        std::fclose(f);
    }
}

void Rasterizer::OnSubmit() {
    g_shadow_maps.NextFrame();
    g_predict.ordinal.clear();
    if (HugeWarmEnabled()) {
        // One slice of each remembered heap that this session has not bound yet.
        for (auto& bind : HugeBinds()) {
            if (bind.bound) {
                continue;
            }
            const VAddr start = bind.base + bind.cursor;
            const u64 length = std::min(HugeWarmSlice, bind.size - bind.cursor);
            bind.cursor = bind.cursor + length >= bind.size ? 0 : bind.cursor + length;
            boost::container::small_vector<std::pair<VAddr, u64>, 8> parts;
            ForEachMappedRangeInRange(start, length, [&](const auto& range) {
                parts.emplace_back(range.lower(), range.upper() - range.lower());
            });
            for (const auto& [addr, part_size] : parts) {
                buffer_cache.WarmRange(addr, u32(part_size));
            }
        }
    }
    {
        // Freeze log: when a frame took over 80 ms, say where the time went. Thread CPU time
        // far below the gap means this thread waited (for the game or the GPU).
        static const bool freeze_log = Common::g_freeze_log =
            std::getenv("I3_FREEZELOG") != nullptr;
        static auto last = std::chrono::steady_clock::now();
        static u64 last_cpu = 0, last_read_us = 0, last_read_bytes = 0, last_wait = 0;
        static u64 last_upload = 0;
        const u64 upload = Common::g_upload_bytes.load();
        static u64 last_part[9]{};
        FILETIME creation, exit, kernel, user;
        GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user);
        const auto ticks = [](const FILETIME& t) {
            return (u64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
        };
        const u64 cpu = (ticks(kernel) + ticks(user)) / 10;
        const u64 read_us = Common::g_file_read_us.load();
        const u64 read_bytes = Common::g_file_read_bytes.load();
        const u64 wait = Common::g_wait_total_us.load();
        const auto now = std::chrono::steady_clock::now();
        const u64 gap_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last).count();
        u64 part[9];
        for (int i = 0; i < 9; ++i) {
            part[i] = Common::g_freeze_us[i].load();
        }
        if (freeze_log && gap_us > 80'000) {
            LOG_ERROR(Render, "freeze {} ms: render thread cpu {} ms (gpu waits {}, image create {}, "
                              "image upload {}, pipeline compile {}, draws {}, dispatches {}, "
                              "buffers {} of which resident {} upload {}, shader compile {}); "
                              "game file reads {} ms, {} KB; buffer upload {} MB",
                      gap_us / 1000, (cpu - last_cpu) / 1000, (wait - last_wait) / 1000,
                      (part[0] - last_part[0]) / 1000, (part[1] - last_part[1]) / 1000,
                      (part[2] - last_part[2]) / 1000, (part[3] - last_part[3]) / 1000,
                      (part[4] - last_part[4]) / 1000, (part[5] - last_part[5]) / 1000,
                      (part[7] - last_part[7]) / 1000, (part[8] - last_part[8]) / 1000,
                      (part[6] - last_part[6]) / 1000, (read_us - last_read_us) / 1000,
                      (read_bytes - last_read_bytes) / 1024, (upload - last_upload) >> 20);
        }
        last_upload = upload;
        last = now;
        last_cpu = cpu;
        last_read_us = read_us;
        last_read_bytes = read_bytes;
        last_wait = wait;
        std::ranges::copy(part, last_part);
    }
    FlushMirrors(8);
    {
        static const char* at = std::getenv("SOTC_BISECT_AT");
        static const auto start = std::chrono::steady_clock::now();
        if (at && !g_bisect.active &&
            std::chrono::steady_clock::now() - start >= std::chrono::seconds(std::atoi(at))) {
            g_bisect.active = true;
            g_bisect.frame = 0;
            g_bisect.draws = 0;
            LOG_ERROR(Render, "bisect: start, {} shaders", g_bisect.hashes.size());
        } else if (g_bisect.active && g_bisect.index < g_bisect.hashes.size()) {
            if (g_bisect.frame >= 2 && g_bisect.frame < 5) {
                g_bisect.draws += g_bisect.total_draws;
            }
            if (++g_bisect.frame == 8) {
                LOG_ERROR(Render, "bisect: {:#x} indirect/frame {}", g_bisect.hashes[g_bisect.index],
                          g_bisect.draws / 3);
                ++g_bisect.index;
                g_bisect.frame = 0;
                g_bisect.draws = 0;
            }
        }
        if (!g_bisect.active && !g_mirror_everything && g_bisect.total_draws > 20000) {
            g_mirror_everything = true;
            LOG_ERROR(Render, "{} indirect draws in one frame: mirroring every compute output",
                      g_bisect.total_draws);
        }
        g_bisect.total_draws = 0;
    }
    ++g_stats.frames;
    texture_cache.DebugDumpImages();
    buffer_cache.TickFrame();
    texture_cache.ProcessDownloadImages();
    texture_cache.RunGarbageCollector();
    runtime.TickFrame();
}

// The game waits for a label before it reads the results of its compute work. Blocking the
// command thread until those results are back costs the overlap of CPU and GPU work (about
// 7 ms a frame at the crash site). Instead the copy of the results is submitted, the command
// thread goes on, and the label is written by a deferred operation once the GPU has run the
// copy and the results are in guest memory: the game is told only when it is true, as on the
// console. Labels written while one is still waiting queue up behind it, so their order holds.
// I3_NO_ASYNC_LABELS=1 switches this off (the results are then fetched with a blocking wait).
bool Rasterizer::DeferLabel(void* address, const void* data, u32 size) {
    static const bool enabled = Common::ElfInfo::Instance().GameSerial() == "CUSA00004" &&
                                std::getenv("I3_NO_ASYNC_LABELS") == nullptr;
    if (!enabled || size > 32) {
        return false;
    }
    std::array<u8, 32> bytes{};
    std::memcpy(bytes.data(), data, size);
    if (g_arg_windows.urgent && !g_urgent_mirrors.empty()) {
        std::ranges::sort(g_urgent_mirrors);
        const auto dup = std::ranges::unique(g_urgent_mirrors);
        g_urgent_mirrors.erase(dup.begin(), dup.end());
        ++deferred_labels;
        ++g_stats.forced_flushes;
        buffer_cache.MirrorUrgentAsync(g_urgent_mirrors, [this, address, bytes, size] {
            std::memcpy(address, bytes.data(), size);
            --deferred_labels;
        });
        g_urgent_mirrors.clear();
        g_arg_windows.urgent = false;
        return true;
    }
    if (deferred_labels != 0) {
        ++deferred_labels;
        scheduler.DeferOperation([this, address, bytes, size] {
            std::memcpy(address, bytes.data(), size);
            --deferred_labels;
        });
        scheduler.Flush();
        return true;
    }
    return false;
}

void Rasterizer::CompleteDeferred(bool wait) {
    if (deferred_labels == 0) {
        return;
    }
    if (wait) {
        ++g_stats.deferred_waits;
        scheduler.Finish();
    }
    scheduler.PopPendingOperations();
}

void Rasterizer::OnFence() {
    FlushMirrors(16);
    texture_cache.ProcessDownloadImages();
}

bool Rasterizer::BindResources(const Pipeline* pipeline) {
    g_tlg_draw_tags = 0;
    if (IsComputeImageCopy(pipeline) || IsComputeMetaClear(pipeline) ||
        IsComputeImageClear(pipeline)) {
        return false;
    }

    set_write_index = 0;
    set_writes.clear();
    buffer_infos.clear();
    image_infos.clear();

    bool uses_dma = false;

    // Bind resource buffers and textures.
    Shader::Backend::Bindings binding{};
    push_data = MakeUserData(liverpool->regs);
    for (const auto* stage : pipeline->GetStages()) {
        if (!stage) {
            continue;
        }
        set_writes.resize(set_writes.size() + stage->buffers.size() + stage->images.size() +
                          stage->samplers.size());
        {
            ScopedNs t{g_stats.ns_bind_buffers};
            BindBuffers(*stage, binding, push_data);
        }
        {
            ScopedNs t{g_stats.ns_bind_textures};
            BindTextures(*stage, binding);
        }
        uses_dma |= stage->uses_dma;
    }

    if (uses_dma) {
        buffer_cache.SynchronizeDmaBuffers();
    }

    return true;
}

void Rasterizer::BindVertexBuffers(const GraphicsPipeline* pipeline) {
    const auto& regs = liverpool->regs;
    VertexInputs<vk::VertexInputAttributeDescription2EXT> attributes;
    VertexInputs<vk::VertexInputBindingDescription2EXT> bindings;
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT> divisors;
    VertexInputs<AmdGpu::Buffer> guest_buffers;
    pipeline->GetVertexInputs(attributes, bindings, divisors, guest_buffers,
                              regs.vgt_instance_step_rate_0, regs.vgt_instance_step_rate_1);

    if (instance.IsVertexInputDynamicState()) {
        // Update current vertex inputs.
        auto& cmdbuf = scheduler.Stream();
        cmdbuf.setVertexInputEXT(bindings, attributes);
    }

    if (DrawLogActive()) {
        std::string vbs;
        for (const auto& buffer : guest_buffers) {
            vbs += fmt::format("[{:#x} st{} n{:#x}]", u64(buffer.base_address),
                               buffer.GetStride(), u32(buffer.num_records));
        }
        LOG_ERROR(Render, "   vertex bindings {} attrs {} {}", bindings.size(), attributes.size(),
                  vbs);
    }

    if (bindings.empty()) {
        // If there are no bindings, there is nothing further to do.
        return;
    }

    struct BufferRange {
        VAddr base_address;
        VAddr end_address;
        const VideoCore::Buffer* buffer;
        u64 offset;

        [[nodiscard]] size_t GetSize() const {
            return end_address - base_address;
        }
    };

    // Build list of ranges covering the requested buffers
    VertexInputs<BufferRange> ranges{};
    for (const auto& buffer : guest_buffers) {
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            ranges.emplace_back(buffer.base_address, buffer.base_address + buffer.GetSize());
        }
    }

    // Merge connecting ranges together
    VertexInputs<BufferRange> ranges_merged{};
    if (!ranges.empty()) {
        std::ranges::sort(ranges, [](const BufferRange& lhv, const BufferRange& rhv) {
            return lhv.base_address < rhv.base_address;
        });
        ranges_merged.emplace_back(ranges[0]);
        for (auto range : ranges) {
            auto& prev_range = ranges_merged.back();
            if (prev_range.end_address < range.base_address) {
                ranges_merged.emplace_back(range);
            } else {
                prev_range.end_address = std::max(prev_range.end_address, range.end_address);
            }
        }
    }

    // Map buffers for merged ranges
    for (auto& range : ranges_merged) {
        const u64 size = memory->ClampRangeSize(range.base_address, range.GetSize());
        std::tie(range.buffer, range.offset) =
            buffer_cache.ObtainBuffer(range.base_address, size, false);
        // Small vertex ranges are copied into the stream buffer, which needs no barrier
        // tracking (see BindBuffers).
        if (range.buffer != &buffer_cache.GetStreamBuffer()) {
            needs_barrier |= runtime.IsBufferAccessed(range.buffer, range.offset, size);
            bound_buffers.emplace_back(range.buffer, range.offset, size,
                                       vk::AccessFlagBits2::eVertexAttributeRead);
        }
    }

    // Bind vertex buffers
    VertexInputs<vk::Buffer> host_buffers;
    VertexInputs<vk::DeviceSize> host_offsets;
    VertexInputs<vk::DeviceSize> host_sizes;
    VertexInputs<vk::DeviceSize> host_strides;
    for (const auto& buffer : guest_buffers) {
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            const auto host_buffer_info =
                std::ranges::find_if(ranges_merged, [&](const BufferRange& range) {
                    return buffer.base_address >= range.base_address &&
                           buffer.base_address < range.end_address;
                });
            ASSERT(host_buffer_info != ranges_merged.cend());
            host_buffers.emplace_back(host_buffer_info->buffer->Handle());
            host_offsets.push_back(host_buffer_info->offset + buffer.base_address -
                                   host_buffer_info->base_address);
        } else {
            host_buffers.emplace_back(VK_NULL_HANDLE);
            host_offsets.push_back(0);
        }
        host_sizes.push_back(buffer.GetSize());
        host_strides.push_back(buffer.GetStride());
    }

    auto& cmdbuf = scheduler.Stream();
    const auto num_buffers = guest_buffers.size();
    if (instance.IsVertexInputDynamicState()) {
        cmdbuf.bindVertexBuffers(0, num_buffers, host_buffers.data(), host_offsets.data());
    } else {
        cmdbuf.bindVertexBuffers2(0, num_buffers, host_buffers.data(), host_offsets.data(),
                                  host_sizes.data(), host_strides.data());
    }
}

void Rasterizer::BindIndexBuffer(u32 index_offset) {
    const auto& regs = liverpool->regs;

    // Figure out index type and size.
    const bool is_index16 = regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16;
    const vk::IndexType index_type = is_index16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    const u32 index_size = is_index16 ? sizeof(u16) : sizeof(u32);
    const VAddr index_address =
        regs.index_base_address.Address<VAddr>() + index_offset * index_size;

    // Bind index buffer.
    const u32 index_buffer_size = regs.num_indices * index_size;
    const auto [buffer, offset] =
        buffer_cache.ObtainBuffer(index_address, index_buffer_size, false);
    // Small index ranges are copied into the stream buffer, which needs no barrier tracking
    // (see BindBuffers).
    if (buffer != &buffer_cache.GetStreamBuffer()) {
        needs_barrier |= runtime.IsBufferAccessed(buffer, offset, index_buffer_size);
        bound_buffers.emplace_back(buffer, offset, index_buffer_size,
                                   vk::AccessFlagBits2::eIndexRead);
    }

    auto& cmdbuf = scheduler.Stream();
    cmdbuf.bindIndexBuffer(buffer->Handle(), offset, index_type);
}

void Rasterizer::ResetBindings(bool is_compute) {
    for (auto& image_id : bound_images) {
        texture_cache.GetImage(image_id).binding = {};
    }
    for (const auto [buffer, offset, size, src_access] : bound_buffers) {
        const auto dst_stage = is_compute ? vk::PipelineStageFlagBits2::eComputeShader
                                          : vk::PipelineStageFlagBits2::eAllGraphics;
        runtime.AccessBuffer(buffer, offset, size, dst_stage, src_access);
    }
    bound_images.clear();
    bound_buffers.clear();
    needs_barrier = false;
}

bool Rasterizer::IsComputeMetaClear(const Pipeline* pipeline) {
    if (!pipeline->IsCompute()) {
        return false;
    }

    // Most of the time when a metadata is updated with a shader it gets cleared. It means
    // we can skip the whole dispatch and update the tracked state instead. Also, it is not
    // intended to be consumed and in such rare cases (e.g. HTile introspection, CRAA) we
    // will need its full emulation anyways.
    const auto& info = pipeline->GetStage(Shader::SwStage::Compute);

    // Assume if a shader reads metadata, it is a copy shader.
    for (const auto& desc : info.buffers) {
        const VAddr address = desc.GetSharp(info).base_address;
        if (!desc.IsSpecial() && !desc.is_written && texture_cache.IsMeta(address)) {
            return false;
        }
    }

    // Metadata surfaces are tiled and thus need address calculation to be written properly.
    // If a shader wants to encode HTILE, for example, from a depth image it will have to compute
    // proper tile address from dispatch invocation id. This address calculation contains an xor
    // operation so use it as a heuristic for metadata writes that are probably not clears.
    if (!info.has_bitwise_xor) {
        // Assume if a shader writes metadata without address calculation, it is a clear shader.
        for (const auto& desc : info.buffers) {
            const VAddr address = desc.GetSharp(info).base_address;
            if (!desc.IsSpecial() && desc.is_written && texture_cache.ClearMeta(address)) {
                // Assume all slices were updates
                LOG_TRACE(Render_Vulkan, "Metadata update skipped");
                return true;
            }
        }
    }
    return false;
}

bool Rasterizer::IsComputeImageCopy(const Pipeline* pipeline) {
    if (!pipeline->IsCompute()) {
        return false;
    }

    // Ensure shader only has 2 bound buffers
    const auto& cs_pgm = liverpool->GetCsRegs();
    const auto& info = pipeline->GetStage(Shader::SwStage::Compute);
    if (cs_pgm.num_thread_x.full != 64 || info.buffers.size() < 2 || info.buffers.size() > 3 ||
        !info.images.empty()) {
        return false;
    }

    // Those 2 buffers must both be formatted. One must be source and another destination.
    const auto& desc0 = info.buffers[0];
    const auto& desc1 = info.buffers[1];
    if (!desc0.is_formatted || !desc1.is_formatted || desc0.is_written == desc1.is_written) {
        return false;
    }
    if (info.buffers.size() == 3 && info.buffers[2].buffer_type != Shader::BufferType::Flatbuf) {
        return false;
    }

    // Buffers must have the same size and each thread of the dispatch must copy 1 dword of data
    const AmdGpu::Buffer buf0 = desc0.GetSharp(info);
    const AmdGpu::Buffer buf1 = desc1.GetSharp(info);
    if (buf0.GetSize() != buf1.GetSize() || cs_pgm.dim_x != (buf0.GetSize() / 256)) {
        return false;
    }

    // Find images the buffer alias
    const auto image0_id = texture_cache.FindImageFromRange(buf0.base_address, buf0.GetSize());
    if (!image0_id) {
        return false;
    }
    const auto image1_id =
        texture_cache.FindImageFromRange(buf1.base_address, buf1.GetSize(), false);
    if (!image1_id) {
        return false;
    }

    // Image copy must be valid
    VideoCore::Image& image0 = texture_cache.GetImage(image0_id);
    VideoCore::Image& image1 = texture_cache.GetImage(image1_id);
    if (image0.info.guest_size != image1.info.guest_size ||
        image0.info.pitch != image1.info.pitch || image0.info.guest_size != buf0.GetSize() ||
        image0.info.num_bits != image1.info.num_bits) {
        return false;
    }

    // Perform image copy
    VideoCore::Image& src_image = desc0.is_written ? image1 : image0;
    VideoCore::Image& dst_image = desc0.is_written ? image0 : image1;
    runtime.CopyColorAndDepth(&src_image, &dst_image);
    return true;
}

bool Rasterizer::IsComputeImageClear(const Pipeline* pipeline) {
    if (!pipeline->IsCompute()) {
        return false;
    }

    // Ensure shader only has 2 bound buffers
    const auto& cs_pgm = liverpool->GetCsRegs();
    const auto& info = pipeline->GetStage(Shader::SwStage::Compute);
    if (cs_pgm.num_thread_x.full != 64 || info.buffers.size() < 2 || info.buffers.size() > 3 ||
        !info.images.empty()) {
        return false;
    }

    // From those 2 buffers, first must hold the clear vector and second the image being cleared
    const auto& desc0 = info.buffers[0];
    const auto& desc1 = info.buffers[1];
    if (desc0.is_formatted || !desc1.is_formatted || desc0.is_written || !desc1.is_written) {
        return false;
    }
    if (info.buffers.size() == 3 && info.buffers[2].buffer_type != Shader::BufferType::Flatbuf) {
        return false;
    }

    // First buffer must have size of vec4 and second the size of a single layer
    const AmdGpu::Buffer buf0 = desc0.GetSharp(info);
    const AmdGpu::Buffer buf1 = desc1.GetSharp(info);
    const u32 buf1_bpp = AmdGpu::NumBitsPerBlock(buf1.GetDataFmt());
    if (buf0.GetSize() != 16 || (cs_pgm.dim_x * 128ULL * (buf1_bpp / 8)) != buf1.GetSize()) {
        return false;
    }

    // Find image the buffer alias
    const auto image1_id =
        texture_cache.FindImageFromRange(buf1.base_address, buf1.GetSize(), false);
    if (!image1_id) {
        return false;
    }

    // Image clear must be valid
    VideoCore::Image& image1 = texture_cache.GetImage(image1_id);
    if (image1.info.guest_size != buf1.GetSize() || image1.info.num_bits != buf1_bpp ||
        image1.info.props.is_depth) {
        return false;
    }

    // Perform image clear
    const float* values = reinterpret_cast<float*>(buf0.base_address);
    const vk::ClearValue clear = {
        .color = {.float32 = std::array<float, 4>{values[0], values[1], values[2], values[3]}},
    };
    const VideoCore::SubresourceRange range = {
        .base =
            {
                .level = 0,
                .layer = 0,
            },
        .extent = image1.info.resources,
    };
    runtime.ClearImage(&image1, range, clear);
    return true;
}

void Rasterizer::BindBuffers(const Shader::Info& stage, Shader::Backend::Bindings& binding,
                             Shader::PushData& push_data) {
    const u64 alignment = instance.StorageMinAlignment();
    for (const auto& desc : stage.buffers) {
        const auto src_access =
            vk::AccessFlagBits2::eShaderRead |
            (desc.is_written ? vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eNone);
        if (desc.IsSpecial()) {
            if (desc.buffer_type == Shader::BufferType::GdsBuffer) {
                const auto* gds_buf = buffer_cache.GetGdsBuffer();
                buffer_infos.emplace_back(gds_buf->Handle(), 0, gds_buf->SizeBytes());
                needs_barrier |=
                    runtime.IsBufferAccessed(gds_buf, 0, gds_buf->SizeBytes(), desc.is_written);
                bound_buffers.emplace_back(gds_buf, 0, gds_buf->SizeBytes(), src_access);
            } else if (desc.buffer_type == Shader::BufferType::Flatbuf) {
                auto& vk_buffer = buffer_cache.GetStreamBuffer();
                const u32 ubo_size = stage.flattened_ud_buf.size() * sizeof(u32);
                const u64 offset =
                    vk_buffer.Copy(stage.flattened_ud_buf.data(), ubo_size, alignment);
                buffer_infos.emplace_back(vk_buffer.Handle(), offset, ubo_size);
            } else if (desc.buffer_type == Shader::BufferType::ClipPlanes) {
                // Permutations compiled without enabled planes never read the buffer, so the
                // declared binding is satisfied with a null descriptor instead of a copy.
                if (liverpool->regs.clipper_control.user_clip_plane_enable == 0) {
                    buffer_infos.emplace_back(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
                } else {
                    auto& vk_buffer = buffer_cache.GetStreamBuffer();
                    std::array<float, AmdGpu::NUM_CLIP_PLANES * 4> planes{};
                    for (u32 i = 0; i < AmdGpu::NUM_CLIP_PLANES; ++i) {
                        const auto& plane = liverpool->regs.clip_user_data[i];
                        planes[i * 4 + 0] = std::bit_cast<float>(plane.data_x);
                        planes[i * 4 + 1] = std::bit_cast<float>(plane.data_y);
                        planes[i * 4 + 2] = std::bit_cast<float>(plane.data_z);
                        planes[i * 4 + 3] = std::bit_cast<float>(plane.data_w);
                    }
                    const u32 ubo_size = static_cast<u32>(sizeof(planes));
                    const u64 offset = vk_buffer.Copy(planes.data(), ubo_size, alignment);
                    buffer_infos.emplace_back(vk_buffer.Handle(), offset, ubo_size);
                }
            } else if (desc.buffer_type == Shader::BufferType::BdaPagetable) {
                const auto* bda_buffer = buffer_cache.GetBdaPageTableBuffer();
                buffer_infos.emplace_back(bda_buffer->Handle(), 0, bda_buffer->SizeBytes());
            } else if (desc.buffer_type == Shader::BufferType::FaultBuffer) {
                const auto* fault_buffer = buffer_cache.GetFaultBuffer();
                buffer_infos.emplace_back(fault_buffer->Handle(), 0, fault_buffer->SizeBytes());
            } else if (desc.buffer_type == Shader::BufferType::SharedMemory) {
                auto& lds_buffer = buffer_cache.GetStreamBuffer();
                const auto& cs_program = liverpool->GetCsRegs();
                const auto lds_size = cs_program.SharedMemSize() * cs_program.NumWorkgroups();
                const auto [data, offset] = lds_buffer.Map(lds_size, alignment);
                std::memset(data, 0, lds_size);
                lds_buffer.Commit();
                buffer_infos.emplace_back(lds_buffer.Handle(), offset, lds_size);
            } else {
                UNREACHABLE_MSG("Unexpected buffer type {}", u32(desc.buffer_type));
            }
        } else {
            const auto vsharp = desc.GetSharp(stage);
            {
                // Temporary: SOTC_BUFLOG=<shader hash> describes that shader's buffers and
                // compares their GPU copies with guest memory (first few binds, after 90 s).
                static const u64 only = [] {
                    const char* v = std::getenv("SOTC_BUFLOG");
                    return v ? std::strtoull(v, nullptr, 16) : 0ULL;
                }();
                static const auto start = std::chrono::steady_clock::now();
                static u32 logged = 0;
                if (only && desc.is_written && (vsharp.GetStride() == 68 || vsharp.GetStride() == 80 ||
                                                vsharp.GetStride() == 288)) {
                    static std::unordered_set<u64> writers;
                    if (writers.insert(stage.pgm_hash ^ (u64(vsharp.GetStride()) << 40)).second) {
                        LOG_ERROR(Render, "bufwriter shader {:#x} addr {:#x} size {:#x} stride {}",
                                  stage.pgm_hash, u64(vsharp.base_address), vsharp.GetSize(),
                                  vsharp.GetStride());
                    }
                }
                if (only && stage.pgm_hash == only && logged < 24 &&
                    std::chrono::steady_clock::now() - start > std::chrono::seconds(90) &&
                    vsharp.base_address != 0 && vsharp.GetSize() != 0) {
                    ++logged;
                    const u64 full = memory->ClampRangeSize(vsharp.base_address, vsharp.GetSize());
                    const u32 check = u32(std::min<u64>(full, 4_MB));
                    const bool valid = memory->IsValidMapping(vsharp.base_address, check);
                    const bool gpu = buffer_cache.IsRegionGpuModified(vsharp.base_address, check);
                    const u64 bad = valid && !desc.is_written
                                        ? buffer_cache.DebugMismatch(vsharp.base_address, check)
                                        : 0;
                    LOG_ERROR(Render, "buflog addr {:#x} size {:#x} clamp {:#x} stride {} written "
                                      "{} gpumod {} valid {} mismatch {} of {}",
                              u64(vsharp.base_address), vsharp.GetSize(), full,
                              vsharp.GetStride(), desc.is_written, gpu, valid, bad, check);
                    if (valid && !desc.is_written) {
                        std::vector<u8> data(check);
                        memory->CopySparseMemory(vsharp.base_address, data.data(), check);
                        if (FILE* f = std::fopen(fmt::format("buflog_{:x}_{}.raw",
                                                             u64(vsharp.base_address),
                                                             vsharp.GetStride()).c_str(), "wb")) {
                            std::fwrite(data.data(), 1, data.size(), f);
                            std::fclose(f);
                        }
                    }
                }
            }
            if (vsharp.base_address == 0 || vsharp.GetSize() == 0) {
                buffer_infos.emplace_back(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
            } else {
                u64 size = memory->ClampRangeSize(vsharp.base_address, vsharp.GetSize());
                if (size != vsharp.GetSize()) {
                    // The engine declares its structured buffers as ~4GB ("unbounded"). Binding
                    // everything up to the end of the mapping makes every draw track and upload
                    // hundreds of MB of unrelated heap, so bound them.
                    // ponytail: fixed window, derive the real extent from the shader if a
                    // buffer ever needs more.
                    static const u64 unbounded_cap = [] {
                        const char* mb = std::getenv("SOTC_UNBOUNDED_CAP_MB");
                        return (mb ? std::strtoull(mb, nullptr, 10) : 0) << 20;
                    }();
                    if (unbounded_cap) {
                        size = std::min(size, unbounded_cap);
                    }
                }
                if (!desc.is_written && size >= HugeBindSize) {
                    NoteHugeBind(vsharp.base_address, size);
                }
                static int bind_traced = 0;
                if (desc.is_written && CoversViewBlock(vsharp.base_address, size) &&
                    bind_traced++ < 12) {
                    LOG_ERROR(Render, "viewblock written bind by {:#x} base {:#x} size {:#x} "
                                      "declared {:#x} stride {}",
                              stage.pgm_hash, u64(vsharp.base_address), size, vsharp.GetSize(),
                              vsharp.GetStride());
                }
                const auto [buffer, offset] = buffer_cache.ObtainBuffer(
                    vsharp.base_address, size, desc.is_written, desc.is_formatted);
                const u64 offset_aligned = Common::AlignDown(offset, alignment);
                const u64 adjust = offset - offset_aligned;
                if (adjust % 4 != 0) {
                    LOG_WARNING(Render_Vulkan, "Buffer binding in shader {:#x} isn't dword aligned",
                                stage.pgm_hash);
                }
                push_data.AddOffset(binding.buffer, adjust);
                buffer_infos.emplace_back(buffer->Handle(), offset_aligned, size + adjust);
                // The stream buffer is only ever written through its mapping, never by the
                // GPU, so reads of it need no barrier and no bookkeeping. Small read-only
                // buffers land there for every draw; tracking each copy as a range of its
                // own was a large part of the per-draw cost.
                static const bool track_stream = std::getenv("I3_TRACK_STREAM") != nullptr;
                if (track_stream || buffer != &buffer_cache.GetStreamBuffer()) {
                    bound_buffers.emplace_back(buffer, offset, size, src_access);
                    needs_barrier |=
                        runtime.IsBufferAccessed(buffer, offset, size, desc.is_written);
                }
                if (desc.is_written) {
                    // Raw storage-buffer writes can also make an aliased cached image stale.
                    texture_cache.InvalidateMemoryFromGPU(vsharp.base_address, size);
                }
            }
        }

        auto& set_write = set_writes[set_write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified++;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = 1;
        set_write.descriptorType = vk::DescriptorType::eStorageBuffer;
        set_write.pBufferInfo = &buffer_infos.back();
        ++binding.buffer;
    }
}

void Rasterizer::BindTextures(const Shader::Info& stage, Shader::Backend::Bindings& binding) {
    const u32 first_image_idx = image_infos.size();
    // To emulate storing to explicit mip levels, build a descriptor array with each mip level.
    boost::container::small_vector<u32, 8> image_descriptor_array_sizes;

    u32 num_images{};
    for (const auto& image_desc : stage.images) {
        const auto tsharp = image_desc.GetSharp(stage);
        const auto data_fmt = tsharp.GetDataFmt();
        const auto num_fmt = tsharp.GetNumberFmt();
        if (tsharp.Address() == 0 || data_fmt == AmdGpu::DataFormat::FormatInvalid) {
            image_bindings[num_images++].image_id = {};
            image_descriptor_array_sizes.push_back(1);
            continue;
        }

        if (!memory->IsValidGpuMapping(tsharp.Address(), 0) ||
            !magic_enum::enum_contains(data_fmt) || !magic_enum::enum_contains(num_fmt)) {
            LOG_WARNING(Render_Vulkan,
                        "Rejecting invalid T# address={:#x}, pitch={}, width={}, "
                        "data_format={}, num_format={}",
                        tsharp.Address(), tsharp.pitch, tsharp.width, static_cast<u32>(data_fmt),
                        static_cast<u32>(num_fmt));
            image_bindings[num_images++].image_id = {};
            image_descriptor_array_sizes.push_back(1);
            continue;
        }

        const Shader::MipStorageFallbackMode mip_fallback_mode = image_desc.mip_fallback_mode;
        const u32 num_bindings = image_desc.NumBindings(stage);

        for (auto i = 0; i < num_bindings; i++) {
            auto& [image_id, desc] = image_bindings[num_images++];
            std::construct_at(&desc, tsharp, image_desc);

            if (mip_fallback_mode == Shader::MipStorageFallbackMode::ConstantIndex) {
                ASSERT(num_bindings == 1);
                desc.view_info.range.base.level += image_desc.constant_mip_index;
                desc.view_info.range.extent.levels = 1;
            } else if (mip_fallback_mode == Shader::MipStorageFallbackMode::DynamicIndex) {
                desc.view_info.range.base.level += i;
                desc.view_info.range.extent.levels = 1;
            }

            image_id = texture_cache.FindImage(desc);
            auto* image = &texture_cache.GetImage(image_id);
            if (auto depth_image_id = texture_cache.GetAssociatedDepth(*image)) {
                // If this image has an associated depth image, it's a stencil attachment.
                // Redirect the access to the actual depth-stencil buffer.
                image_id = depth_image_id;
                image = &texture_cache.GetImage(image_id);
            }
            if (image->binding.is_bound) {
                // The image is already bound. In case if it is about to be used as storage we
                // need to force general layout on it.
                image->binding.force_general |= image_desc.is_written;
            }
            image->binding.is_bound = 1u;
        }

        image_descriptor_array_sizes.push_back(num_bindings);
    }

    // Second pass to re-bind images that were updated after binding
    for (u32 i = 0; i < num_images; ++i) {
        auto& [image_id, desc] = image_bindings[i];
        bool is_storage = desc.type == VideoCore::TextureCache::BindingType::Storage;
        if (!image_id) {
            image_infos.emplace_back(VK_NULL_HANDLE, VK_NULL_HANDLE, vk::ImageLayout::eGeneral);
        } else {
            if (auto& old_image = texture_cache.GetImage(image_id);
                old_image.binding.needs_rebind) {
                old_image.binding = {};
                image_id = texture_cache.FindImage(desc);
            }

            bound_images.emplace_back(image_id);

            auto& image = texture_cache.GetImage(image_id);
            auto& image_view = texture_cache.FindTexture(image_id, desc);
            if (Tlg::Ui::Enabled()) {
                const bool tlg_first = image.tlg_tag == -2;
                if (tlg_first || (image.tlg_tag == -1 && (++image.tlg_retry & 31) == 0)) {
                    const auto& ii = image.info;
                    image.tlg_tag = -1;
                    if (ii.guest_size <= 512_KB && ii.size.depth == 1 &&
                        memory->IsValidMapping(ii.guest_address, ii.guest_size)) {
                        const u64 tlg_hash =
                            memory->HashSparseMemory(ii.guest_address, ii.guest_size);
                        image.tlg_tag = static_cast<s8>(Tlg::Ui::Lookup(tlg_hash));
                        // Test aid: TLG_UILOG=2 logs the menu-sized textures that match no tag.
                        static const bool log_misses = [] {
                            const char* v = std::getenv("TLG_UILOG");
                            return v && v[0] == '2';
                        }();
                        if (log_misses && tlg_first && image.tlg_tag < 0 && ii.size.width == 1024 &&
                            ii.size.height == 128) {
                            LOG_ERROR(Render, "menu texture miss: {:016x} at {:#x} flags {:#x}",
                                      tlg_hash, ii.guest_address, u32(image.flags));
                        }
                    }
                }
                if (image.tlg_tag >= 0) {
                    g_tlg_draw_tags |= 1u << image.tlg_tag;
                }
            }
            {
                // Temporary: who reads and writes the 8192-wide lighting atlases.
                static const u64 only = [] {
                    const char* v = std::getenv("SOTC_ATLASLOG");
                    return v ? std::strtoull(v, nullptr, 16) : 0ULL;
                }();
                static const bool on = only != 0;
                static std::unordered_set<u64> seen;
                if (on && stage.pgm_hash == only && image.info.size.width == 16) {
                    // Temporary: track the small probe arrays' guest contents over time.
                    static std::unordered_map<u64, u64> last_hash;
                    static u32 logged = 0;
                    const auto& ii = image.info;
                    const u64 hash = memory->HashSparseMemory(ii.guest_address, ii.guest_size);
                    if (auto [it, fresh] = last_hash.try_emplace(ii.guest_address, 0);
                        it->second != hash && logged < 60) {
                        it->second = hash;
                        ++logged;
                        std::vector<u8> data(ii.guest_size);
                        memory->CopySparseMemory(ii.guest_address, data.data(), data.size());
                        const auto zeros = std::ranges::count(data, u8{0});
                        LOG_ERROR(Render, "probearr {:#x} size {:#x} layers {} hash {:#x} zero "
                                          "bytes {} tick {} tile {}",
                                  ii.guest_address, ii.guest_size, ii.resources.layers, hash,
                                  zeros, scheduler.CurrentTick(), u32(ii.tile_mode));
                        if (FILE* f = std::fopen(
                                fmt::format("probearr_{:x}_{}_{}.raw", ii.guest_address,
                                            ii.resources.layers, logged).c_str(), "wb")) {
                            std::fwrite(data.data(), 1, data.size(), f);
                            std::fclose(f);
                        }
                    }
                }
                if (on && (only == 1 ? image.info.size.width == 8192 : stage.pgm_hash == only) &&
                    seen.insert(stage.pgm_hash ^ (u64(desc.type) << 60) ^ image.info.guest_address ^ (u64(image.flags) << 44) ^ (u64(image.info.pixel_format) << 20))
                        .second) {
                    LOG_ERROR(Render, "atlaslog shader {:#x} stage {} addr {:#x} storage {} fmt {} "
                                      "view fmt {} levels {} base {} n {} dims {}x{}x{} layers {} flags {:#x}",
                              stage.pgm_hash, u32(stage.sw_stage), image.info.guest_address,
                              desc.type == VideoCore::TextureCache::BindingType::Storage,
                              vk::to_string(image.info.pixel_format),
                              vk::to_string(desc.view_info.format), image.info.resources.levels,
                              desc.view_info.range.base.level,
                              desc.view_info.range.extent.levels, image.info.size.width,
                              image.info.size.height, image.info.size.depth,
                              image.info.resources.layers, u32(image.flags));
                }
            }
            const auto binding = image.binding;

            // The image is either bound as storage in a separate descriptor or bound as render
            // target in feedback loop. Depth images are excluded because they can't be bound as
            // storage and feedback loop doesn't make sense for them
            if ((binding.force_general || binding.is_target) && !image.info.props.is_depth) {
                if (instance.IsAttachmentFeedbackLoopLayoutSupported() && image.binding.is_target) {
                    needs_barrier |= runtime.Transit(
                        &image, vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT,
                        vk::PipelineStageFlagBits2::eAllGraphics, vk::AccessFlagBits2::eShaderRead);
                } else {
                    needs_barrier |= runtime.Transit(
                        &image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
                        desc.view_info.range);
                }
            } else {
                if (is_storage) {
                    needs_barrier |= runtime.Transit(
                        &image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
                        desc.view_info.range);
                } else {
                    const auto new_layout = image.info.props.is_depth
                                                ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                                : vk::ImageLayout::eShaderReadOnlyOptimal;
                    needs_barrier |= runtime.Transit(
                        &image, new_layout, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead, desc.view_info.range);
                }
            }
            image.usage.storage |= is_storage;
            image.usage.texture |= !is_storage;

            image_infos.emplace_back(VK_NULL_HANDLE, *image_view.image_view,
                                     image.backing->state.layout);
        }
    }

    u32 image_info_idx = first_image_idx;
    u32 image_binding_idx = 0;
    for (u32 array_size : image_descriptor_array_sizes) {
        const auto& [_, desc] = image_bindings[image_binding_idx];
        const bool is_storage = desc.type == VideoCore::TextureCache::BindingType::Storage;
        auto& set_write = set_writes[set_write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = array_size;
        set_write.descriptorType =
            is_storage ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eSampledImage;
        set_write.pImageInfo = &image_infos[image_info_idx];

        image_info_idx += array_size;
        image_binding_idx += array_size;
        binding.unified += array_size;
    }

    for (const auto& sampler : stage.samplers) {
        auto ssharp = sampler.GetSharp(stage);
        if (!ssharp.Valid() || (ssharp.border_color_type.Value() == AmdGpu::BorderColor::Custom &&
                                liverpool->regs.ta_bc_base.Address() == 0)) {
            LOG_WARNING(Render_Vulkan,
                        "Rejecting invalid S# max_aniso={}, filter_mode={}, mip_filter={}, "
                        "border_color_type={}, border_color_base={:#x}",
                        static_cast<u32>(ssharp.max_aniso.Value()),
                        static_cast<u32>(ssharp.filter_mode.Value()),
                        static_cast<u32>(ssharp.mip_filter.Value()),
                        static_cast<u32>(ssharp.border_color_type.Value()),
                        liverpool->regs.ta_bc_base.Address());
            ssharp = AmdGpu::Sampler{};
        }
        const auto vk_sampler =
            texture_cache.GetSampler(ssharp, liverpool->regs.ta_bc_base, sampler.is_depth);
        image_infos.emplace_back(vk_sampler, VK_NULL_HANDLE, vk::ImageLayout::eGeneral);
        auto& set_write = set_writes[set_write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified++;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = 1;
        set_write.descriptorType = vk::DescriptorType::eSampler;
        set_write.pImageInfo = &image_infos.back();
    }
}

RenderState Rasterizer::BeginRendering(const GraphicsPipeline* pipeline) {
    attachment_feedback_loop = false;
    const auto& regs = liverpool->regs;
    const auto& key = pipeline->GetGraphicsKey();
    RenderState state;
    state.width = instance.GetMaxFramebufferWidth();
    state.height = instance.GetMaxFramebufferHeight();
    state.num_layers = std::numeric_limits<u16>::max();
    state.num_color_attachments = std::bit_width(key.mrt_mask);
    for (auto cb = 0u; cb < state.num_color_attachments; ++cb) {
        auto& [image_id, desc] = cb_descs[cb];
        if (!image_id) {
            state.color_attachments[cb] = {};
            continue;
        }
        auto* image = &texture_cache.GetImage(image_id);
        if (image->binding.needs_rebind) {
            image_id = bound_images.emplace_back(texture_cache.FindImage(desc));
            image = &texture_cache.GetImage(image_id);
        }
        texture_cache.UpdateImage(image_id);
        runtime.SetBackingSamples(image, key.color_samples[cb]);
        const auto& image_view = texture_cache.FindRenderTarget(image_id, desc);
        const auto slice = image_view.info.range.base.layer;
        const auto mip = image_view.info.range.base.level;

        const auto& col_buf = regs.color_buffers[cb];
        const bool is_clear = texture_cache.IsMetaCleared(col_buf.CmaskAddress(), slice);
        texture_cache.TouchMeta(col_buf.CmaskAddress(), slice, false);

        if (image->binding.is_bound) {
            ASSERT_MSG(!image->binding.force_general,
                       "Having image both as storage and render target is unsupported");
            runtime.FlushBarriers();
            needs_barrier |=
                runtime.Transit(image,
                                instance.IsAttachmentFeedbackLoopLayoutSupported()
                                    ? vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT
                                    : vk::ImageLayout::eGeneral,
                                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                vk::AccessFlagBits2::eColorAttachmentWrite |
                                    vk::AccessFlagBits2::eColorAttachmentRead);
            attachment_feedback_loop = true;
        } else {
            needs_barrier |= runtime.Transit(image, vk::ImageLayout::eColorAttachmentOptimal,
                                             vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                             vk::AccessFlagBits2::eColorAttachmentWrite |
                                                 vk::AccessFlagBits2::eColorAttachmentRead,
                                             desc.view_info.range);
        }

        state.width = std::min<u32>(state.width, std::max(image->info.size.width >> mip, 1u));
        state.height = std::min<u32>(state.height, std::max(image->info.size.height >> mip, 1u));
        state.num_layers = std::min<u32>(state.num_layers, image_view.info.range.extent.layers);

        const auto clear_value =
            is_clear ? LiverpoolToVK::ColorBufferClearValue(col_buf) : vk::ClearValue{};
        auto& attachment = state.color_attachments[cb];
        attachment.image_view = *image_view.image_view;
        attachment.image_layout = image->backing->state.layout;
        attachment.clear_value = clear_value.color.uint32;
        attachment.is_clear = is_clear;

        image->usage.render_target = 1u;
    }
    for (u32 cb = state.num_color_attachments; cb < state.color_attachments.size(); ++cb) {
        state.color_attachments[cb] = {};
    }

    if (auto image_id = db_desc.image_id; image_id) {
        auto& desc = db_desc.desc;
        const auto htile_address = regs.depth_htile_data_base.GetAddress();
        const auto& image_view = texture_cache.FindDepthTarget(image_id, desc);
        auto& image = texture_cache.GetImage(image_id);

        const auto slice = image_view.info.range.base.layer;
        const bool is_depth_clear =
            (regs.depth_render_control.depth_clear_enable && regs.depth_control.depth_enable &&
             regs.depth_control.depth_write_enable) ||
            texture_cache.IsMetaCleared(htile_address, slice);
        const bool is_stencil_clear = regs.depth_render_control.stencil_clear_enable;
        texture_cache.TouchMeta(htile_address, slice, false);
        ASSERT(desc.view_info.range.extent.levels == 1 && !image.binding.needs_rebind);

        const bool has_stencil = image.info.props.has_stencil;
        // Stencil writes can be enabled while depth writes are off.
        const bool stencil_write =
            has_stencil && regs.depth_control.stencil_enable && !desc.view_info.is_storage;
        const auto new_layout = desc.view_info.is_storage
                                    ? has_stencil ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                                                  : vk::ImageLayout::eDepthAttachmentOptimal
                                : stencil_write
                                    ? vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal
                                : has_stencil ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                              : vk::ImageLayout::eDepthReadOnlyOptimal;
        needs_barrier |= runtime.Transit(&image, new_layout,
                                         vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                             vk::PipelineStageFlagBits2::eLateFragmentTests,
                                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                                             vk::AccessFlagBits2::eDepthStencilAttachmentRead,
                                         desc.view_info.range);

        state.width = std::min<u32>(state.width, image.info.size.width);
        state.height = std::min<u32>(state.height, image.info.size.height);
        state.num_layers = std::min<u32>(state.num_layers, image_view.info.range.extent.layers);

        auto& attachment = state.depth_stencil_attachment;
        attachment.image_view = *image_view.image_view;
        attachment.image_layout = image.backing->state.layout;
        attachment.clear_value = {};
        attachment.is_clear = 0;

        if (regs.depth_buffer.DepthValid()) {
            attachment.clear_value[0] = is_depth_clear ? std::bit_cast<u32>(regs.depth_clear) : 0u;
            attachment.has_depth = true;
            attachment.depth_clear = is_depth_clear;
        }
        if (regs.depth_buffer.StencilValid()) {
            attachment.clear_value[1] = is_stencil_clear ? regs.stencil_clear : 0u;
            attachment.has_stencil = true;
            attachment.stencil_clear = is_stencil_clear;
        }

        image.usage.depth_target = true;
    } else {
        state.depth_stencil_attachment = {};
    }

    if (state.num_layers == std::numeric_limits<u16>::max()) {
        state.num_layers = 1;
    }

    return state;
}

void Rasterizer::Resolve() {
    const auto& mrt0_hint = liverpool->last_cb_extent[0];
    const auto& mrt1_hint = liverpool->last_cb_extent[1];
    VideoCore::TextureCache::ImageDesc mrt0_desc{liverpool->regs.color_buffers[0], mrt0_hint};
    VideoCore::TextureCache::ImageDesc mrt1_desc{liverpool->regs.color_buffers[1], mrt1_hint};
    auto& mrt0_image = texture_cache.GetImage(texture_cache.FindImage(mrt0_desc, true));
    auto& mrt1_image = texture_cache.GetImage(texture_cache.FindImage(mrt1_desc, true));

    ScopeMarkerBegin(fmt::format("Resolve:MRT0={:#x}:MRT1={:#x}",
                                 liverpool->regs.color_buffers[0].Address(),
                                 liverpool->regs.color_buffers[1].Address()));
    runtime.ResolveImage(&mrt0_image, &mrt1_image, mrt0_desc.view_info.range,
                         mrt1_desc.view_info.range);
    ScopeMarkerEnd();
}

void Rasterizer::DepthStencilCopy(bool is_depth, bool is_stencil) {
    auto& regs = liverpool->regs;

    auto read_desc = VideoCore::TextureCache::ImageDesc(
        regs.depth_buffer, regs.depth_view, regs.depth_control,
        regs.depth_htile_data_base.GetAddress(), liverpool->last_db_extent, false);
    auto write_desc = VideoCore::TextureCache::ImageDesc(
        regs.depth_buffer, regs.depth_view, regs.depth_control,
        regs.depth_htile_data_base.GetAddress(), liverpool->last_db_extent, true);

    auto& read_image = texture_cache.GetImage(texture_cache.FindImage(read_desc));
    auto& write_image = texture_cache.GetImage(texture_cache.FindImage(write_desc));

    VideoCore::SubresourceRange sub_range;
    sub_range.base.layer = liverpool->regs.depth_view.slice_start;
    sub_range.extent.layers = liverpool->regs.depth_view.NumSlices() - sub_range.base.layer;

    ScopeMarkerBegin(fmt::format(
        "DepthStencilCopy:DR={:#x}:SR={:#x}:DW={:#x}:SW={:#x}", regs.depth_buffer.DepthAddress(),
        regs.depth_buffer.StencilAddress(), regs.depth_buffer.DepthWriteAddress(),
        regs.depth_buffer.StencilWriteAddress()));

    runtime.CopyDepthStencil(&read_image, &write_image, sub_range);

    ScopeMarkerEnd();
}

// Temporary: who writes the per-view constant block on the GPU side?
static bool CoversViewBlock(VAddr addr, u64 size) {
    constexpr VAddr block = 0x24230c3a0;
    return addr <= block && block < addr + size;
}

void Rasterizer::FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) {
    ASSERT_MSG(address % 4 == 0 && num_bytes % 4 == 0,
               "FillBuffer address and size must be a multiple of 4 bytes");
    static int fill_traced = 0;
    if (!is_gds && CoversViewBlock(address, num_bytes) && fill_traced++ < 6) {
        LOG_ERROR(Render, "viewblock FillBuffer {:#x} n {:#x} value {:#x}", address, num_bytes,
                  value);
    }
    if (!is_gds) {
        texture_cache.ClearMeta(address);
        // Always keep guest memory in step: constants are also read back on the CPU (SRT walk),
        // even from regions the GPU has written to.
        u32* buffer = std::bit_cast<u32*>(address);
        std::fill(buffer, buffer + (num_bytes / sizeof(u32)), value);
        if (!buffer_cache.IsRegionGpuModified(address, num_bytes)) {
            return;
        }
    }
    const auto [buffer, offset] = [&] -> std::pair<const VideoCore::Buffer*, u64> {
        if (is_gds) {
            return {buffer_cache.GetGdsBuffer(), address};
        }
        return buffer_cache.ObtainBuffer(address, num_bytes, true);
    }();
    runtime.FillBuffer(buffer, offset, num_bytes, value);
}

void Rasterizer::CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds) {
    static int copy_traced = 0;
    if (!dst_gds && CoversViewBlock(dst, num_bytes) && copy_traced++ < 6) {
        LOG_ERROR(Render, "viewblock CopyBuffer dst {:#x} src {:#x} n {:#x} src_gds {} src_gpu {} "
                          "src_img {}",
                  dst, src, num_bytes, src_gds, buffer_cache.IsRegionGpuModified(src, num_bytes),
                  bool(texture_cache.FindImageFromRange(src, num_bytes)));
    }
    if (!dst_gds && !src_gds && !texture_cache.FindImageFromRange(src, num_bytes)) {
        // Mirror the copy in guest memory. Constants and resource descriptors copied this way
        // are also read back on the CPU (SRT walk). The GPU-modified flag is page granular and
        // sticky, so it cannot be used to decide whether the source bytes are current; if they
        // are not, the destination was stale on the CPU side anyway.
        std::memcpy(std::bit_cast<void*>(dst), std::bit_cast<void*>(src), num_bytes);
        if (!buffer_cache.IsRegionGpuModified(src, num_bytes) &&
            !buffer_cache.IsRegionGpuModified(dst, num_bytes)) {
            // Neither buffer was transferred to the GPU yet, the host copy is enough.
            return;
        }
    }
    texture_cache.InvalidateMemoryFromGPU(dst, num_bytes);
    const auto* gds_buffer = buffer_cache.GetGdsBuffer();
    const auto [src_buffer, src_offset] = [&] -> std::pair<const VideoCore::Buffer*, u64> {
        if (src_gds) {
            return {gds_buffer, src};
        }
        return buffer_cache.ObtainBuffer(src, num_bytes, false, true);
    }();
    const auto [dst_buffer, dst_offset] = [&] -> std::pair<const VideoCore::Buffer*, u64> {
        if (dst_gds) {
            return {gds_buffer, dst};
        }
        return buffer_cache.ObtainBuffer(dst, num_bytes, true, true);
    }();
    const vk::BufferCopy copy = {
        .srcOffset = src_offset,
        .dstOffset = dst_offset,
        .size = num_bytes,
    };
    runtime.CopyBuffer(src_buffer, dst_buffer, std::span{&copy, 1});
}

u32 Rasterizer::ReadDataFromGds(u32 gds_offset) {
    auto* gds_buf = buffer_cache.GetGdsBuffer();
    u32 value;
    std::memcpy(&value, gds_buf->mapped_data.data() + gds_offset, sizeof(u32));
    return value;
}

bool Rasterizer::InvalidateMemory(VAddr addr, u64 size, bool assume_locks) {
    if (!IsMapped(addr, size)) {
        // Not GPU mapped memory, can skip invalidation logic entirely.
        return false;
    }
    buffer_cache.InvalidateMemory(addr, size, assume_locks);
    texture_cache.InvalidateMemory(addr, size);
    return true;
}

bool Rasterizer::ReadMemory(VAddr addr, u64 size, bool assume_locks) {
    if (!IsMapped(addr, size)) {
        // Not GPU mapped memory, can skip invalidation logic entirely.
        return false;
    }
    buffer_cache.ReadMemory(addr, size, false, assume_locks);
    return true;
}

bool Rasterizer::IsMapped(VAddr addr, u64 size) {
    if (size == 0) {
        // There is no memory, so not mapped.
        return false;
    }
    if (static_cast<u64>(addr) > std::numeric_limits<u64>::max() - size) {
        // Memory range wrapped the address space, cannot be mapped.
        return false;
    }
    const auto range = decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);

    Common::RecursiveSharedLock lock{mapped_ranges_mutex};
    return boost::icl::contains(mapped_ranges, range);
}

void Rasterizer::MapMemory(VAddr addr, u64 size) {
    {
        std::scoped_lock lock{mapped_ranges_mutex};
        mapped_ranges += decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
    }
}

void Rasterizer::RegisterMemory(VAddr addr, u64 size) {
    page_manager.OnGpuMap(addr, size);
}

void Rasterizer::UnmapMemory(VAddr addr, u64 size) {
    buffer_cache.InvalidateMemory(addr, size);
    texture_cache.UnmapMemory(addr, size);
    {
        std::scoped_lock lock{mapped_ranges_mutex};
        mapped_ranges -= decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
    }
}

void Rasterizer::UpdateDynamicState(const GraphicsPipeline* pipeline, const bool is_indexed) const {
    UpdateViewportScissorState();
    UpdateDepthStencilState();
    UpdatePrimitiveState(is_indexed);
    UpdateRasterizationState();
    UpdateColorBlendingState(pipeline);

    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.Commit(instance, scheduler.Stream());
}

void Rasterizer::UpdateViewportScissorState() const {
    const auto& regs = liverpool->regs;

    const auto combined_scissor_value_tl = [](s16 scr, s16 win, s16 gen, s16 win_offset) {
        return std::max({scr, s16(win + win_offset), s16(gen + win_offset)});
    };
    const auto combined_scissor_value_br = [](s16 scr, s16 win, s16 gen, s16 win_offset) {
        return std::min({scr, s16(win + win_offset), s16(gen + win_offset)});
    };
    const bool enable_offset = !regs.window_scissor.window_offset_disable;

    AmdGpu::Scissor scsr{};
    scsr.top_left_x = combined_scissor_value_tl(
        regs.screen_scissor.top_left_x, s16(regs.window_scissor.top_left_x),
        s16(regs.generic_scissor.top_left_x),
        enable_offset ? regs.window_offset.window_x_offset : 0);
    scsr.top_left_y = combined_scissor_value_tl(
        regs.screen_scissor.top_left_y, s16(regs.window_scissor.top_left_y),
        s16(regs.generic_scissor.top_left_y),
        enable_offset ? regs.window_offset.window_y_offset : 0);
    scsr.bottom_right_x = combined_scissor_value_br(
        regs.screen_scissor.bottom_right_x, regs.window_scissor.bottom_right_x,
        regs.generic_scissor.bottom_right_x,
        enable_offset ? regs.window_offset.window_x_offset : 0);
    scsr.bottom_right_y = combined_scissor_value_br(
        regs.screen_scissor.bottom_right_y, regs.window_scissor.bottom_right_y,
        regs.generic_scissor.bottom_right_y,
        enable_offset ? regs.window_offset.window_y_offset : 0);

    boost::container::static_vector<vk::Viewport, AmdGpu::NUM_VIEWPORTS> viewports;
    boost::container::static_vector<vk::Rect2D, AmdGpu::NUM_VIEWPORTS> scissors;

    if (regs.polygon_control.enable_window_offset &&
        (regs.window_offset.window_x_offset != 0 || regs.window_offset.window_y_offset != 0)) {
        LOG_ERROR(Render_Vulkan,
                  "PA_SU_SC_MODE_CNTL.VTX_WINDOW_OFFSET_ENABLE support is not yet implemented.");
    }

    const auto& vp_ctl = regs.viewport_control;
    for (u32 i = 0; i < AmdGpu::NUM_VIEWPORTS; i++) {
        const auto& vp = regs.viewports[i];
        const auto& vp_d = regs.viewport_depths[i];
        if (vp.xscale == 0) {
            continue;
        }

        const auto zoffset = vp_ctl.zoffset_enable ? vp.zoffset : 0.f;
        const auto zscale = vp_ctl.zscale_enable ? vp.zscale : 1.f;

        vk::Viewport viewport{};

        // https://gitlab.freedesktop.org/mesa/mesa/-/blob/209a0ed/src/amd/vulkan/radv_pipeline_graphics.c#L688-689
        // https://gitlab.freedesktop.org/mesa/mesa/-/blob/209a0ed/src/amd/vulkan/radv_cmd_buffer.c#L3103-3109
        // When the clip space is ranged [-1...1], the zoffset is centered.
        // By reversing the above viewport calculations, we get the following:
        if (regs.clipper_control.clip_space == AmdGpu::ClipSpace::MinusWToW) {
            viewport.minDepth = zoffset - zscale;
            viewport.maxDepth = zoffset + zscale;
        } else {
            viewport.minDepth = zoffset;
            viewport.maxDepth = zoffset + zscale;
        }

        if (!instance.IsDepthRangeUnrestrictedSupported()) {
            // Unrestricted depth range not supported by device. Restrict to valid range.
            viewport.minDepth = std::max(viewport.minDepth, 0.f);
            viewport.maxDepth = std::min(viewport.maxDepth, 1.f);
        }

        if (regs.IsClipDisabled()) {
            // In case if clipping is disabled we patch the shader to convert vertex position
            // from screen space coordinates to NDC by defining a render space as full hardware
            // window range [0..16383, 0..16383] and setting the viewport to its size.
            viewport.x = 0.f;
            viewport.y = 0.f;
            viewport.width = float(std::min<u32>(instance.GetMaxViewportWidth(), 16_KB));
            viewport.height = float(std::min<u32>(instance.GetMaxViewportHeight(), 16_KB));
        } else {
            const auto xoffset = vp_ctl.xoffset_enable ? vp.xoffset : 0.f;
            const auto xscale = vp_ctl.xscale_enable ? vp.xscale : 1.f;
            const auto yoffset = vp_ctl.yoffset_enable ? vp.yoffset : 0.f;
            const auto yscale = vp_ctl.yscale_enable ? vp.yscale : 1.f;

            viewport.x = xoffset - xscale;
            viewport.y = yoffset - yscale;
            viewport.width = xscale * 2.0f;
            viewport.height = yscale * 2.0f;
        }

        viewports.push_back(viewport);

        auto vp_scsr = scsr;
        if (regs.mode_control.vport_scissor_enable) {
            vp_scsr.top_left_x =
                std::max(vp_scsr.top_left_x, s16(regs.viewport_scissors[i].top_left_x));
            vp_scsr.top_left_y =
                std::max(vp_scsr.top_left_y, s16(regs.viewport_scissors[i].top_left_y));
            vp_scsr.bottom_right_x = std::min(AmdGpu::Scissor::Clamp(vp_scsr.bottom_right_x),
                                              regs.viewport_scissors[i].bottom_right_x);
            vp_scsr.bottom_right_y = std::min(AmdGpu::Scissor::Clamp(vp_scsr.bottom_right_y),
                                              regs.viewport_scissors[i].bottom_right_y);
        }
        scissors.push_back({
            .offset = {vp_scsr.top_left_x, vp_scsr.top_left_y},
            .extent = {vp_scsr.GetWidth(), vp_scsr.GetHeight()},
        });
    }

    if (viewports.empty()) {
        // Vulkan requires providing at least one viewport.
        constexpr vk::Viewport empty_viewport = {
            .x = -1.0f,
            .y = -1.0f,
            .width = 1.0f,
            .height = 1.0f,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        constexpr vk::Rect2D empty_scissor = {
            .offset = {0, 0},
            .extent = {1, 1},
        };
        viewports.push_back(empty_viewport);
        scissors.push_back(empty_scissor);
    }

    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetViewports(viewports);
    dynamic_state.SetScissors(scissors);
}

void Rasterizer::UpdateDepthStencilState() const {
    const auto& regs = liverpool->regs;
    auto& dynamic_state = scheduler.GetDynamicState();

    const auto depth_test_enabled =
        regs.depth_control.depth_enable && regs.depth_buffer.DepthValid();
    dynamic_state.SetDepthTestEnabled(depth_test_enabled);
    if (depth_test_enabled) {
        dynamic_state.SetDepthWriteEnabled(regs.depth_control.depth_write_enable &&
                                           !regs.depth_render_control.depth_clear_enable);
        dynamic_state.SetDepthCompareOp(LiverpoolToVK::CompareOp(regs.depth_control.depth_func));
    }

    const auto depth_bounds_test_enabled = regs.depth_control.depth_bounds_enable;
    dynamic_state.SetDepthBoundsTestEnabled(depth_bounds_test_enabled);
    if (depth_bounds_test_enabled) {
        dynamic_state.SetDepthBounds(regs.depth_bounds_min, regs.depth_bounds_max);
    }

    const auto depth_bias_enabled = regs.polygon_control.NeedsBias();
    dynamic_state.SetDepthBiasEnabled(depth_bias_enabled);
    if (depth_bias_enabled) {
        const bool front = regs.polygon_control.enable_polygon_offset_front;
        dynamic_state.SetDepthBias(
            front ? regs.poly_offset.front_offset : regs.poly_offset.back_offset,
            regs.poly_offset.depth_bias,
            (front ? regs.poly_offset.front_scale : regs.poly_offset.back_scale) / 16.f);
    }

    const auto stencil_test_enabled =
        regs.depth_control.stencil_enable && regs.depth_buffer.StencilValid();
    dynamic_state.SetStencilTestEnabled(stencil_test_enabled);
    if (stencil_test_enabled) {
        const StencilOps front_ops{
            .fail_op = LiverpoolToVK::StencilOp(regs.stencil_control.stencil_fail_front),
            .pass_op = LiverpoolToVK::StencilOp(regs.stencil_control.stencil_zpass_front),
            .depth_fail_op = LiverpoolToVK::StencilOp(regs.stencil_control.stencil_zfail_front),
            .compare_op = LiverpoolToVK::CompareOp(regs.depth_control.stencil_ref_func),
        };
        const StencilOps back_ops = regs.depth_control.backface_enable ? StencilOps{
            .fail_op = LiverpoolToVK::StencilOp(regs.stencil_control.stencil_fail_back),
            .pass_op = LiverpoolToVK::StencilOp(regs.stencil_control.stencil_zpass_back),
            .depth_fail_op = LiverpoolToVK::StencilOp(regs.stencil_control.stencil_zfail_back),
            .compare_op = LiverpoolToVK::CompareOp(regs.depth_control.stencil_bf_func),
        } : front_ops;
        dynamic_state.SetStencilOps(front_ops, back_ops);

        const bool stencil_clear = regs.depth_render_control.stencil_clear_enable;
        const auto front = regs.stencil_ref_front;
        const auto back =
            regs.depth_control.backface_enable ? regs.stencil_ref_back : regs.stencil_ref_front;
        // GCN REPLACE_OP writes DB_STENCILREFMASK.STENCILOPVAL, so a face whose stencil ops
        // include ReplaceOp takes its Vulkan reference from op_val.
        const auto& sc = regs.stencil_control;
        const auto uses_op_val = [](AmdGpu::StencilFunc fail, AmdGpu::StencilFunc zpass,
                                    AmdGpu::StencilFunc zfail) {
            return fail == AmdGpu::StencilFunc::ReplaceOp ||
                   zpass == AmdGpu::StencilFunc::ReplaceOp ||
                   zfail == AmdGpu::StencilFunc::ReplaceOp;
        };
        const bool front_op =
            uses_op_val(sc.stencil_fail_front, sc.stencil_zpass_front, sc.stencil_zfail_front);
        const bool back_op =
            regs.depth_control.backface_enable
                ? uses_op_val(sc.stencil_fail_back, sc.stencil_zpass_back, sc.stencil_zfail_back)
                : front_op;
        const auto ref_conflict = [](AmdGpu::CompareFunc func, const AmdGpu::StencilRefMask& ref) {
            return func != AmdGpu::CompareFunc::Always && func != AmdGpu::CompareFunc::Never &&
                   ref.stencil_test_val != ref.stencil_op_val;
        };
        if ((front_op && ref_conflict(regs.depth_control.stencil_ref_func, front)) ||
            (back_op && regs.depth_control.backface_enable &&
             ref_conflict(regs.depth_control.stencil_bf_func, back))) {
            LOG_WARNING(Render_Vulkan, "Stencil test requires test_val while ReplaceOp requires "
                                       "op_val; the stencil test will use op_val");
        }
        dynamic_state.SetStencilReferences(front_op ? front.stencil_op_val : front.stencil_test_val,
                                           back_op ? back.stencil_op_val : back.stencil_test_val);
        dynamic_state.SetStencilWriteMasks(!stencil_clear ? front.stencil_write_mask : 0U,
                                           !stencil_clear ? back.stencil_write_mask : 0U);
        dynamic_state.SetStencilCompareMasks(front.stencil_mask, back.stencil_mask);
    }
}

void Rasterizer::UpdatePrimitiveState(const bool is_indexed) const {
    const auto& regs = liverpool->regs;
    auto& dynamic_state = scheduler.GetDynamicState();

    const auto is_list_topology = [](const AmdGpu::PrimitiveType type) {
        const auto topology = LiverpoolToVK::PrimitiveType(type);
        return topology == vk::PrimitiveTopology::ePointList ||
               topology == vk::PrimitiveTopology::eLineList ||
               topology == vk::PrimitiveTopology::eTriangleList ||
               topology == vk::PrimitiveTopology::eLineListWithAdjacency ||
               topology == vk::PrimitiveTopology::eTriangleListWithAdjacency;
    };
    const auto is_patch_list_topology = [](const AmdGpu::PrimitiveType type) {
        // Quad and rect lists are emulated using tessellation.
        return type == AmdGpu::PrimitiveType::PatchPrimitive ||
               type == AmdGpu::PrimitiveType::QuadList || type == AmdGpu::PrimitiveType::RectList;
    };

    const auto prim_restart =
        (regs.enable_primitive_restart & 1) != 0 &&
        (instance.IsListRestartSupported() || !is_list_topology(regs.primitive_type)) &&
        (instance.IsPatchListRestartSupported() || !is_patch_list_topology(regs.primitive_type));
    ASSERT_MSG(!is_indexed || !prim_restart || regs.primitive_restart_index == 0xFFFF ||
                   regs.primitive_restart_index == 0xFFFFFFFF,
               "Primitive restart index other than -1 is not supported yet");

    const auto cull_mode = LiverpoolToVK::IsPrimitiveCulled(regs.primitive_type)
                               ? LiverpoolToVK::CullMode(regs.polygon_control.CullingMode())
                               : vk::CullModeFlagBits::eNone;
    const auto front_face = LiverpoolToVK::FrontFace(regs.polygon_control.front_face);

    dynamic_state.SetPrimitiveRestartEnabled(prim_restart);
    dynamic_state.SetRasterizerDiscardEnabled(regs.clipper_control.dx_rasterization_kill);
    dynamic_state.SetCullMode(cull_mode);
    dynamic_state.SetFrontFace(front_face);
}

void Rasterizer::UpdateRasterizationState() const {
    const auto& regs = liverpool->regs;
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetLineWidth(regs.line_control.Width());
}

void Rasterizer::UpdateColorBlendingState(const GraphicsPipeline* pipeline) const {
    const auto& regs = liverpool->regs;
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetBlendConstants(regs.blend_constants);
    dynamic_state.SetColorWriteMasks(pipeline->GetGraphicsKey().write_masks);
    dynamic_state.SetAttachmentFeedbackLoopEnabled(attachment_feedback_loop);
}

void Rasterizer::ScopeMarkerBegin(const std::string_view& str, bool from_guest) {
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
    });
}

void Rasterizer::ScopeMarkerEnd(bool from_guest) {
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.endDebugUtilsLabelEXT();
}

void Rasterizer::ScopedMarkerInsert(const std::string_view& str, bool from_guest) {
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.insertDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
    });
}

void Rasterizer::ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                         bool from_guest) {
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.insertDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
        .color = std::array<f32, 4>(
            {(f32)((color >> 16) & 0xff) / 255.0f, (f32)((color >> 8) & 0xff) / 255.0f,
             (f32)(color & 0xff) / 255.0f, (f32)((color >> 24) & 0xff) / 255.0f})});
}

std::thread::id Rasterizer::GetGpuCommandProcessorThread() {
    return liverpool->GetGpuCommandProcessorThread();
}

#ifdef __linux__
u32 Rasterizer::GetGpuCommandProcessorThreadId() {
    return liverpool->GetGpuCommandProcessorThreadId();
}
#endif

} // namespace Vulkan

namespace VideoCore {
/// The compute shader that last declared an output covering `address` (0 when none is known).
u64 WriterOfGuestMemory(u64 address, u64* base, u32* size) {
    const u32 total = Vulkan::g_cs_output_count.load(std::memory_order_relaxed);
    for (u32 i = 0; i < std::min<u32>(total, 8192); ++i) {
        const auto& e = Vulkan::g_cs_outputs[(total - 1 - i) % 8192];
        if (address >= e.base && address < e.base + e.size) {
            *base = e.base;
            *size = e.size;
            return e.hash;
        }
    }
    return 0;
}
} // namespace VideoCore

