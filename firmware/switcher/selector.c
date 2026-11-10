#include "package.h"

bool switch_package_available(uint32_t game)
{
    struct switch_paths paths = switch_paths(game);
    FIL file;
    struct switch_image image;
    if (!switch_open(&file, paths.current, game, &image) &&
        !switch_open(&file, paths.backup, game, &image))
        return false;
    return f_close(&file) == FR_OK;
}
