#if defined(OPENOSC_APPLICATION) && defined(__ARM_FEATURE_DSP)
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include <arm_acle.h>

// LVGL's RGB565 mix quantizes alpha to (alpha + 4) >> 3. Thus 0..3 keep the
// background and 252..255 take the foreground, including masks below LV_OPA_MAX.
static inline __attribute__((always_inline)) void
blend_pixel(uint16_t *dest, unsigned alpha, uint16_t color, uint32_t prepared) {
    if (alpha >= 252)
        *dest = color;
    else if (alpha >= 4)
        LV_COLOR_MIX_16_TO_16_PREPARED(*dest, prepared, *dest, alpha);
}

static inline __attribute__((always_inline)) void
blend_pair(uint16_t *dest, const lv_opa_t *mask, uint16_t color, uint32_t prepared) {
    const uint32_t alpha = mask[0] | ((uint32_t)mask[1] << 16);
    // In each halfword, subtracting 4 puts the partial-alpha interval at
    // 0..247. Values below 4 wrap above it; values at least 252 follow it.
    if (__uqsub16(0x00f800f8U, __usub16(alpha, 0x00040004U))) {
        blend_pixel(dest, mask[0], color, prepared);
        blend_pixel(dest + 1, mask[1], color, prepared);
        return;
    }
    if (!(alpha & 0x00800080U)) return;
    const uint32_t foreground = color | ((uint32_t)color << 16);
    uint32_t background, result;
    __builtin_memcpy(&background, dest, sizeof(background));
    // USUB16 sets both GE bits of a lane to 1 for an opaque pixel, and SEL
    // takes both of that lane's color bytes together.
    __asm__("usub16 %0, %1, %2\n\tsel %0, %3, %4"
            : "=&r"(result) : "r"(alpha), "r"(0x00fc00fcU), "r"(foreground), "r"(background) : "cc");
    __builtin_memcpy(dest, &result, sizeof(result));
}

__attribute__((noinline)) bool
LV_DRAW_SW_RGB565_FILL_HOOK(lv_layer_t *layer, const lv_area_t *area, const lv_draw_sw_blend_dsc_t *dsc) {
    const unsigned width = area->x2 - area->x1 + 1;
    if (width > 4 || dsc->opa < LV_OPA_MAX) return false;

    const lv_draw_buf_t *buffer = layer->draw_buf;
    const unsigned x = area->x1 - layer->buf_area.x1;
    const unsigned y = area->y1 - layer->buf_area.y1;
    const unsigned height = area->y2 - area->y1 + 1;
    const unsigned stride = buffer->header.stride;
    if (buffer->header.cf != LV_COLOR_FORMAT_RGB565 || !buffer->data || x >= buffer->header.w || width > buffer->header.w - x ||
        y >= buffer->header.h || height > buffer->header.h - y || stride % 2)
        return false;

    uint8_t *row = buffer->data + y * stride + x * sizeof(uint16_t);
    const uint16_t color = lv_color_to_u16(dsc->color);
    if (!dsc->mask_buf || dsc->mask_res == LV_DRAW_SW_MASK_RES_FULL_COVER) {
        for (unsigned i = 0; i < height; ++i, row += stride) {
            uint16_t *dest = (uint16_t *)row;
            dest[0] = color;
            if (width >= 2) dest[1] = color;
            if (width >= 3) dest[2] = color;
            if (width == 4) dest[3] = color;
        }
        return true;
    }

    const int32_t mask_stride = dsc->mask_stride ? dsc->mask_stride : lv_area_get_width(dsc->mask_area);
    const int32_t mask_offset = (area->y1 - dsc->mask_area->y1) * mask_stride + (area->x1 - dsc->mask_area->x1);
    const lv_opa_t *mask = dsc->mask_buf + mask_offset;
    const uint32_t prepared = LV_COLOR_MIX_16_PREPARE(color);
    for (unsigned i = 0; i < height; ++i, row += stride, mask += mask_stride) {
        uint16_t *dest = (uint16_t *)row;
        if (width == 1) {
            blend_pixel(dest, mask[0], color, prepared);
        }
        else {
            blend_pair(dest, mask, color, prepared);
            if (width == 3)
                blend_pixel(dest + 2, mask[2], color, prepared);
            else if (width == 4)
                blend_pair(dest + 2, mask + 2, color, prepared);
        }
    }
    return true;
}
#endif
