#include "drv_usb_hw.h"
#include "v9_board.h"
#include "v9_color_test.h"

void fixfinder_v9_run(void)
{
    v9_lcd_progress.phase = V9_LCD_BOARD_INIT;
    const struct v9_lcd_board *board = v9_lcd_board_init();
    if (board == NULL) {
        v9_lcd_progress.phase = V9_LCD_BOARD_FAILED;
        return;
    }
    v9_lcd_color_test(&board->bus, board->expected_id, board->landscape, usb_mdelay);
}
