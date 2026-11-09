#ifndef FIXFINDER_RADIO_FRAME_H
#define FIXFINDER_RADIO_FRAME_H

#include "protocol.h"

#define RADIO_MARK 0xfaU
#define RADIO_FRAME 0xa5U
#define RADIO_WAKE 0x57U
#define RADIO_BAUD 115200U
#define RADIO_PORT 5034U

static inline uint16_t radio_crc(const uint8_t *report)
{
    uint16_t crc = 0xffffU;
    for (unsigned int i = 0; i < LOADER_REPORT_SIZE; ++i) {
        crc ^= (uint16_t)report[i] << 8;
        for (unsigned int bit = 0; bit < 8; ++bit) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0));
        }
    }
    return crc;
}

#endif
