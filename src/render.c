#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>

#include "game.h"

static void render_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        return;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_FreeSurface(surface);
    if (!texture) {
        return;
    }
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static SDL_Color tile_color(TileType type) {
    switch (type) {
        case TILE_GRASS:
            return (SDL_Color){34, 139, 34, 255};
        case TILE_WATER:
            return (SDL_Color){30, 144, 255, 255};
        case TILE_MOUNTAIN:
            return (SDL_Color){90, 90, 90, 255};
    }
    return (SDL_Color){255, 255, 255, 255};
}

static SDL_Color resource_color(ResourceType type) {
    switch (type) {
        case RESOURCE_FOOD:
            return (SDL_Color){220, 120, 60, 255};
        case RESOURCE_WOOD:
            return (SDL_Color){139, 69, 19, 255};
        case RESOURCE_STONE:
            return (SDL_Color){200, 200, 200, 255};
        case RESOURCE_COUNT:
            break;
    }
    return (SDL_Color){255, 255, 255, 255};
}

void render_world(SDL_Renderer *renderer, const World *world) {
    SDL_Rect tile_rect = {0, 0, TILE_SIZE, TILE_SIZE};
    for (int y = 0; y < WORLD_HEIGHT; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            int idx = y * WORLD_WIDTH + x;
            tile_rect.x = x * TILE_SIZE;
            tile_rect.y = y * TILE_SIZE;
            SDL_Color color = tile_color(world->tiles[idx].type);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(renderer, &tile_rect);
        }
    }

    for (int i = 0; i < world->resource_count; ++i) {
        const ResourceNode *node = &world->resources[i];
        if (!node->active) continue;
        SDL_Color color = resource_color(node->type);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_Rect rect = {(int)node->x, (int)node->y, TILE_SIZE / 2, TILE_SIZE / 2};
        SDL_RenderFillRect(renderer, &rect);
    }

    for (int i = 0; i < world->civilization_count; ++i) {
        const Civilization *civ = &world->civilizations[i];
        if (!civ->active) continue;
        SDL_SetRenderDrawColor(renderer, 250, 222, 85, 255);
        SDL_Rect rect = {(int)civ->x, (int)civ->y, TILE_SIZE, TILE_SIZE};
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 180, 160, 60, 255);
        SDL_RenderDrawRect(renderer, &rect);
    }

    SDL_SetRenderDrawColor(renderer, 75, 105, 190, 255);
    SDL_Rect player_rect = {(int)world->player.x, (int)world->player.y, TILE_SIZE, TILE_SIZE};
    SDL_RenderFillRect(renderer, &player_rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawRect(renderer, &player_rect);
}

void render_ui(SDL_Renderer *renderer, const World *world, TTF_Font *font) {
    if (!font) return;
    SDL_Color text_color = {250, 250, 250, 255};
    SDL_Color background = {20, 20, 20, 200};

    SDL_Rect hud_rect = {8, 8, 320, 140};
    SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
    SDL_RenderFillRect(renderer, &hud_rect);
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &hud_rect);

    int total_population = 0;
    int total_food = 0;
    int total_wood = 0;
    int total_stone = 0;
    int total_structures = 0;

    for (int i = 0; i < world->civilization_count; ++i) {
        const Civilization *civ = &world->civilizations[i];
        if (!civ->active) continue;
        total_population += civ->population;
        total_food += civ->food;
        total_wood += civ->wood;
        total_stone += civ->stone;
        total_structures += civ->structures;
    }

    char buffer[256];
    SDL_snprintf(buffer, sizeof(buffer), "Population: %d  Structures: %d", total_population, total_structures);
    render_text(renderer, font, buffer, hud_rect.x + 10, hud_rect.y + 10, text_color);
    SDL_snprintf(buffer, sizeof(buffer), "Food: %d  Wood: %d  Stone: %d", total_food, total_wood, total_stone);
    render_text(renderer, font, buffer, hud_rect.x + 10, hud_rect.y + 34, text_color);

    SDL_snprintf(buffer, sizeof(buffer), "Player (%.0f, %.0f)", world->player.x, world->player.y);
    render_text(renderer, font, buffer, hud_rect.x + 10, hud_rect.y + 58, text_color);

    render_text(renderer, font, "Controls:", hud_rect.x + 10, hud_rect.y + 82, text_color);
    render_text(renderer, font, "Move: WASD/Arrows", hud_rect.x + 20, hud_rect.y + 104, text_color);
    render_text(renderer, font, "Interact: E | Spawn Civ: C | Spawn Resource: R", hud_rect.x + 20, hud_rect.y + 124, text_color);
}
