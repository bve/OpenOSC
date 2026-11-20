#include "openosc/ui/scope/painter.hpp"
#include "raster_line.hpp"
#include "trace_geometry.hpp"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include <cstring>
#if defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
#endif

namespace openosc::plot {
namespace {
constexpr int Radius = 6;
constexpr unsigned RowWords = (MaximumWidth + 31) / 32;
constexpr unsigned RingRows = 8, BandRows = 4;
static_assert(RingRows > Radius && !(RingRows & (RingRows - 1)));
// Only the synchronous software draw callback uses this workspace. It is
// cleared per channel and never referenced by a queued task or DMA2D.
static_assert(LV_USE_OS == LV_OS_NONE);
struct GlowScratch {
    uint32_t silhouette[MaximumHeight][RowWords];
    uint32_t rings[Radius][RingRows][RowWords];
    uint8_t band[BandRows * MaximumWidth];
};
#if defined(OPENOSC_APPLICATION)
__attribute__((section(".scope_glow"), aligned(8)))
#endif
GlowScratch scratch;

class Silhouette {
public:
    Silhouette(int width, int height) : right_(width - 1), bottom_(height - 1), bounds_{width, height, -1, -1} {
        std::memset(scratch.silhouette, 0, height * sizeof(scratch.silhouette[0]));
    }

    void segment(int x1, int y1, int x2, int y2) {
        // Match the raw LVGL horizontal and vertical strokes, including the
        // one-pixel segments used for Points.
        if (x1 == x2 && y1 != y2) --(y1 > y2 ? y1 : y2);
        if (y1 == y2 && x1 != x2) --(x1 > x2 ? x1 : x2);
        if (!clipLine(x1, y1, x2, y2, right_, bottom_)) return;
        bounds_.x1 = std::min<int>({bounds_.x1, x1, x2});
        bounds_.x2 = std::max<int>({bounds_.x2, x1, x2});
        bounds_.y1 = std::min<int>({bounds_.y1, y1, y2});
        bounds_.y2 = std::max<int>({bounds_.y2, y1, y2});
        rasterLine(x1, y1, x2, y2, [](unsigned x, unsigned y) {
            scratch.silhouette[y][x / 32] |= 1U << (x % 32);
        });
    }

    const uint32_t *row(int y) const { return y >= 0 && y <= bottom_ ? scratch.silhouette[y] : empty_; }
    const lv_area_t &bounds() const { return bounds_; }

private:
    int right_, bottom_;
    lv_area_t bounds_;
    static constexpr uint32_t empty_[RowWords]{};
};

class GlowPath {
public:
    explicit GlowPath(Silhouette &target) : target_(target) {}
    void vertex(int x, int y) {
        if (x_ != Missing && (x != x_ || y != y_)) target_.segment(x_, y_, x, y);
        x_ = x;
        y_ = y;
    }
    void gap() { x_ = Missing; }

private:
    Silhouette &target_;
    int x_ = Missing, y_ = 0;
};

// Six packed dilations, alternating a square and a cross. Their contours
// approximate circles. Only the rows needed by the next stage are retained.
class Halo {
public:
    Halo(unsigned words, unsigned opacity) : words_(words) {
        std::memset(scratch.rings, 0, sizeof(scratch.rings));
        constexpr uint8_t falloff[Radius] = {94, 78, 58, 39, 23, 11};
        for (unsigned i = 0; i < Radius; ++i)
            alpha_[i] = uint8_t((falloff[i] * opacity + 127) / 255);
    }

    void feed(const Silhouette &shape, int y) {
        for (unsigned r = 0; r < Radius; ++r) {
            const int at = y - int(r) - 1;
            const uint32_t *a = r ? row(r-1, at-1) : shape.row(at-1);
            const uint32_t *b = r ? row(r-1, at) : shape.row(at);
            const uint32_t *c = r ? row(r-1, at+1) : shape.row(at+1);
            if (r & 1) grow<false>(row(r, at), a, b, c);
            else grow<true>(row(r, at), a, b, c);
        }
    }

