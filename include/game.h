#ifndef GAME_H
#define GAME_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

#define WORLD_WIDTH 40
#define WORLD_HEIGHT 24
#define TILE_SIZE 32
#define MAX_RESOURCES 128
#define MAX_CIVILIZATIONS 16
#define MAX_STRUCTURES 32

typedef enum {
    TILE_GRASS,
    TILE_WATER,
    TILE_MOUNTAIN
} TileType;

typedef enum {
    RESOURCE_FOOD,
    RESOURCE_WOOD,
    RESOURCE_STONE,
    RESOURCE_COUNT
} ResourceType;

typedef struct {
    TileType type;
    float fertility;
} Tile;

typedef struct {
    float x;
    float y;
    ResourceType type;
    int amount;
    bool active;
} ResourceNode;

typedef struct {
    float x;
    float y;
    int population;
    int food;
    int wood;
    int stone;
    int structures;
    float gather_timer;
    float growth_timer;
    bool active;
} Civilization;

typedef struct {
    float x;
    float y;
    float speed;
} Player;

typedef struct {
    Tile tiles[WORLD_WIDTH * WORLD_HEIGHT];
    ResourceNode resources[MAX_RESOURCES];
    Civilization civilizations[MAX_CIVILIZATIONS];
    int resource_count;
    int civilization_count;
    Player player;
} World;

typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    bool interact;
    bool spawn_civilization;
    bool spawn_resource;
} InputState;

void world_init(World *world);
void world_update(World *world, float dt);
void world_spawn_resource(World *world, float x, float y, ResourceType type, int amount);
void world_spawn_civilization(World *world, float x, float y);
void handle_player_actions(World *world, const InputState *input);

// Rendering helpers
void render_world(SDL_Renderer *renderer, const World *world);
void render_ui(SDL_Renderer *renderer, const World *world, TTF_Font *font);

#endif // GAME_H
