/* Copyright 2022-2023 Vulcalien
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "screen.h"

#include <gba/background.h>
#include <gba/sprite.h>
#include <memory.h>

#include "images.h"

#define DISPLAY_CONTROL *((vu16 *) 0x04000000)

#define WINDOW_IN  *((vu16 *) 0x04000048)
#define WINDOW_OUT *((vu16 *) 0x0400004a)

#define CHAR_BLOCK_0 ((vu16 *) 0x06000000)
#define CHAR_BLOCK_1 ((vu16 *) 0x06004000)
#define CHAR_BLOCK_2 ((vu16 *) 0x06008000)
#define CHAR_BLOCK_3 ((vu16 *) 0x0600c000)

#define SPR_TILESET ((vu16 *) 0x06010000)

#define BG_PALETTE  ((vu16 *) 0x05000000)
#define SPR_PALETTE ((vu16 *) 0x05000200)

static void load_tileset(vu16 *dest, const u8 *tileset, u32 size,
                         u8 zero_conversion) {
    for(u32 i = 0; i < size; i++) {
        u8 b0 = tileset[i * 2];
        u8 b1 = tileset[i * 2 + 1];

        if((b0 & 0x0f) == 0)
            b0 |= zero_conversion;
        if((b0 & 0xf0) == 0)
            b0 |= (zero_conversion << 4);

        if((b1 & 0x0f) == 0)
            b1 |= zero_conversion;
        if((b1 & 0xf0) == 0)
            b1 |= (zero_conversion << 4);

        // in a byte, lower 4 bits are for left, upper 4 bits for right
        b0 = (b0 << 4) | (b0 >> 4);
        b1 = (b1 << 4) | (b1 >> 4);

        dest[i] = (b1 << 8) | b0;
    }
}

#define LOAD_TILESET_CONVERT(dest, tileset, zero_conversion)\
    load_tileset(dest, tileset, sizeof(tileset) / sizeof(u16), zero_conversion)

#define LOAD_TILESET(dest, tileset)\
    LOAD_TILESET_CONVERT(dest, tileset, 0)

#define LOAD_PALETTE(dest, palette)\
    memcpy16(dest, palette, sizeof(palette))

void screen_init(void) {
    DISPLAY_CONTROL = 0       | // Video mode
                      1 << 6  | // OBJ Character mapping (1 is linear)
                      1 << 7  | // Forced Blank
                      0 << 8  | // Enable BG 0
                      1 << 9  | // Enable BG 1
                      0 << 10 | // Enable BG 2
                      1 << 11 | // Enable BG 3
                      1 << 12 | // Enable OBJ
                      1 << 15;  // Enable OBJ Window

    // Filter out the light layer when inside OBJ Window
    WINDOW_OUT = ~(1 << 10);

    // Sky Background
    background_config(BG0, &(struct Background) {
        .priority = 3,
        .tileset  = 0,
        .tilemap  = 16
    });

    // Level Tiles
    background_config(BG1, &(struct Background) {
        .priority = 2,
        .tileset  = 0,
        .tilemap  = 17
    });

    // Light system
    background_config(BG2, &(struct Background) {
        .priority = 1,
        .tileset  = 1,
        .tilemap  = 18
    });

    // Text and GUI
    background_config(BG3, &(struct Background) {
        .priority = 0,
        .tileset  = 1,
        .tilemap  = 19
    });

    // load palettes
    LOAD_PALETTE(BG_PALETTE,  bg_palette);
    LOAD_PALETTE(SPR_PALETTE, sprite_palette);

    LOAD_PALETTE(BG_PALETTE  + 12 * 16, item_palette);
    LOAD_PALETTE(SPR_PALETTE + 12 * 16, item_palette);

    // load tilesets
    LOAD_TILESET(CHAR_BLOCK_0, level_tileset);
    LOAD_TILESET(CHAR_BLOCK_1, gui_tileset);
    LOAD_TILESET(CHAR_BLOCK_1 + 256 * 32 / 2, light_tileset);

    LOAD_TILESET(SPR_TILESET, sprite_tileset);

    LOAD_TILESET_CONVERT(CHAR_BLOCK_1 + 128 * 32 / 2, item_tileset, 0xf);
    LOAD_TILESET(SPR_TILESET + 256 * 32 / 2, item_tileset);

    LOAD_TILESET(SPR_TILESET + 320 * 32 / 2, light_sprite_tileset);

    // load font sprites
    for(u32 i = 0; i <= 51; i++) {
        load_tileset(
            SPR_TILESET + (640 + i * 2) * 32 / 2,
            font_sprite_tileset + (i / 10) * 32,
            32 / 2, 0
        );

        load_tileset(
            SPR_TILESET + (640 + i * 2 + 1) * 32 / 2,
            font_sprite_tileset + (i % 10) * 32,
            32 / 2, 0
        );
    }

    // set sky background
    for(u32 y = 0; y <= 18; y++)
        for(u32 x = 0; x <= 30; x++)
            BG0_TILEMAP[x + y * 32] = 0 | 9 << 12;

    // prepare prescreen
    screen_set_bg_palette_color(9, 0xc, 0x7fff);
    screen_set_bg_palette_color(9, 0xd, 0x7fff);
    for(u32 y = 0; y < 20; y++)
        for(u32 x = 0; x < 30; x++)
            BG3_TILEMAP[x + y * 32] = 29 | 9 << 12;

    sprite_hide_all();

    // disable forced blank
    DISPLAY_CONTROL &= ~(1 << 7);
}

IWRAM_SECTION
void vsync(void) {
    __asm__ volatile ("swi 0x05 << 16");
}

IWRAM_SECTION
void screen_write(const char *text, u8 palette, u32 x, u32 y) {
    const u32 x0 = x;

    for(u32 i = 0; text[i] != '\0'; i++) {
        const char c = text[i];

        if(c == '\n') {
            x = x0;
            y++;

            if(y >= 20)
                break;
        } else {
            if(x >= 30)
                continue;

            u16 tile = 29 + (c - 32);
            BG3_TILEMAP[x + y * 32] = tile | palette << 12;

            x++;
        }
    }
}

THUMB
void screen_draw_frame(const char *title, u32 x, u32 y, u32 w, u32 h) {
    w--;
    h--;

    // draw corners
    BG3_TILEMAP[(x)     + (y)     * 32] = 88 | 0 << 10 | 6 << 12;
    BG3_TILEMAP[(x + w) + (y)     * 32] = 88 | 1 << 10 | 6 << 12;
    BG3_TILEMAP[(x)     + (y + h) * 32] = 88 | 2 << 10 | 6 << 12;
    BG3_TILEMAP[(x + w) + (y + h) * 32] = 88 | 3 << 10 | 6 << 12;

    // draw vertical borders
    for(u32 yi = y + 1; yi <= y + h - 1; yi++) {
        BG3_TILEMAP[(x)     + yi * 32] = 90 | 0 << 10 | 6 << 12;
        BG3_TILEMAP[(x + w) + yi * 32] = 90 | 1 << 10 | 6 << 12;

        // draw background
        for(u32 xi = x + 1; xi <= x + w - 1; xi++)
            BG3_TILEMAP[xi + yi * 32] = 29 | 6 << 12;
    }

    // draw horizontal borders
    for(u32 xi = x + 1; xi <= x + w - 1; xi++) {
        BG3_TILEMAP[xi + (y)     * 32] = 89 | 0 << 10 | 6 << 12;
        BG3_TILEMAP[xi + (y + h) * 32] = 89 | 2 << 10 | 6 << 12;
    }

    screen_write(title, 8, x + 1, y);
}

static inline u32 ticks_to_seconds(u32 ticks) {
    // refresh time:    280_896    cycles = 4389   * 64 cycles
    // clock frequency: 16_777_216 Hz     = 262144 * 64 Hz
    //
    // framerate = (clock frequency) / (refresh time)
    // time = ticks / framerate
    //      = (ticks * 4389) / 262144
    //      = (ticks * 4389) >> 18

    return (((u64) ticks) * 4389) >> 18;
}

void screen_write_time(u32 ticks, u8 palette, u32 x, u32 y) {
    // NOTE this is different from the original game: here, seconds are
    // displayed even if hours is not 0 and there is a space between the
    // hours and minutes values.

    u32 seconds = ticks_to_seconds(ticks);
    u32 minutes = seconds / 60;
    u32 hours   = minutes / 60;

    seconds %= 60;
    minutes %= 60;

    char text[15] = { 0 };

    u8 offset = 0;
    if(hours > 0) {
        itoa(hours, 10, text, 5, false);
        offset += 1 + (hours > 9)   + (hours > 99) +
                      (hours > 999) + (hours > 9999);
        text[offset++] = 'H';
        text[offset++] = ' ';
    }

    itoa(minutes, 10, text + offset, 2, true);
    offset += 2;
    text[offset++] = 'M';
    text[offset++] = ' ';

    itoa(seconds, 10, text + offset, 2, true);
    offset += 2;
    text[offset++] = 'S';

    screen_write(text, palette, x, y);
}

void screen_set_bg_palette_color(u8 palette, u8 index, u16 color) {
    BG_PALETTE[palette * 16 + index] = color;
}

void screen_load_active_item_palette(u8 palette) {
    memcpy16(BG_PALETTE + 11 * 16, item_palette + 16 * palette, 15 * 2);
    screen_set_bg_palette_color(11, 0xf, 0x0421);
}

void screen_update_level_specific(void) {
    // sky background
    if(current_level == 4)
        DISPLAY_CONTROL |= 1 << 8;
    else
        DISPLAY_CONTROL &= ~(1 << 8);

    // light layer
    if(current_level < 3)
        DISPLAY_CONTROL |= 1 << 10;
    else
        DISPLAY_CONTROL &= ~(1 << 10);

    // dirt color
    const u16 dirt_colors[5][2] = {
        { 0x1ce7, 0x318c },
        { 0x1ce7, 0x318c },
        { 0x1ce7, 0x318c },
        { 0x1cea, 0x35b0 },
        { -1,     0x6318 }
    };
    for(u32 i = 0; i <= 5; i++) {
        if(i == 3)
            continue;

        screen_set_bg_palette_color(i, 0xe, dirt_colors[current_level][0]);
        screen_set_bg_palette_color(i, 0xf, dirt_colors[current_level][1]);
    }
    screen_set_bg_palette_color(0, 0, dirt_colors[current_level][1]);

    // farmland
    const u16 farmland_colors[5][2] = {
        { 0x1cea, 0x1445 },
        { 0x1cea, 0x1445 },
        { 0x1cea, 0x1445 },
        { 0x210e, 0x1869 },
        { -1, -1 }
    };
    screen_set_bg_palette_color(3, 4, farmland_colors[current_level][0]);
    screen_set_bg_palette_color(3, 5, farmland_colors[current_level][1]);

    // ore/cloud cactus
    const u16 ore_colors[5][3] = {
        { 0x733c, 0x44b1, 0x1445 }, // gem
        { 0x5fbd, 0x2ed6, 0x0cc6 }, // gold
        { 0x673b, 0x35b0, 0x0844 }, // iron
        { -1, -1, -1 },
        { 0x7bde, 0x4a52, 0x1ce7 }  // cloud cactus
    };
    screen_set_bg_palette_color(2, 3, ore_colors[current_level][0]);
    screen_set_bg_palette_color(2, 4, ore_colors[current_level][1]);
    screen_set_bg_palette_color(2, 5, ore_colors[current_level][2]);

    // liquid
    const u16 liquid_colors[2][2] = {
        { 0x14b3, 0x21d7 },
        { 0x4442, 0x4d08 }
    };
    for(u32 i = 2; i <= 3; i++) {
        screen_set_bg_palette_color(i, 1, liquid_colors[current_level > 0][0]);
        screen_set_bg_palette_color(i, 2, liquid_colors[current_level > 0][1]);
    }
}