    void mask(uint8_t *output, int y, unsigned left, unsigned right) {
        std::memset(output, 0, right - left + 1);
        const unsigned first = left / 32, last = right / 32;
        for (unsigned w = first; w <= last; ++w) {
            uint32_t remaining = row(Radius-1, y)[w];
            if (w == first) remaining &= UINT32_MAX << (left % 32);
            if (w == last) remaining &= UINT32_MAX >> (31 - right % 32);
            for (unsigned r = 0; r < Radius && remaining; ++r) {
                const uint32_t pixels = row(r, y)[w] & remaining;
                remaining ^= pixels;
                write(output, left, right, w * 32, pixels, alpha_[r]);
            }
        }
    }

private:
    static void write(uint8_t *out, unsigned left, unsigned right, unsigned base, uint32_t pixels, uint8_t alpha) {
        if (pixels == UINT32_MAX) {
            std::memset(out + (base - left), alpha, 32);
            return;
        }
        while (pixels) {
            const unsigned bit = __builtin_ctz(pixels);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            const unsigned group = bit & ~3U;
            const unsigned x = base + group;
            const uint32_t selected = (pixels >> group) & 15U;
            if ((selected & (selected - 1)) && x >= left && x + 3 <= right) {
                // Spread the four flags into byte lanes. Rings are disjoint, so
                // the new alpha is added only to lanes that still contain zero.
                const uint32_t lanes = (selected * 0x00204081U) & 0x01010101U;
                uint32_t ink;
                std::memcpy(&ink, out + (x - left), sizeof(ink));
#if defined(__ARM_FEATURE_DSP)
                ink = __uadd8(ink, lanes * alpha);
#else
                ink |= lanes * alpha;
#endif
                std::memcpy(out + (x - left), &ink, sizeof(ink));
                pixels &= ~(15U << group);
                continue;
            }
#else
            (void)right;
#endif
            out[base + bit - left] = alpha;
            pixels &= pixels - 1;
        }
    }

    uint32_t *row(unsigned r, int y) { return scratch.rings[r][unsigned(y) % RingRows]; }

    template <bool Square>
    void grow(uint32_t *out, const uint32_t *top, const uint32_t *center, const uint32_t *bottom) const {
        uint32_t previous = 0, middle = Square ? top[0] | center[0] | bottom[0] : center[0];
        for (unsigned word = 0; word < words_; ++word) {
            uint32_t next = 0;
            if (word + 1 < words_)
                next = Square ? top[word+1] | center[word+1] | bottom[word+1] : center[word+1];
            const uint32_t vertical = Square ? middle : top[word] | middle | bottom[word];
            out[word] = vertical | (middle << 1) | (middle >> 1) | (previous >> 31) | (next << 31);
            previous = middle;
            middle = next;
        }
    }

    unsigned words_;
    uint8_t alpha_[Radius];
};

struct GlowTask {
    lv_draw_sw_callback_dsc_t descriptor{};
    const Model *model;
    lv_color_t color;
    unsigned channel;

    static void render(lv_draw_task_t *task, const lv_draw_sw_callback_dsc_t *descriptor) {
        const auto &self = *reinterpret_cast<const GlowTask *>(descriptor);
        const auto &model = *self.model;
        const auto &options = model.options();
        const auto &view = model.viewport();
        const auto &plot = task->area;
        Silhouette shape(view.width, view.height);
        GlowPath main(shape), extrema(shape);
        if (options.mode == Mode::XY)
            xyGeometry(model.storage().xy, model.xyCount(), options.style, main);
        else
            traceGeometry(model.storage().traces[self.channel], view.width, options.style, false, main, extrema);
        const auto &ink = shape.bounds();
        if (ink.x1 > ink.x2) return;
        const int left = std::max<int>({ink.x1 - Radius, task->clip_area.x1 - plot.x1, 0});
        const int right = std::min<int>({ink.x2 + Radius, task->clip_area.x2 - plot.x1, int(view.width) - 1});
        const int top = std::max<int>({ink.y1 - Radius, task->clip_area.y1 - plot.y1, 0});
        const int bottom = std::min<int>({ink.y2 + Radius, task->clip_area.y2 - plot.y1, int(view.height) - 1});
        if (left > right || top > bottom) return;

        Halo halo((view.width + 31) / 32, options.channels[self.channel].opacity);
        auto *rows = scratch.band;
        const int stride = right - left + 1;
        int bandStart = top;
        for (int y = top - Radius; y <= bottom + Radius; ++y) {
            halo.feed(shape, y);
            const int visible = y - Radius;
            if (visible < top) continue;
            halo.mask(rows + (visible - bandStart) * stride, visible, left, right);
            if (visible - bandStart + 1 < int(BandRows) && visible != bottom) continue;
            const lv_area_t band{plot.x1 + left, plot.y1 + bandStart, plot.x1 + right, plot.y1 + visible};
            lv_draw_sw_blend_dsc_t blend{};
            blend.blend_area = &band;
            blend.mask_area = &band;
            blend.mask_buf = rows;
            blend.mask_stride = stride;
            blend.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
            blend.opa = LV_OPA_COVER;
            blend.color = self.color;
            lv_draw_sw_blend(task, &blend);
            bandStart = visible + 1;
        }
    }
};
} // namespace

void Painter::glow(unsigned channel) {
    const auto &options = model_.options();
    if (!options.channels[channel].opacity ||
        (options.mode != Mode::XY && !model_.storage().traces[channel].enabled)) return;
    // RenderIdle services I/O only. The projected model remains stable until
    // the queued tasks of this refresh have completed.
    GlowTask task{{}, &model_, lv_color_hex(palette_.channels[channel]), channel};
    task.descriptor.base.dsc_size = sizeof(task);
    task.descriptor.render = GlowTask::render;
    lv_draw_sw_callback(layer_, &area_, &task.descriptor);
    ++model_.statistics().drawBatches;
}
} // namespace openosc::plot
