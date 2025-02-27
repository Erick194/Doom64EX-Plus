// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2005 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
// Handles merging of PWADs, similar to deutex's -merge option
//
// Ideally this should work exactly the same as in deutex, but trying to
// read the deutex source code made my brain hurt.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "i_system.h"
#include "w_merge.h"
#include "w_wad.h"
#include "z_zone.h"
#ifdef _WIN32
#include "i_opndir.h"
#endif

typedef enum {
    SECTION_NORMAL,
    SECTION_TEXTURES,
    SECTION_SPRITES,
    SECTION_GFX,
    SECTION_SOUNDS
} section_t;

typedef struct {
    lumpinfo_t *lumps;
    int numlumps;
} searchlist_t;

typedef struct {
    char sprname[4];
    char frame;
    lumpinfo_t *angle_lumps[8];
} sprite_frame_t;

static searchlist_t iwad;
static searchlist_t iwad_sprites;
static searchlist_t iwad_textures;
static searchlist_t iwad_gfx;
static searchlist_t iwad_sounds;

static searchlist_t pwad;
static searchlist_t pwad_sprites;
static searchlist_t pwad_textures;
static searchlist_t pwad_gfx;
static searchlist_t pwad_sounds;

// lumps with these sprites must be replaced in the IWAD
static sprite_frame_t *sprite_frames;
static int num_sprite_frames;
static int sprite_frames_alloced;

// Search in a list to find a lump with a particular name
// Linear search (slow!)
//
// Returns -1 if not found

static int FindInList(searchlist_t *list, const char *name) {
    int i;

    for(i = 0; i < list->numlumps; ++i) {
        if(!strncasecmp(list->lumps[i].name, name, 8)) {
            return i;
        }
    }

    return -1;
}

static dboolean SetupList(searchlist_t *list, searchlist_t *src_list,
                          const char *startname, const char *endname,
                          const char *startname2, const char *endname2) {
    int startlump, endlump;

    list->numlumps = 0;
    startlump = FindInList(src_list, startname);

    if(startname2 != NULL && startlump < 0) {
        startlump = FindInList(src_list, startname2);
    }

    if(startlump >= 0) {
        endlump = FindInList(src_list, endname);

        if(endname2 != NULL && endlump < 0) {
            endlump = FindInList(src_list, endname2);
        }

        if(endlump > startlump) {
            list->lumps = src_list->lumps + startlump + 1;
            list->numlumps = endlump - startlump - 1;
            return true;
        }
    }

    return false;
}

// Sets up the sprite/flat search lists

static void SetupLists(void) {
    // IWAD

    if(!SetupList(&iwad_textures, &iwad, "T_START", "T_END", NULL, NULL)) {
        I_Error("Textures section not found in IWAD");
    }

    if(!SetupList(&iwad_sprites, &iwad, "S_START", "S_END", NULL, NULL)) {
        I_Error("Sprites section not found in IWAD");
    }

    if(!SetupList(&iwad_gfx, &iwad, "SYMBOLS", "MOUNTC", NULL, NULL)) {
        I_Error("GFX section not found in IWAD");
    }

    if(!SetupList(&iwad_sounds, &iwad, "DM_START", "DM_END", NULL, NULL)) {
        I_Error("Sounds section not found in IWAD");
    }

    // PWAD

    SetupList(&pwad_textures,   &pwad, "T_START", "T_END", "TT_START", "TT_END");
    SetupList(&pwad_sprites,    &pwad, "S_START", "S_END", "SS_START", "SS_END");
    SetupList(&pwad_gfx,        &pwad, "SYMBOLS", "MOUNTC", "SYMBOLS", "MOUNTC");
    SetupList(&pwad_sounds,     &pwad, "DM_START", "DM_END", NULL, NULL);
}

// Initialise the replace list

static void InitSpriteList(void) {
    if(sprite_frames == NULL) {
        sprite_frames_alloced = 128;
        sprite_frames = (sprite_frame_t*) Z_Malloc(sizeof(*sprite_frames) * sprite_frames_alloced,
                                 PU_STATIC, NULL);
    }

    num_sprite_frames = 0;
}

// Find a sprite frame

