#include "../lcd/exmc_dcs.h"
#include "drv_usb_hw.h"

void fixfinder_game_run(void)
{
    init_display_bus();
    usb_mdelay(150);
    if (!panel_matches()) {
        return;
    }
    start_panel(usb_mdelay, 0xa0);
    start_backlight();
    for (unsigned int position = 0;; position = (position + 5) % 320) {
        begin_panel_frame(320, 240);
        for (unsigned int y = 0; y < 240; ++y) {
            for (unsigned int x = 0; x < 320; ++x) {
                bool square = y >= 100 && y < 140 && (x + 320 - position) % 320 < 40;
                write_data(square ? 0xff : 0x00);
                write_data(square ? 0xe0 : 0x1f);
            }
        }
        usb_mdelay(30);
    }
}
