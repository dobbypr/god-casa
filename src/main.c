#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>

#include "game.h"

static void process_input(InputState *input, bool *running, World *world, float dt) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *running = false;
        } else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    *running = false;
                    break;
                case SDLK_w:
                case SDLK_UP:
                    input->up = true;
                    break;
                case SDLK_s:
                case SDLK_DOWN:
                    input->down = true;
                    break;
                case SDLK_a:
                case SDLK_LEFT:
                    input->left = true;
                    break;
                case SDLK_d:
                case SDLK_RIGHT:
                    input->right = true;
                    break;
                case SDLK_e:
                    input->interact = true;
                    break;
                case SDLK_c:
                    input->spawn_civilization = true;
                    break;
                case SDLK_r:
                    input->spawn_resource = true;
                    break;
            }
        } else if (event.type == SDL_KEYUP) {
            switch (event.key.keysym.sym) {
                case SDLK_w:
                case SDLK_UP:
                    input->up = false;
                    break;
                case SDLK_s:
                case SDLK_DOWN:
                    input->down = false;
                    break;
                case SDLK_a:
                case SDLK_LEFT:
                    input->left = false;
                    break;
                case SDLK_d:
                case SDLK_RIGHT:
                    input->right = false;
                    break;
                case SDLK_e:
                    input->interact = false;
                    break;
                default:
                    break;
            }
        }
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (input->up) dy -= 1.0f;
    if (input->down) dy += 1.0f;
    if (input->left) dx -= 1.0f;
    if (input->right) dx += 1.0f;

    if (dx != 0.0f || dy != 0.0f) {
        float inv_len = 1.0f / SDL_sqrtf(dx * dx + dy * dy);
        dx *= inv_len;
        dy *= inv_len;
    }

    world->player.x += dx * world->player.speed * dt;
    world->player.y += dy * world->player.speed * dt;
}

static TTF_Font *load_font(void) {
    const char *font_paths[] = {
        "./assets/PlaceholderFont.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL,
    };

    for (int i = 0; font_paths[i] != NULL; ++i) {
        TTF_Font *font = TTF_OpenFont(font_paths[i], 16);
        if (font) {
            return font;
        }
    }
    fprintf(stderr, "Failed to load font. HUD text will be missing.\n");
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "The Beginning - Prototype",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WORLD_WIDTH * TILE_SIZE,
        WORLD_HEIGHT * TILE_SIZE,
        SDL_WINDOW_SHOWN);

    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "Failed to create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    World world;
    world_init(&world);

    InputState input = {0};
    bool running = true;
    Uint32 last_ticks = SDL_GetTicks();

    TTF_Font *font = load_font();

    while (running) {
        Uint32 current_ticks = SDL_GetTicks();
        float dt = (current_ticks - last_ticks) / 1000.0f;
        last_ticks = current_ticks;

        input.spawn_civilization = false;
        input.spawn_resource = false;
        input.interact = false;

        process_input(&input, &running, &world, dt);
        handle_player_actions(&world, &input);
        world_update(&world, dt);

        SDL_SetRenderDrawColor(renderer, 15, 15, 18, 255);
        SDL_RenderClear(renderer);
        render_world(renderer, &world);
        render_ui(renderer, &world, font);
        SDL_RenderPresent(renderer);
    }

    if (font) {
        TTF_CloseFont(font);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