static sprite_frame_t *FindSpriteFrame(char *name, int frame) {
    sprite_frame_t *result;
    int i;

    // Search the list and try to find the frame

    for(i = 0; i < num_sprite_frames; ++i) {
        sprite_frame_t *cur = &sprite_frames[i];

        if(!strncasecmp(cur->sprname, name, 4) && cur->frame == frame) {
            return cur;
        }
    }

    // Not found in list; Need to add to the list

    // Grow list?

    if(num_sprite_frames >= sprite_frames_alloced) {
        sprite_frame_t *newframes;

        newframes = (sprite_frame_t*) Z_Malloc(sprite_frames_alloced * 2 * sizeof(*sprite_frames),
                             PU_STATIC, NULL);
        memcpy(newframes, sprite_frames,
               sprite_frames_alloced * sizeof(*sprite_frames));
        Z_Free(sprite_frames);
        sprite_frames_alloced *= 2;
        sprite_frames = newframes;
    }

    // Add to end of list

    result = &sprite_frames[num_sprite_frames];
    strncpy(result->sprname, name, 4);
    result->frame = frame;

    for(i = 0; i < 8; ++i) {
        result->angle_lumps[i] = NULL;
    }

    ++num_sprite_frames;

    return result;
}

// Check if sprite lump is needed in the new wad

static dboolean SpriteLumpNeeded(lumpinfo_t *lump) {
    sprite_frame_t *sprite;
    int angle_num;
    int i;

    // check the first frame

    sprite = FindSpriteFrame(lump->name, lump->name[4]);
    angle_num = lump->name[5] - '0';

    if(angle_num == 0) {
        // must check all frames

        for(i = 0; i < 8; ++i) {
            if(sprite->angle_lumps[i] == lump) {
                return true;
            }
        }
    }
    else {
        // check if this lump is being used for this frame

        if(sprite->angle_lumps[angle_num - 1] == lump) {
            return true;
        }
    }

    // second frame if any

    // no second frame?
    if(lump->name[6] == '\0') {
        return false;
    }

    sprite = FindSpriteFrame(lump->name, lump->name[6]);
    angle_num = lump->name[7] - '0';

    if(angle_num == 0) {
        // must check all frames

        for(i = 0; i < 8; ++i) {
            if(sprite->angle_lumps[i] == lump) {
                return true;
            }
        }
    }
    else {
        // check if this lump is being used for this frame

        if(sprite->angle_lumps[angle_num - 1] == lump) {
            return true;
        }
    }

    return false;
}

static void AddSpriteLump(lumpinfo_t *lump) {
    sprite_frame_t *sprite;
    int angle_num;
    int i;

    // first angle

    sprite = FindSpriteFrame(lump->name, lump->name[4]);
    angle_num = lump->name[5] - '0';

    if(angle_num == 0) {
        for(i = 0; i < 8; ++i) {
            sprite->angle_lumps[i] = lump;
        }
    }
    else {
        sprite->angle_lumps[angle_num - 1] = lump;
    }

    // second angle

    // no second angle?

    if(lump->name[6] == '\0') {
        return;
    }

    sprite = FindSpriteFrame(lump->name, lump->name[6]);
    angle_num = lump->name[7] - '0';

    if(angle_num == 0) {
        for(i = 0; i < 8; ++i) {
            sprite->angle_lumps[i] = lump;
        }
    }
    else {
        sprite->angle_lumps[angle_num - 1] = lump;
    }
}

// Generate the list.  Run at the start, before merging

static void GenerateSpriteList(void) {
    int i;

    InitSpriteList();

    // Add all sprites from the IWAD

    for(i = 0; i < iwad_sprites.numlumps; ++i) {
        AddSpriteLump(&iwad_sprites.lumps[i]);
    }

    // Add all sprites from the PWAD
    // (replaces IWAD sprites)

    for(i = 0; i < pwad_sprites.numlumps; ++i) {
        AddSpriteLump(&pwad_sprites.lumps[i]);
    }
}

