#pragma once

#include "cmsis_compiler.h"
#include "pins.h"
#include "driver/gpio.h"
#include "esp_cpu.h"
#include "hal/gpio_ll.h"
#include <stdint.h>
#include <string.h>

#define CPU_CLOCK               160000000U
#define IO_PORT_WRITE_CYCLES    2U
#define DAP_SWD                 1
#define DAP_JTAG                0
#define DAP_JTAG_DEV_CNT         0
#define DAP_DEFAULT_PORT        1U
#define DAP_DEFAULT_SWJ_CLOCK   500000U
#define DAP_PACKET_SIZE         512U
#define DAP_PACKET_COUNT        1U
#define SWO_UART                0
#define SWO_MANCHESTER          0
#define SWO_STREAM              0
#define DAP_UART                0
#define DAP_UART_USB_COM_PORT   0
#define TIMESTAMP_CLOCK         0U
#define DELAY_SLOW_CYCLES       1U
#define DAP_CUSTOM_DELAY        1

__STATIC_FORCEINLINE void PIN_DELAY_SLOW(uint32_t cycles) {
    const uint32_t start = esp_cpu_get_cycle_count();
    while ((uint32_t)(esp_cpu_get_cycle_count() - start) < cycles) {}
}
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void) { return gpio_ll_get_level(&GPIO, COMPANION_SWCLK); }
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void) { gpio_ll_set_level(&GPIO, COMPANION_SWCLK, 1); }
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void) { gpio_ll_set_level(&GPIO, COMPANION_SWCLK, 0); }
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void) { return gpio_ll_get_level(&GPIO, COMPANION_SWDIO); }
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void) { gpio_ll_set_level(&GPIO, COMPANION_SWDIO, 1); }
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void) { gpio_ll_set_level(&GPIO, COMPANION_SWDIO, 0); }
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void) { return PIN_SWDIO_TMS_IN(); }
__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit) { gpio_ll_set_level(&GPIO, COMPANION_SWDIO, bit & 1U); }
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void) { gpio_ll_output_enable(&GPIO, COMPANION_SWDIO); }
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void) { gpio_ll_output_disable(&GPIO, COMPANION_SWDIO); }
__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void) { return gpio_ll_get_level(&GPIO, COMPANION_NRST); }
__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit) { gpio_ll_set_level(&GPIO, COMPANION_NRST, bit & 1U); }
__STATIC_FORCEINLINE uint32_t PIN_TDI_IN(void) { return 0; }
__STATIC_FORCEINLINE uint32_t PIN_TDO_IN(void) { return 0; }
__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN(void) { return 1; }
__STATIC_FORCEINLINE void PIN_TDI_OUT(uint32_t bit) { (void)bit; }
__STATIC_FORCEINLINE void PIN_nTRST_OUT(uint32_t bit) { (void)bit; }
__STATIC_FORCEINLINE uint32_t TIMESTAMP_GET(void) { return esp_cpu_get_cycle_count(); }
__STATIC_FORCEINLINE void LED_CONNECTED_OUT(uint32_t state) { (void)state; }
__STATIC_FORCEINLINE void LED_RUNNING_OUT(uint32_t state) { (void)state; }

// Keep SWCLK high outside transfers to mark the pins as owned by the probe.
__STATIC_FORCEINLINE void PORT_OFF(void) {
    PIN_SWCLK_TCK_SET();
    PIN_SWDIO_OUT_DISABLE();
    PIN_nRESET_OUT(1);
}
__STATIC_FORCEINLINE void PORT_SWD_SETUP(void) {
    PIN_SWCLK_TCK_SET();
    PIN_SWDIO_TMS_SET();
    PIN_SWDIO_OUT_ENABLE();
}
__STATIC_FORCEINLINE void PORT_JTAG_SETUP(void) {}
__STATIC_FORCEINLINE void DAP_SETUP(void) { PORT_OFF(); }
__STATIC_FORCEINLINE uint8_t RESET_TARGET(void) { return 0; } // Reset is performed through SWD by OpenOCD.

__STATIC_FORCEINLINE uint8_t DAP_GetVendorString(char *s) { memcpy(s, "OpenOSC", sizeof("OpenOSC")); return sizeof("OpenOSC"); }
__STATIC_FORCEINLINE uint8_t DAP_GetProductString(char *s) { memcpy(s, "Wireless SWD", sizeof("Wireless SWD")); return sizeof("Wireless SWD"); }
__STATIC_FORCEINLINE uint8_t DAP_GetSerNumString(char *s) { (void)s; return 0; }
__STATIC_FORCEINLINE uint8_t DAP_GetProductFirmwareVersionString(char *s) { (void)s; return 0; }
__STATIC_FORCEINLINE uint8_t DAP_GetTargetDeviceVendorString(char *s) { (void)s; return 0; }
__STATIC_FORCEINLINE uint8_t DAP_GetTargetDeviceNameString(char *s) { (void)s; return 0; }
__STATIC_FORCEINLINE uint8_t DAP_GetTargetBoardVendorString(char *s) { (void)s; return 0; }
__STATIC_FORCEINLINE uint8_t DAP_GetTargetBoardNameString(char *s) { (void)s; return 0; }
