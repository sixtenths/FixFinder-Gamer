#ifndef FIXFINDER_EPISODE_MAPS_H
#define FIXFINDER_EPISODE_MAPS_H

#include <stdbool.h>
#include <stdint.h>

#define FIXFINDER_EXTERNAL_MAP (UINT32_C(1) << 30)

bool doom_maps_available(void);
void doom_stage_map(int episode, int map);
const void *doom_staged_lump(uint32_t offset, uint32_t size);
void *doom_map_line_storage(uint32_t length);
void P_ResetSecnodePool(void);

#endif
