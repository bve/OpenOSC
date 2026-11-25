// Density grading (GitHub #85): a graded channel's levels (Persistence, four bits a pixel, a log
// count) drawn straight into the layer's RGB565 buffer from a palette made once a draw, scaled to the
// plane's peak: heat colours from cool (level 1) to warm (the peak, the most frequent path), or one
// colour's intensity. One synchronous software task a channel,
// as the glow: no LVGL task a pixel or a span, and no DMA2D register use outside LVGL's own queue.
#include "openosc/ui/scope/painter.hpp"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace openosc::plot {
namespace {
static_assert(LV_USE_OS == LV_OS_NONE);
constexpr uint16_t rgb565(uint32_t rgb) {
    return uint16_t(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
}
constexpr uint32_t mix(uint32_t a, uint32_t b, unsigned t, unsigned span) { // a..b at t/span, per 8-bit channel.
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int x = int((a >> shift) & 255), y = int((b >> shift) & 255);
        out |= uint32_t(x + (y - x) * int(t) / int(span)) << shift;
    }
    return out;
}
constexpr auto heatPalette() {
    // Cool to warm: blue, cyan, green, yellow, red at levels 1, 4.5, 8, 11.5, 15.
    constexpr uint32_t heat[] = {0x1d4ed8, 0x06b6d4, 0x22c55e, 0xfacc15, 0xef4444};
    std::array<uint16_t, 16> colors{};
    for (unsigned level = 1; level < colors.size(); ++level) {
        const unsigned t = (level - 1) * 4;
        colors[level] = rgb565(mix(heat[t / 14], heat[std::min(4U, t / 14 + 1)], t % 14, 14));
    }
    return colors;
}
struct DensityTask {
    lv_draw_sw_callback_dsc_t descriptor{};
    const Persistence *plane = nullptr;
    uint16_t width = 0, height = 0;
    uint8_t channel = 0;
    bool colors = false, opaque = false; // Opaque: Colors at full opacity, no destination read at all.
    uint8_t alpha[16]{}; // Level 1..15; 0 is never drawn (alpha 0 leaves the pixel as it is).
    // Colors: each level's colour. Mono: each level's pixel over the plot's background as blend()
    // makes it (level 0: the background), exact wherever the layer holds that background.
    uint16_t ink[16]{};
    uint16_t mono = 0; // Mono's colour.

    // Out of line: done before the task is queued, its frame never under render's. The brightness
    // (GitHub #86) shades the colours here towards the plot's background, so Colors stays opaque
    // stores and Mono its blends: nothing a pixel more, and at 100 % the colours are as before.
    [[gnu::noinline]] void palette(const ChannelOptions &channel, uint32_t rgb, unsigned peak, uint32_t plot) {
        static constexpr auto Heat = heatPalette();
        const unsigned opacity = channel.opacity;
        colors = channel.grade == Grade::Colors;
        mono = rgb565(ColorShade::toward(rgb, plot, channel.brightness));
        const uint8_t heat = uint8_t((channel.brightness * 255U + 50) / 100); // Colors: LVGL's mix towards the background.
        const uint16_t base = rgb565(plot);
        opaque = colors && opacity == 255;
        for (unsigned level = 1; level < 16; ++level) {
            // Levels 1..peak over the whole scale, the same number of colours a doubling of the count.
            const unsigned shown = level >= peak ? 15 : 1 + (level - 1) * 14 / (peak - 1);
            // Mono: the colour's intensity from a faint 48 to full; Colors: opaque, as the trace.
            const unsigned shade = colors ? 255 : 48 + (shown - 1) * 207 / 14;
            alpha[level] = uint8_t(shade * opacity / 255);
            ink[level] = lv_color_16_16_mix_inlined(Heat[shown], base, heat); // 255: Heat[shown] itself.
        }
        if (!colors)
            for (unsigned level = 0; level < 16; ++level) ink[level] = blend(level, base);
    }
    // LVGL's own mix: alpha 0 keeps the pixel, 255 takes the colour. A leaf call, as before: the
    // pairs over the background, the most common case, never need it.
    [[gnu::noinline]] uint16_t blend(unsigned level, uint16_t pixel) const {
        return lv_color_16_16_mix_inlined(colors ? ink[level] : mono, pixel, alpha[level]);
    }
    // A word whose eight pixels are all inside the clip, a pair of pixels a step (both 0: none).
    template <bool Opaque> [[gnu::always_inline]] void word(uint16_t *dst, uint32_t levels, uint32_t background) const {
        const unsigned level = levels & 15U;
        if (levels == level * 0x11111111U) {
            // Dense, steady traces fill many complete words with one level.
            if (Opaque || alpha[level] == 255) {
                const uint16_t pixel = colors ? ink[level] : mono;
                const uint32_t pair = uint32_t(pixel) * 0x00010001U;
                for (unsigned i = 0; i < 8; i += 2) std::memcpy(dst + i, &pair, sizeof pair);
                return;
            }
        }
        for (unsigned i = 0; i < 8; i += 2, levels >>= 8) {
            if (!(levels & 0xffU)) continue;
            const unsigned a = levels & 15U, b = (levels >> 4) & 15U;
            uint32_t pair;
            if (Opaque) {
                if (!a) { dst[i + 1] = ink[b]; continue; }
                if (!b) { dst[i] = ink[a]; continue; }
                pair = ink[a] | uint32_t(ink[b]) << 16;
            } else {
                std::memcpy(&pair, dst + i, sizeof pair);
                if (!colors && pair == background) pair = ink[a] | uint32_t(ink[b]) << 16;
                else pair = blend(a, uint16_t(pair)) | uint32_t(blend(b, uint16_t(pair >> 16))) << 16;
            }
            std::memcpy(dst + i, &pair, sizeof pair);
        }
    }

