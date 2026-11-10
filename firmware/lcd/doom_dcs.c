#include "../doom-swd/display.h"
#include "exmc_dcs.h"
#include "drv_usb_hw.h"
#include "gd32f4xx_dma.h"
#include <string.h>

static bool ordered_pixels;

static void configure_lcd_memory(void)
{
    uint32_t regions = (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
    if (!regions || (MPU->CTRL & MPU_CTRL_ENABLE_Msk)) return;
    /* Map the command/data window as shareable Device memory. The default
     * external-RAM attributes do not guarantee ordered peripheral writes. */
    __DMB();
    MPU->RNR = regions - 1;
    MPU->RBAR = 0x68000000U;
    MPU->RASR = MPU_RASR_XN_Msk | (3U << MPU_RASR_AP_Pos) | MPU_RASR_S_Msk |
                MPU_RASR_B_Msk | (4U << MPU_RASR_SIZE_Pos) | MPU_RASR_ENABLE_Msk;
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
    ordered_pixels = true;
}

bool doom_dcs_init(void)
{
    init_display_bus();
    usb_mdelay(150);
    if (!panel_matches()) {
        return false;
    }
    start_panel(usb_mdelay, 0xa0);  /* Upright landscape in the FixFinder case. */
    /* Keep the conservative read timing used to identify the panel.
       Use a shorter data phase for writes, still longer than the stock setting. */
    EXMC_SNWTCFG2 = 0x00000b15U;
    EXMC_SNCTL2 |= EXMC_SNCTL_EXMODEN;
    __DSB();
    configure_lcd_memory();
    start_backlight();
    return true;
}

static void write_pixels(const uint16_t *pixels, unsigned int count)
{
    if (ordered_pixels) {
        for (unsigned int x = 0; x < count; ++x) {
            LCD_DATA = (uint8_t)(pixels[x] >> 8);
            LCD_DATA = (uint8_t)pixels[x];
        }
        __DSB();
    } else {
        for (unsigned int x = 0; x < count; ++x) {
            write_data((uint8_t)(pixels[x] >> 8));
            write_data((uint8_t)pixels[x]);
        }
    }
}

static bool write_row(const void *context, unsigned int row, const uint16_t *pixels)
{
    (void)context;
    write_pixels(pixels, DOOM_LCD_WIDTH);
    doom_lcd_write_row(row, pixels);
    return true;
}

void doom_dcs_begin_native_span(unsigned int x, unsigned int width)
{
    begin_panel_window(x, 0, width, DOOM_LCD_HEIGHT);
}

void doom_dcs_write_native_span(unsigned int row, const uint8_t *indices,
                                unsigned int x, unsigned int width)
{
    const uint16_t *pixels = doom_display_native_row(indices);
    write_pixels(pixels + x, width);
    doom_lcd_write_row(row, pixels);
}

void doom_dcs_write_native_dirty_row(unsigned int row, const uint8_t *indices,
                                     unsigned int x, unsigned int width)
{
    begin_panel_window(x, row, width, 1);
    write_pixels(doom_display_native_pixels(indices + x, width), width);
}

void doom_dcs_update_native_row(unsigned int row, const uint8_t *indices,
                                uint8_t *previous, unsigned int x, unsigned int width)
{
    const uint8_t *current = indices + x;
    if (memcmp(previous, current, width) == 0) return;
    unsigned int first = 0, end = width;
    while (previous[first] == current[first]) ++first;
    while (previous[end - 1] == current[end - 1]) --end;
    doom_dcs_write_native_dirty_row(row, indices, x + first, end - first);
    memcpy(previous + first, current + first, end - first);
}

void doom_dcs_present(const uint8_t *frame)
{
    begin_panel_frame(DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT);
    doom_display_stream(frame, write_row, NULL);
}

void doom_dcs_begin_native(void)
{
    begin_panel_frame(DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT);
}

void doom_dcs_begin_native_rows(unsigned int first)
{
    begin_panel_window(0, first, DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT - first);
}

void doom_dcs_write_native(unsigned int row, const uint8_t *indices)
{
    write_row(NULL, row, doom_display_native_row(indices));
}

void doom_dcs_write_rgb_rect(unsigned int x, unsigned int y, unsigned int width,
                             unsigned int height, const uint16_t *pixels)
{
    begin_panel_window(x, y, width, height);
    write_pixels(pixels, width * height);
}

/* This buffer is in DMA-accessible SRAM. Each halfword holds two successive
 * byte writes to the panel's data register, high byte first. */
static uint16_t dma_row[DOOM_LCD_WIDTH] __attribute__((aligned(4)));
static bool dma_initialized, dma_pending, dma_failed;
static struct {
    unsigned int x, y, width, height;
    const uint16_t *pixels;
} video_rect;

void doom_dcs_finish_native_stream(void)
{
    if (!dma_pending) return;
    const uint32_t errors = DMA_INTF_FEEIF | DMA_INTF_SDEIF | DMA_INTF_TAEIF;
    uint32_t flags;
    unsigned int limit = 100000;
    do {
        flags = DMA_INTF0(DMA1);
    } while (!(flags & (errors | DMA_INTF_FTFIF)) && --limit);

    if ((flags & errors) || !limit) {
        DMA_CH0CTL(DMA1) &= ~DMA_CHXCTL_CHEN;
        while (DMA_CH0CTL(DMA1) & DMA_CHXCTL_CHEN) {}
        dma_failed = true;
    }
    dma_pending = false;
}

static void start_lcd_dma(const uint16_t *source, unsigned int count)
{
    if (!dma_initialized) {
        RCU_AHB1EN |= RCU_AHB1EN_DMA1EN;
        (void)RCU_AHB1EN;
        DMA_CH0CTL(DMA1) = DMA_MEMORY_TO_MEMORY | DMA_CHXCTL_PNAGA;
        DMA_CH0FCTL(DMA1) = DMA_CHXFCTL_MDMEN;
        DMA_CH0M0ADDR(DMA1) = (uintptr_t)&LCD_DATA;
        dma_initialized = true;
    }
    DMA_CH0PADDR(DMA1) = (uintptr_t)source;
    DMA_INTC0(DMA1) = DMA_INTC_FEEIFC | DMA_INTC_SDEIFC | DMA_INTC_TAEIFC |
                     DMA_INTC_HTFIFC | DMA_INTC_FTFIFC;
    DMA_CH0CNT(DMA1) = count * sizeof *source;
    __DMB();
    DMA_CH0CTL(DMA1) |= DMA_CHXCTL_CHEN;
    dma_pending = true;
}

void doom_dcs_stream_native_row(unsigned int row, const uint8_t *indices)
{
    bool was_pending = dma_pending;
    doom_dcs_finish_native_stream();
    if (dma_failed) {
        if (was_pending) begin_panel_window(0, row, DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT - row);
        doom_dcs_write_native(row, indices);
        return;
    }
    const uint16_t *pixels = doom_display_native_row(indices);
    for (unsigned int x = 0; x < DOOM_LCD_WIDTH; ++x)
        dma_row[x] = (uint16_t)__REV16(pixels[x]);
    start_lcd_dma(dma_row, DOOM_LCD_WIDTH);
}

void doom_dcs_video_timing(bool fast)
{
    __DSB();
    /* Match the factory write phase during the video transfer. */
    EXMC_SNWTCFG2 = fast ? 0x00000615U : 0x00000b15U;
    __DSB();
}

static void write_wire_pixels(const uint16_t *pixels, unsigned int count)
{
    if (ordered_pixels) {
        for (unsigned int i = 0; i < count; ++i) {
            LCD_DATA = (uint8_t)pixels[i];
            LCD_DATA = (uint8_t)(pixels[i] >> 8);
        }
        __DSB();
    } else {
        for (unsigned int i = 0; i < count; ++i) {
            write_data((uint8_t)pixels[i]);
            write_data((uint8_t)(pixels[i] >> 8));
        }
    }
}

void doom_dcs_stream_video_rect(unsigned int x, unsigned int y, unsigned int width,
                                 unsigned int height, const uint16_t *wire_pixels)
{
    begin_panel_window(x, y, width, height);
    if (dma_failed) {
        write_wire_pixels(wire_pixels, width * height);
        return;
    }
    video_rect.x = x;
    video_rect.y = y;
    video_rect.width = width;
    video_rect.height = height;
    video_rect.pixels = wire_pixels;
    start_lcd_dma(wire_pixels, width * height);
}

void doom_dcs_finish_video_rect(void)
{
    if (!video_rect.pixels) return;
    doom_dcs_finish_native_stream();
    if (dma_failed) {
        begin_panel_window(video_rect.x, video_rect.y, video_rect.width, video_rect.height);
        write_wire_pixels(video_rect.pixels, video_rect.width * video_rect.height);
    }
    video_rect.pixels = NULL;
}
