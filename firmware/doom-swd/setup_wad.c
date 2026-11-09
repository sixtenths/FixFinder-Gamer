#include <stdint.h>
#include <string.h>

#include "doom_iwad.h"
#include "lprintf.h"
#include "w_wad.h"
#include "setup_wad.h"

#define PACKED_SETUP (UINT32_C(1) << 31)

static uint8_t setup_buffer[21 * 1024]
    __attribute__((section(".doom_setup"), aligned(4)));

static int decode_setup(const uint8_t *src, size_t remaining, size_t size)
{
    size_t written = 0;
    while (remaining != 0 && written < size) {
        const uint8_t token = *src++;
        remaining--;
        if (token < 0x80) {
            const size_t count = token + 1U;
            if (count > remaining || count > size - written) {
                return 0;
            }
            memcpy(setup_buffer + written, src, count);
            written += count;
            src += count;
            remaining -= count;
        } else {
            const size_t count = (token & 0x7fU) + 3U;
            if (remaining < 2 || count > size - written) {
                return 0;
            }
            const size_t distance = src[0] | ((size_t)src[1] << 8);
            if (distance == 0 || distance > written) {
                return 0;
            }
            for (size_t i = 0; i < count; i++, written++) {
                setup_buffer[written] = setup_buffer[written - distance];
            }
            src += 2;
            remaining -= 2;
        }
    }
    return remaining == 0 && written == size;
}

const void *W_LoadSetupLump(int lump)
{
    const wadinfo_t *header = (const wadinfo_t *)doom_iwad;
    if (lump < 0 || lump >= header->numlumps) {
        I_Error("Invalid setup lump %d", lump);
    }
    const filelump_t *directory = (const filelump_t *)(doom_iwad + header->infotableofs);
    const uint32_t position = (uint32_t)directory[lump].filepos;
    if ((position & PACKED_SETUP) == 0) {
        return W_CacheLumpNum(lump);
    }

    const uint32_t offset = position & ~PACKED_SETUP;
    const size_t size = (size_t)directory[lump].size;
    uint32_t packed_size;
    if (size > sizeof setup_buffer || offset > doom_iwad_len ||
        doom_iwad_len - offset < sizeof packed_size) {
        I_Error("Invalid packed setup lump %d", lump);
    }
    memcpy(&packed_size, doom_iwad + offset, sizeof packed_size);
    if (packed_size > doom_iwad_len - offset - sizeof packed_size ||
        !decode_setup(doom_iwad + offset + sizeof packed_size, packed_size, size)) {
        I_Error("Invalid packed setup data in lump %d", lump);
    }
    return setup_buffer;
}