    // One instance a task: Opaque stores colours, else each pixel blends (Mono: over the background in pairs).
    template <bool Opaque> static void render(lv_draw_task_t *task, const lv_draw_sw_callback_dsc_t *descriptor) {
        const auto &self = *reinterpret_cast<const DensityTask *>(descriptor);
        auto *layer = task->target_layer;
        auto *buffer = layer->draw_buf;
        if (!buffer || buffer->header.cf != LV_COLOR_FORMAT_RGB565) return;
        const auto &plot = task->area;
        const auto &bounds = layer->buf_area;
        const auto &clip = task->clip_area;
        const int left = std::max<int>(std::max<int>(clip.x1, bounds.x1) - plot.x1, 0);
        const int right = std::min<int>(std::min<int>(clip.x2, bounds.x2) - plot.x1, int(self.width) - 1);
        const int top = std::max<int>(std::max<int>(clip.y1, bounds.y1) - plot.y1, 0);
        const int bottom = std::min<int>(std::min<int>(clip.y2, bounds.y2) - plot.y1, int(self.height) - 1);
        if (left > right || top > bottom) return;
        constexpr unsigned PerWord = Persistence::FinitePixelsPerWord;
        const unsigned firstWord = unsigned(left) / PerWord, endWord = unsigned(right) / PerWord + 1;
        const uint32_t background = uint32_t(self.ink[0]) * 0x00010001U; // Mono only.
        // The clip's first pixel of the top row, then one buffer stride a row.
        auto *line = static_cast<uint8_t *>(lv_draw_buf_goto_xy(buffer, uint32_t(plot.x1 + left - bounds.x1),
                                                                uint32_t(plot.y1 + top - bounds.y1)));
        const uint32_t stride = buffer->header.stride;
        for (int y = top; y <= bottom; ++y, line += stride) {
            const uint32_t *row = self.plane->packedRow(self.channel, unsigned(y));
            if (!row) continue;
            auto *pixels = reinterpret_cast<uint16_t *>(line);
            for (unsigned w = firstWord; w < endWord; ++w) {
                uint32_t levels = row[w];
                if (!levels) continue;
                const int x0 = int(w * PerWord);
                if (x0 >= left && x0 + int(PerWord) - 1 <= right) {
                    self.template word<Opaque>(pixels + (x0 - left), levels, background);
                    continue;
                }
                while (levels) { // A word the clip cuts: each pixel inside it alone.
                    const unsigned nibble = unsigned(__builtin_ctz(levels)) / 4;
                    const unsigned level = (levels >> (nibble * 4)) & 15U;
                    levels &= ~(15U << (nibble * 4));
                    const int x = x0 + int(nibble);
                    if (x < left || x > right) continue;
                    auto &pixel = pixels[x - left];
                    pixel = Opaque ? self.ink[level] : self.blend(level, pixel);
                }
            }
        }
    }
};
} // namespace

bool Painter::densityShown(unsigned c) const {
    if (c >= 2 || layer_->color_format != LV_COLOR_FORMAT_RGB565) return false;
    const auto &o = model_.options().channels[c];
    const auto &plane = model_.storage().persistence;
    return o.visible && o.opacity && plane.graded(c) && plane.hasPixels(c);
}
void Painter::density(unsigned c) {
    const auto &o = model_.options().channels[c];
    const auto &view = model_.viewport();
    DensityTask task;
    task.plane = &model_.storage().persistence;
    task.channel = uint8_t(c);
    task.width = uint16_t(view.width);
    task.height = uint16_t(view.height);
    const uint32_t mono = o.gradeColor == ChannelOptions::InheritColor ? palette_.channel(c) : ColorCatalog::swatch(o.gradeColor).rgb;
    task.palette(o, mono, model_.storage().persistence.peak(c), palette_.background);
    task.descriptor.base.dsc_size = sizeof(task);
    task.descriptor.render = task.opaque ? DensityTask::render<true> : DensityTask::render<false>;
    lv_draw_sw_callback(layer_, &area_, &task.descriptor);
    ++model_.statistics().drawBatches;
}
} // namespace openosc::plot
