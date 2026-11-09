#ifndef FIXFINDER_DOOM_COMPACT_RECIPROCAL_H
#define FIXFINDER_DOOM_COMPACT_RECIPROCAL_H

#include <stdint.h>

/* Match GBADoom's saturated, rounded 2^32 / divisor table without a
 * 64-bit divide. For divisor >= 3 the rounding step cannot overflow. */
static inline int32_t doom_compact_reciprocal(uint32_t divisor)
{
    if (divisor < 3U) {
        return divisor == 0U ? 0 : INT32_MAX;
    }
    const uint32_t quotient = UINT32_MAX / divisor;
    const uint32_t remainder = UINT32_MAX - quotient * divisor;
    return (int32_t)(quotient + (remainder >= (divisor - 1U) / 2U));
}

#endif
