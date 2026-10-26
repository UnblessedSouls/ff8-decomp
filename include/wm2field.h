#ifndef WM2FIELD_H
#define WM2FIELD_H

#include "common.h"

// An entrance from the world map into a field: which field, and where the party appears there.
typedef struct {
    /** 0x00 */ s16 position_x;
    /** 0x02 */ s16 position_y;
    /** 0x04 */ u16 spawnTriIdx; // navmesh triangle
    /** 0x06 */ s16 fieldId; // global id of the destination field
    /** 0x08 */ u8 anim_state;
    /** 0x09 */ u8 unk09[3];
    /** 0x0C */ u8 unk0C[12];
} WorldToFieldEntrance;

// The 72 entrances of data/eng/wm2field.tbl.
#define WORLD_TO_FIELD_ENTRANCES ((WorldToFieldEntrance *)0x80097940)

#endif /* WM2FIELD_H */
