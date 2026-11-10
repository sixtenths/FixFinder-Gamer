#ifndef FIXFINDER_SAVE_FILES_H
#define FIXFINDER_SAVE_FILES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef FIXFINDER_SAVE_FILES
extern const char *fixfinder_save_notice;
bool fixfinder_save_read(const char *name, void *data, unsigned int size);
bool fixfinder_save_write(const char *name, const void *data, unsigned int size);
#else
static inline bool fixfinder_save_read(const char *name, void *data, unsigned int size)
{
    (void)name;
    (void)data;
    (void)size;
    return false;
}

static inline bool fixfinder_save_write(const char *name, const void *data, unsigned int size)
{
    (void)name;
    (void)data;
    (void)size;
    return false;
}
#endif

#endif
