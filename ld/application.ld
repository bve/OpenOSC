ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08040400, LENGTH = __openosc_flash_end - 0x08040400
    RAM (rw) : ORIGIN = 0x20000000, LENGTH = 0x1FF00
    AXI (rw) : ORIGIN = 0x24000000, LENGTH = 1M
    AHB (rw) : ORIGIN = 0x30000000, LENGTH = 128K
    SRAM4 (rw) : ORIGIN = 0x38000000, LENGTH = 32K
    /* ITCM (64 KiB at 0; the CPU, the debugger and MDMA reach it, DMA1/2 and DMA2D do not): the
       decoder workspace and, in firmware_trace builds, the USB trace ring (sections below). The
       first 1 KiB stays unused, so no object has the null address. */
    ITCM (rw) : ORIGIN = 0x00000400, LENGTH = 63K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .bootstrap : { KEEP(*(.bootstrap*)) . = ALIGN(4); } > FLASH
    .text : {
        _stext = .;
        *(.text*) *(.rodata*)
        . = ALIGN(4); KEEP(*(.init)) KEEP(*(.fini))
        __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
        . = ALIGN(4); _etext = .;
    } > FLASH
    _sitext = _stext;
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
    /* The runtime vector table (VTOR) first, at DTCM's 1 KiB-aligned start: inside .bss its alignment
       left holes. NOLOAD, not zeroed: Reset_Handler writes all of it. */
    .runtime_vectors (NOLOAD) : { KEEP(*(.runtime_vectors)) } > RAM
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > RAM
    .framebuffer (NOLOAD) : { . = ALIGN(64); KEEP(*(.framebuffer)) . = ALIGN(64); } > AXI
    .dma (NOLOAD) : { . = ALIGN(32); *(.dma .dma.*) . = ALIGN(32); } > AXI
    .dma_data : {
        . = ALIGN(32); _sdma_data = .; KEEP(*(.dma_data))
        . = ALIGN(32); _edma_data = .;
    } > AXI AT> FLASH
    _sidma_data = LOADADDR(.dma_data);
    .scope_plot (NOLOAD) : { . = ALIGN(32); KEEP(*(.scope_plot)) . = ALIGN(32); } > AXI
    .lvgl_heap (NOLOAD) : { . = ALIGN(32); KEEP(*(.lvgl_heap)) . = ALIGN(32); } > AXI
    .image_preview (NOLOAD) : { . = ALIGN(32); KEEP(*(.image_preview)) . = ALIGN(32); } > AXI
    /* Aligned by their 32-byte arrays: 8-byte DMA bursts then never cross a 1 KiB boundary. */
    .capture_trial (NOLOAD) : { KEEP(*(.capture_trial)) } > AXI
    /* Record banks 1-3 (bank 0 is .acquisition in AHB): one block, the last in AXI. */
    .capture_arena (NOLOAD) : { KEEP(*(.capture_arena)) } > AXI
    /* MDMA linked-list nodes of a split record (HistoryCopy): the MDMA cannot fetch them from DTCM. */
    .capture_nodes (NOLOAD) : { . = ALIGN(8); KEEP(*(.capture_nodes)) } > AXI
    .acquisition (NOLOAD) : { . = ALIGN(32); KEEP(*(.acquisition)) } > AHB
    .generator (NOLOAD) : { . = ALIGN(32); KEEP(*(.generator)) } > SRAM4
    .settings_workspace (NOLOAD) : { . = ALIGN(32); KEEP(*(.settings_workspace)) . = ALIGN(32); } > SRAM4
    . = ALIGN(8); _end = .; PROVIDE(end = .);
    /* ITCM (CPU and MDMA only, no DMA1/2 or DMA2D): the protocol decoder's workspace (its job, lease and
       two result sets) and, in OPENOSC_USB_TRACE builds, the trace ring. Reset_Handler enables ITCM and
       zeroes both in 64-bit ECC words before any constructor runs (the job is built in place later). */
    .decode_workspace (NOLOAD) : { . = ALIGN(8); _sdecode_workspace = .; KEEP(*(.decode_workspace)) . = ALIGN(8); _edecode_workspace = .; } > ITCM
    .usb_trace (NOLOAD) : { _susb_trace = .; KEEP(*(.usb_trace)) . = ALIGN(8); _eusb_trace = .; } > ITCM
    /* Software drawing's reusable silhouette, initialized in 64-bit ECC words at reset. */
    .scope_glow (NOLOAD) : { . = ALIGN(8); _sscope_glow = .; KEEP(*(.scope_glow)) . = ALIGN(8); _escope_glow = .; } > ITCM
    /* The frequency response session (GitHub #72, both boards: its settings, sweep and 256 points), zeroed
       at reset in 64-bit ECC words, built in place on first use. */
    .bode (NOLOAD) : { . = ALIGN(8); _sbode = .; KEEP(*(.bode)) . = ALIGN(8); _ebode = .; } > ITCM
    /* SCPI: WAV:MEAS? RECord's one record-readouts job (any session) and two MMEM:DATA? 512 B stages; zeroed at reset. */
    .scpi_measure (NOLOAD) : { . = ALIGN(8); _sscpi_measure = .; KEEP(*(.scpi_measure)) . = ALIGN(8); _escpi_measure = .; } > ITCM
    /* The DMM session recorder (GitHub #144: its file, record and accumulators), zeroed at reset, built in place on first use. */
    .dmm_log (NOLOAD) : { . = ALIGN(8); _sdmm_log = .; KEEP(*(.dmm_log)) . = ALIGN(8); _edmm_log = .; } > ITCM
    /DISCARD/ : { *(.comment) *(.note*) }
    ASSERT(_ebss + 49152 < _estack, "Application leaves less than 48 KiB stack")
    ASSERT(ADDR(.runtime_vectors) == ORIGIN(RAM) && SIZEOF(.runtime_vectors) == 1024, "Runtime vectors must open DTCM")
    ASSERT(ADDR(.framebuffer) == 0x24000000, "Panel framebuffer address changed")
    ASSERT(SIZEOF(.dma_data) >= 1024, "Initialized USB DMA buffers were not linked")
    ASSERT(ADDR(.dma_data) + SIZEOF(.dma_data) <= 0x24080000, "USB DMA buffers exceed their qualified AXI region")
    ASSERT(SIZEOF(.lvgl_heap) == 61440, "LVGL DMA2D workspace must reside in AXI SRAM")
    ASSERT(SIZEOF(.capture_arena) == 393216, "Record banks 1-3 must be 384 KiB of AXI SRAM")
    ASSERT(SIZEOF(.acquisition) == 131072, "Record bank 0 must be the 128 KiB AHB SRAM")
    ASSERT(SIZEOF(.decode_workspace) <= 40960, "Decoder workspace exceeds its 40 KiB plan")
    ASSERT(SIZEOF(.decode_workspace) + SIZEOF(.usb_trace) + SIZEOF(.scope_glow) + SIZEOF(.bode) + SIZEOF(.scpi_measure) + SIZEOF(.dmm_log) <= LENGTH(ITCM),
           "ITCM: decoder, trace ring, Glow silhouette, Bode session, SCPI measure and DMM session recorder exceed usable RAM")
    ASSERT(SIZEOF(.capture_trial) == 16384, "The AutoSetup trial area must be AXI SRAM")
    ASSERT(SIZEOF(.capture_nodes) == 960, "MDMA history nodes must be AXI SRAM")
    ASSERT(LOADADDR(.dma_data) + SIZEOF(.dma_data) <= __openosc_flash_end, "Application exceeds Flash reservation")
}
