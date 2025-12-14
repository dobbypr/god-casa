#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void generate_tiles(World *world) {
    for (int y = 0; y < WORLD_HEIGHT; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            int idx = y * WORLD_WIDTH + x;
            float noise = (float)(rand() % 100) / 100.0f;
            if (noise < 0.15f) {
                world->tiles[idx].type = TILE_WATER;
                world->tiles[idx].fertility = 0.05f;
            } else if (noise > 0.85f) {
                world->tiles[idx].type = TILE_MOUNTAIN;
                world->tiles[idx].fertility = 0.2f;
            } else {
                world->tiles[idx].type = TILE_GRASS;
                world->tiles[idx].fertility = 0.8f;
            }
        }
    }
}

void world_spawn_resource(World *world, float x, float y, ResourceType type, int amount) {
    if (world->resource_count >= MAX_RESOURCES) {
        return;
    }

    ResourceNode *node = &world->resources[world->resource_count++];
    node->x = x;
    node->y = y;
    node->type = type;
    node->amount = amount;
    node->active = true;
}

void world_spawn_civilization(World *world, float x, float y) {
    if (world->civilization_count >= MAX_CIVILIZATIONS) {
        return;
    }

    Civilization *civ = &world->civilizations[world->civilization_count++];
    civ->x = x;
    civ->y = y;
    civ->population = 5;
    civ->food = 10;
    civ->wood = 5;
    civ->stone = 0;
    civ->structures = 0;
    civ->gather_timer = 0.0f;
    civ->growth_timer = 0.0f;
    civ->active = true;
}

static ResourceNode *find_nearby_resource(World *world, const Civilization *civ, float radius) {
    for (int i = 0; i < world->resource_count; ++i) {
        ResourceNode *node = &world->resources[i];
        if (!node->active || node->amount <= 0) {
            continue;
        }
        float dx = node->x - civ->x;
        float dy = node->y - civ->y;
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq <= radius * radius) {
            return node;
        }
    }
    return NULL;
}

static void update_civilization(World *world, Civilization *civ, float dt) {
    const float gather_interval = 0.75f;
    const float growth_interval = 3.0f;

    civ->gather_timer += dt;
    civ->growth_timer += dt;

    if (civ->gather_timer >= gather_interval) {
        civ->gather_timer = 0.0f;
        ResourceNode *node = find_nearby_resource(world, civ, 96.0f);
        if (node != NULL) {
            node->amount -= 1;
            switch (node->type) {
                case RESOURCE_FOOD:
                    civ->food += 1;
                    break;
                case RESOURCE_WOOD:
                    civ->wood += 1;
                    break;
                case RESOURCE_STONE:
                    civ->stone += 1;
                    break;
                case RESOURCE_COUNT:
                    break;
            }
            if (node->amount <= 0) {
                node->active = false;
            }
        }
    }

    if (civ->growth_timer >= growth_interval && civ->food > civ->population) {
        civ->growth_timer = 0.0f;
        civ->population += 1;
        civ->food -= 1;
    }

    if (civ->wood >= 5 && civ->stone >= 2) {
        civ->structures = clamp_int(civ->structures + 1, 0, MAX_STRUCTURES);
        civ->wood -= 5;
        civ->stone -= 2;
    }
}

void world_init(World *world) {
    memset(world, 0, sizeof(World));
    srand((unsigned int)SDL_GetTicks());
    generate_tiles(world);

    world->player.x = TILE_SIZE * (WORLD_WIDTH / 2);
    world->player.y = TILE_SIZE * (WORLD_HEIGHT / 2);
    world->player.speed = 160.0f;

    world_spawn_resource(world, world->player.x + 64.0f, world->player.y, RESOURCE_FOOD, 12);
    world_spawn_resource(world, world->player.x - 120.0f, world->player.y + 80.0f, RESOURCE_WOOD, 15);
    world_spawn_resource(world, world->player.x + 140.0f, world->player.y - 100.0f, RESOURCE_STONE, 10);
    world_spawn_civilization(world, world->player.x - 60.0f, world->player.y - 40.0f);
}

static void clamp_player(World *world) {
    float max_x = (float)(WORLD_WIDTH * TILE_SIZE - TILE_SIZE);
    float max_y = (float)(WORLD_HEIGHT * TILE_SIZE - TILE_SIZE);
    if (world->player.x < 0.0f) world->player.x = 0.0f;
    if (world->player.y < 0.0f) world->player.y = 0.0f;
    if (world->player.x > max_x) world->player.x = max_x;
    if (world->player.y > max_y) world->player.y = max_y;
}

void handle_player_actions(World *world, const InputState *input) {
    if (input->spawn_civilization) {
        world_spawn_civilization(world, world->player.x, world->player.y);
    }
    if (input->spawn_resource) {
        ResourceType type = (world->resource_count % 3);
        world_spawn_resource(world, world->player.x + 32.0f, world->player.y + 32.0f, type, 12);
    }
    if (input->interact) {
        for (int i = 0; i < world->resource_count; ++i) {
            ResourceNode *node = &world->resources[i];
            if (!node->active) continue;
            float dx = node->x - world->player.x;
            float dy = node->y - world->player.y;
            if ((dx * dx + dy * dy) <= (48.0f * 48.0f)) {
                node->amount -= 1;
                if (node->amount <= 0) node->active = false;
                break;
            }
        }
    }
}

void world_update(World *world, float dt) {
    for (int i = 0; i < world->civilization_count; ++i) {
        Civilization *civ = &world->civilizations[i];
        if (!civ->active) continue;
        update_civilization(world, civ, dt);
    }

    clamp_player(world);
}
