#pragma once
#define LV_CONF_H
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_OS LV_OS_NONE
#define LV_MEM_SIZE (60 * 1024U) /* Peak use 42 KiB in the native UI review; AXI also holds the 512 KiB arena. */
// Temporary LVGL image/layer buffers can be handed to DMA2D as well as objects.
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY __attribute__((section(".lvgl_heap"), aligned(32)))
#define LV_DEF_REFR_PERIOD 6
#define LV_USE_DRAW_DMA2D 1
#define LV_DRAW_DMA2D_HAL_INCLUDE "stm32h7xx.h"
#define LV_USE_DRAW_DMA2D_INTERRUPT 1
// OpenOSC: DMA2D runs asynchronously without an OS. A transfer is started and its task stays in
// flight while the software unit draws independent tasks; completion is the hardware START bit
// (the IRQ clears flags, records errors and requests a dispatch). A transfer longer than the
// timeout is aborted and its task fails. While nothing can be dispatched, LVGL calls the idle
// hook: the application services acquisition and USB there (no LVGL calls).
#define LV_DRAW_DMA2D_ASYNC_NO_OS 1
#define LV_DRAW_DMA2D_TIMEOUT_MS 50
#define LV_DRAW_DMA2D_RESET_HOOK openosc_dma2d_reset // A transfer ABORT did not stop: reset the DMA2D.
#define LV_DRAW_DISPATCH_IDLE_HOOK openosc_render_idle
// AXI and D2 SRAM (framebuffers, LVGL heap and draw buffers) are not cacheable (DmaMemory's MPU
// regions); images DMA2D reads are in flash: no cache maintenance per transfer line.
#define LV_DRAW_DMA2D_CACHE 0
// Rows of a skewed line's mask per blend (a steep 1..3 px stroke is one to a few bytes wide).
#define LV_DRAW_SW_LINE_MASK_BYTES 320
#if defined(__ARM_FEATURE_DSP)
#define LV_DRAW_SW_RGB565_FILL_HOOK openosc_rgb565_narrow_fill
#endif
#define LV_DRAW_QUEUE_LIMIT 16 // Tasks queued behind a transfer before the tree waits (~4 KiB of the LVGL heap).
#ifndef __ASSEMBLER__
#ifdef __cplusplus
extern "C" {
#endif
void openosc_render_idle(void);
void openosc_dma2d_reset(void);
#ifdef __cplusplus
}
#endif
#endif
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(openosc_ui_12)
#define LV_FONT_DEFAULT &openosc_ui_12
#define LV_USE_BMP 1
#define LV_USE_FS_FATFS 1
#define LV_FS_FATFS_LETTER 'S'
#define LV_FS_FATFS_CACHE_SIZE 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