// Perform the merge.
//
// The merge code creates a new lumpinfo list, adding entries from the
// IWAD first followed by the PWAD.
//
// For the IWAD:
//  * Flats are added.  If a flat with the same name is in the PWAD,
//    it is ignored(deleted).  At the end of the section, all flats in the
//    PWAD are inserted.  This is consistent with the behavior of
//    deutex/deusf.
//  * Sprites are added.  The "replace list" is generated before the merge
//    from the list of sprites in the PWAD.  Any sprites in the IWAD found
//    to match the replace list are removed.  At the end of the section,
//    the sprites from the PWAD are inserted.
//
// For the PWAD:
//  * All Sprites and Flats are ignored, with the assumption they have
//    already been merged into the IWAD's sections.

static void DoMerge(void) {
    section_t current_section;
    lumpinfo_t *newlumps;
    int num_newlumps;
    int lumpindex;
    int i, n;

    // Can't ever have more lumps than we already have
    newlumps = (lumpinfo_t*) malloc(sizeof(lumpinfo_t) * numlumps);
    num_newlumps = 0;

    // Add IWAD lumps
    current_section = SECTION_NORMAL;

    for(i = 0; i < iwad.numlumps; ++i) {
        lumpinfo_t *lump = &iwad.lumps[i];

        switch(current_section) {
        case SECTION_NORMAL:
            if(!strncasecmp(lump->name, "T_START", 8)) {
                current_section = SECTION_TEXTURES;
            }
            else if(!strncasecmp(lump->name, "S_START", 8)) {
                current_section = SECTION_SPRITES;
            }
            else if(!strncasecmp(lump->name, "SYMBOLS", 8)) {
                current_section = SECTION_GFX;
            }
            else if(!strncasecmp(lump->name, "DM_START", 8)) {
                current_section = SECTION_SOUNDS;
            }

            newlumps[num_newlumps++] = *lump;

            break;

        case SECTION_TEXTURES:

            // Have we reached the end of the section?

            if(!strncasecmp(lump->name, "T_END", 8)) {
                // Add all new flats from the PWAD to the end
                // of the section

                for(n = 0; n < pwad_textures.numlumps; ++n) {
                    newlumps[num_newlumps++] = pwad_textures.lumps[n];
                }

                newlumps[num_newlumps++] = *lump;

                // back to normal reading
                current_section = SECTION_NORMAL;
            }
            else {
                // If there is a flat in the PWAD with the same name,
                // do not add it now.  All PWAD flats are added to the
                // end of the section. Otherwise, if it is only in the
                // IWAD, add it now

                lumpindex = FindInList(&pwad_textures, lump->name);

                if(lumpindex < 0) {
                    newlumps[num_newlumps++] = *lump;
                }
            }

            break;

        case SECTION_SPRITES:

            // Have we reached the end of the section?

            if(!strncasecmp(lump->name, "S_END", 8)) {
                // add all the pwad sprites

                for(n = 0; n < pwad_sprites.numlumps; ++n) {
                    if(SpriteLumpNeeded(&pwad_sprites.lumps[n])) {
                        newlumps[num_newlumps++] = pwad_sprites.lumps[n];
                    }
                }

                // copy the ending
                newlumps[num_newlumps++] = *lump;

                // back to normal reading
                current_section = SECTION_NORMAL;
            }
            else {
                // Is this lump holding a sprite to be replaced in the
                // PWAD? If so, wait until the end to add it.

                if(SpriteLumpNeeded(lump)) {
                    newlumps[num_newlumps++] = *lump;
                }
            }

            break;

        case SECTION_GFX:

            // Have we reached the end of the section?

            if(!strncasecmp(lump->name, "MOUNTC", 8)) {
                // Add all new flats from the PWAD to the end
                // of the section

                for(n = 0; n < pwad_gfx.numlumps; ++n) {
                    newlumps[num_newlumps++] = pwad_gfx.lumps[n];
                }

                newlumps[num_newlumps++] = *lump;

                // back to normal reading
                current_section = SECTION_NORMAL;
            }
            else {
                // If there is a gfx in the PWAD with the same name,
                // do not add it now.  All PWAD gfx are added to the
                // end of the section. Otherwise, if it is only in the
                // IWAD, add it now

                lumpindex = FindInList(&pwad_gfx, lump->name);

                if(lumpindex < 0) {
                    newlumps[num_newlumps++] = *lump;
                }
            }

            break;

        case SECTION_SOUNDS:

            // Have we reached the end of the section?

            if(!strncasecmp(lump->name, "DM_END", 8)) {
                // Add all new flats from the PWAD to the end
                // of the section

                for(n = 0; n < pwad_sounds.numlumps; ++n) {
                    newlumps[num_newlumps++] = pwad_sounds.lumps[n];
                }

                newlumps[num_newlumps++] = *lump;

                // back to normal reading
                current_section = SECTION_NORMAL;
            }
            else {
                // If there is a sound in the PWAD with the same name,
                // do not add it now.  All PWAD sounds are added to the
                // end of the section. Otherwise, if it is only in the
                // IWAD, add it now

                lumpindex = FindInList(&pwad_sounds, lump->name);

                if(lumpindex < 0) {
                    newlumps[num_newlumps++] = *lump;
                }
            }

            break;
        }
    }

    // Add PWAD lumps
    current_section = SECTION_NORMAL;

    for(i = 0; i < pwad.numlumps; ++i) {
        lumpinfo_t *lump = &pwad.lumps[i];

        switch(current_section) {
        case SECTION_NORMAL:
            if(!strncasecmp(lump->name, "T_START", 8)
                    || !strncasecmp(lump->name, "TT_START", 8)) {
                current_section = SECTION_TEXTURES;
            }
            else if(!strncasecmp(lump->name, "S_START", 8)
                    || !strncasecmp(lump->name, "SS_START", 8)) {
                current_section = SECTION_SPRITES;
            }
            else if(!strncasecmp(lump->name, "SYMBOLS", 8)
                    || !strncasecmp(lump->name, "MOUNTC", 8)) {
                current_section = SECTION_GFX;
            }
            else if(!strncasecmp(lump->name, "DM_START", 8)) {
                current_section = SECTION_SOUNDS;
            }
            else {
                // Don't include the headers of sections

                newlumps[num_newlumps++] = *lump;
            }
            break;

        case SECTION_TEXTURES:

            // PWAD flats are ignored (already merged)

            if(!strncasecmp(lump->name, "TT_END", 8)
                    || !strncasecmp(lump->name, "T_END", 8)) {
                // end of section
                current_section = SECTION_NORMAL;
            }
            break;

        case SECTION_SPRITES:

            // PWAD sprites are ignored (already merged)

            if(!strncasecmp(lump->name, "SS_END", 8)
                    || !strncasecmp(lump->name, "S_END", 8)) {
                // end of section
                current_section = SECTION_NORMAL;
            }
            break;

        case SECTION_GFX:

            // PWAD gfx are ignored (already merged)

            if(!strncasecmp(lump->name, "MOUNTC", 8)
                    || !strncasecmp(lump->name, "MOUNTC", 8)) {
                // end of section
                current_section = SECTION_NORMAL;
            }
            break;

        case SECTION_SOUNDS:

            // PWAD sounds are ignored (already merged)

            if(!strncasecmp(lump->name, "DM_END", 8)) {
                // end of section
                current_section = SECTION_NORMAL;
            }
            break;
        }
    }

    // Switch to the new lumpinfo, and free the old one

    free(lumpinfo);
    lumpinfo = newlumps;
    numlumps = num_newlumps;

}

void W_PrintDirectory(void) {
    int i, n;

    // debug
    for(i = 0; i < numlumps; ++i) {
        for(n = 0; n < 8 && lumpinfo[i].name[n] != '\0'; ++n) {
            putchar(lumpinfo[i].name[n]);
        }

        putchar('\n');
    }
}

// Merge in a file by name

void W_MergeFile(char *filename) {
    int old_numlumps;

    old_numlumps = numlumps;

    // Load PWAD

    if(W_AddFile(filename) == NULL) {
        return;
    }

    // iwad is at the start, pwad was appended to the end

    iwad.lumps = lumpinfo;
    iwad.numlumps = old_numlumps;

    pwad.lumps = lumpinfo + old_numlumps;
    pwad.numlumps = numlumps - old_numlumps;

    // Setup sprite/flat lists

    SetupLists();

    // Generate list of sprites to be replaced by the PWAD

    GenerateSpriteList();

    // Perform the merge

    DoMerge();
}



