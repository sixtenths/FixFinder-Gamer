#include "fixfinder.h"
#include <string.h>

enum { PLAYER_SIZE = 13, TARGET_SIZE = 8 };

static void rect(int x, int y, int width, int height, uint8_t color)
{
    for (int row = 0; row < height; ++row) {
        memset(fixfinder_screen + (y + row) * FIXFINDER_WIDTH + x, color, width);
    }
}

void fixfinder_game_main(void)
{
    static const uint8_t colors[FIXFINDER_PALETTE_SIZE] = {
        15, 20, 32, 91, 227, 180, 255, 169, 53, 45, 59, 78
    };
    int x = 15, y = 15, target_x = 180, target_y = 110;
    unsigned int score = 0;
    uint32_t hit_time = 0;
    fixfinder_palette(colors);
    fixfinder_set_led(FIXFINDER_LED_GREEN);

    for (;;) {
        uint32_t now = fixfinder_millis();
        uint32_t keys = fixfinder_keys(now);
        if ((keys & KEY_BACK) && x > 0) --x;
        if ((keys & KEY_ENTER) && x < FIXFINDER_WIDTH - PLAYER_SIZE) ++x;
        if ((keys & KEY_ERASE) && y > 0) --y;
        if ((keys & KEY_DOWN) && y < FIXFINDER_HEIGHT - PLAYER_SIZE) ++y;

        if (x < target_x + TARGET_SIZE && x + PLAYER_SIZE > target_x &&
            y < target_y + TARGET_SIZE && y + PLAYER_SIZE > target_y) {
            ++score;
            target_x = (int)((37U * score + 180U) % (FIXFINDER_WIDTH - TARGET_SIZE + 1));
            target_y = 10 + (int)((53U * score + 100U) % (FIXFINDER_HEIGHT - TARGET_SIZE - 9));
            fixfinder_set_led(FIXFINDER_LED_YELLOW);
            fixfinder_beep(50);
            hit_time = now;
        }
        if (now - hit_time >= 180) fixfinder_set_led(FIXFINDER_LED_GREEN);
        memset(fixfinder_screen, 0, sizeof fixfinder_screen);
        rect(0, 8, FIXFINDER_WIDTH, 1, 3);
        rect(0, 0, (int)(score % 41U) * 6, 7, 2);
        rect(x, y, PLAYER_SIZE, PLAYER_SIZE, 1);
        rect(target_x, target_y, TARGET_SIZE, TARGET_SIZE, 2);
        fixfinder_present();
        fixfinder_sleep(12);
    }
}
