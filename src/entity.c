/* Copyright 2022 Vulcalien
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
#include "entity.h"

#include "level.h"
#include "tile.h"
#include "mob.h"

IWRAM_RODATA_SECTION
const struct Entity * const entity_list[ENTITY_TYPES] = {
    &zombie_entity,
    &slime_entity,
    &air_wizard_entity,
    &player_entity,

    &workbench_entity,
    &furnace_entity,
    &oven_entity,
    &anvil_entity,
    &chest_entity,
    &lantern_entity,

    &item_entity,
    &spark_entity,

    &text_particle_entity,
    &smash_particle_entity
};

IWRAM_SECTION
bool entity_move(struct Level *level, struct entity_Data *data,
                 i32 xm, i32 ym) {
    if(xm == 0 && ym == 0)
        return true;

    bool stopped = true;
    if(xm != 0 && entity_move2(level, data, xm, 0))
        stopped = false;
    if(ym != 0 && entity_move2(level, data, 0, ym))
        stopped = false;

    if(!stopped) {
        i32 xt = data->x >> 4;
        i32 yt = data->y >> 4;

        const struct Tile *tile = LEVEL_GET_TILE_S(level, xt, yt);
        if(tile->stepped_on)
            tile->stepped_on(level, xt, yt, data);
    }

    return !stopped;
}

IWRAM_SECTION
bool entity_move2(struct Level *level, struct entity_Data *data,
                  i32 xm, i32 ym) {
    const struct Entity *entity = ENTITY_S(data);

    u32 tiles_to_check;
    i32 tiles[2][2];

    i32 xto0 = (data->x - entity->xr) >> 4;
    i32 yto0 = (data->y - entity->yr) >> 4;
    i32 xto1 = (data->x + entity->xr) >> 4;
    i32 yto1 = (data->y + entity->yr) >> 4;

    i32 xt0 = (data->x + xm - entity->xr) >> 4;
    i32 yt0 = (data->y + ym - entity->yr) >> 4;
    i32 xt1 = (data->x + xm + entity->xr) >> 4;
    i32 yt1 = (data->y + ym + entity->yr) >> 4;

    if(xm < 0) {
        tiles[0][0] = xt0; tiles[0][1] = yt0;
        tiles[1][0] = xt0; tiles[1][1] = yt1;

        tiles_to_check = (xt0 != xto0) * (1 + (yt0 != yt1));
    } else if(xm > 0) {
        tiles[0][0] = xt1; tiles[0][1] = yt0;
        tiles[1][0] = xt1; tiles[1][1] = yt1;

        tiles_to_check = (xt1 != xto1) * (1 + (yt0 != yt1));
    } else if(ym < 0) {
        tiles[0][0] = xt0; tiles[0][1] = yt0;
        tiles[1][0] = xt1; tiles[1][1] = yt0;

        tiles_to_check = (yt0 != yto0) * (1 + (xt0 != xt1));
    } else {
        tiles[0][0] = xt0; tiles[0][1] = yt1;
        tiles[1][0] = xt1; tiles[1][1] = yt1;

        tiles_to_check = (yt1 != yto1) * (1 + (xt0 != xt1));
    }

    for(u32 i = 0; i < tiles_to_check; i++) {
        const i32 *t = tiles[i];

        const struct Tile *tile = LEVEL_GET_TILE_S(level, t[0], t[1]);
        if(tile->touch_damage && (data->type == ZOMBIE_ENTITY ||
                                  data->type == SLIME_ENTITY  ||
                                  data->type == PLAYER_ENTITY)) {
            struct mob_Data *mob_data = (struct mob_Data *) &data->data;

            mob_hurt(level, data, tile->touch_damage, mob_data->dir ^ 2);
        }

        if(tile->is_solid && tile->may_pass != data->type)
            return false;
    }

    // solid entity collision
    xt0--;
    yt0--;
    xt1++;
    yt1++;

    if(xt0 < 0) xt0 = 0;
    if(yt0 < 0) yt0 = 0;
    if(xt1 >= LEVEL_W) xt1 = LEVEL_W - 1;
    if(yt1 >= LEVEL_H) yt1 = LEVEL_H - 1;

    i32 xo0 = data->x - entity->xr;
    i32 yo0 = data->y - entity->yr;
    i32 xo1 = data->x + entity->xr;
    i32 yo1 = data->y + entity->yr;

    i32 x0 = data->x + xm - entity->xr;
    i32 y0 = data->y + ym - entity->yr;
    i32 x1 = data->x + xm + entity->xr;
    i32 y1 = data->y + ym + entity->yr;

    bool blocked_by_entity = false;
    for(u32 yt = yt0; yt <= yt1; yt++) {
        for(u32 xt = xt0; xt <= xt1; xt++) {
            const u32 tile = xt + yt * LEVEL_W;

            for(u32 i = 0; i < SOLID_ENTITIES_IN_TILE; i++) {
                const u8 entity_id = level_solid_entities[tile][i];
                if(entity_id >= ENTITY_LIMIT)
                    continue;

                struct entity_Data *e_data = &level->entities[entity_id];
                if(e_data == data)
                    continue;

                if(entity_intersects(e_data, x0, y0, x1, y1)) {
                    if(!blocked_by_entity)
                        if(!entity_intersects(e_data, xo0, yo0, xo1, yo1))
                            blocked_by_entity = true;

                    // item entity doesn't have a touch_player function
                    if(data->type == ITEM_ENTITY) {
                        if(blocked_by_entity)
                            return false;
                        else
                            continue;
                    }

                    // touch player
                    if(data->type == PLAYER_ENTITY) {
                        const struct Entity *entity = ENTITY_S(e_data);
                        entity->touch_player(level, e_data, data);
                    } else if(e_data->type == PLAYER_ENTITY) {
                        const struct Entity *entity = ENTITY_S(data);
                        entity->touch_player(level, data, e_data);
                    }
                }
            }
        }
    }

    if(blocked_by_entity)
        return false;

    data->x += xm;
    data->y += ym;
    return true;
}
