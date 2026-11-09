#ifndef FIXFINDER_SETUP_WAD_H
#define FIXFINDER_SETUP_WAD_H

/* Setup-only data. A packed result is valid until the next packed load. */
const void *W_LoadSetupLump(int lump);

#endif
