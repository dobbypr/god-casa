# The Beginning (Prototype)

A small SDL2 prototype that seeds civilizations, lets you roam the world as a player avatar, and tracks how populations harvest and grow over time.

## Project layout

- `src/` – game loop, rendering, and simulation code
- `include/` – shared headers
- `assets/` – placeholder art references and optional font override (`PlaceholderFont.ttf`)
- `Makefile` – build helper targeting SDL2 + SDL_ttf

## Building

Dependencies:
- SDL2 development libraries
- SDL2_ttf for HUD text rendering
- A TTF font (the engine looks for `assets/PlaceholderFont.ttf` first, then `DejaVuSans` from the system)

Build and run:

```bash
make
./the_beginning
```

Or run in one step:

```bash
make run
```

## Gameplay and controls

- Move with **WASD** or **Arrow keys**.
- **E**: interact/harvest nearby resource nodes.
- **C**: place a new civilization at the player location.
- **R**: drop a new resource node near the player (cycles through food, wood, stone).
- **Esc** or window close: quit the prototype.

## Systems overview

- **World generation:** tiles are initialized with simple noise and colored to represent grass, water, and mountains.
- **Player:** a controllable avatar that can explore the world and harvest resources.
- **Resources:** food, wood, and stone nodes with finite amounts. Civs and the player can deplete them.
- **Civilizations:** autonomous agents that gather nearby resources, spend stockpiles to erect placeholder structures, and grow their population over time when food is sufficient.
- **HUD:** overlays aggregated population/resources, player coordinates, and control reminders.

## Placeholder art

The `assets/` folder contains text placeholders. Replace them with sprites and UI art as the prototype evolves. Add a font file at `assets/PlaceholderFont.ttf` to control in-game typography if your system does not provide DejaVuSans.
