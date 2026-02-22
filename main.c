// Copyright (c) dobbypr. All rights reserved.
// Unauthorized copying or distribution of this file, via any medium, is strictly prohibited.
// See the LICENSE file for permitted use.

/*
 * god-casa — A Worldbox-like prototype in C using ncurses
 *
 * Build:  make          (or: gcc -O2 -o god-casa main.c -lncurses -lm)
 * Run:    ./god-casa
 *
 * === CONTROLS ===
 *  Arrow keys      Move cursor
 *  W/A/S/D         Scroll camera
 *  Tab             Cycle selected civilisation
 *  1-6             Select terrain power  (Plains/Water/Forest/Mountain/Lava/Sand)
 *  7               Select "Spawn Unit" power
 *  8               Select "Spawn Village" power
 *  9               Select "Lightning" power  (destroy entity)
 *  0               Select "Meteor Strike" power  (area destruction)
 *  Enter / F       Apply selected power at cursor
 *  Space           Pause / Resume simulation
 *  Q               Quit
 *
 * === LEGEND ===
 *  ~  deep water / water      .  plains    ,  sand
 *  T  forest                  ^  mountain
 *  *  lava                    u  unit
 *  V  village                 C  city
 *  M  monster
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ncurses.h>

#include "simulation.h"

/* ======================================================================
   CONSTANTS
   ====================================================================== */
#define WW  120          /* world width  (tiles) */
#define WH   55          /* world height (tiles) */
#define MAX_E 1500       /* maximum entities */
#define NCIV   4         /* number of civilisations */

/* Simulation tuning */
#define UNIT_HP          40
#define UNIT_ATK          8
#define VILLAGE_HP      150
#define CITY_HP         400
#define MONSTER_HP       60
#define MONSTER_ATK      12
#define UNIT_SPAWN_INT   25   /* ticks between village unit spawns */
#define CITY_SPAWN_INT   12
#define VILLAGE_AGE_UP  300   /* ticks for village → city upgrade */
#define MAX_UNITS_CIV    60   /* cap on units per civilisation */
#define UNIT_MOVE_CD      3   /* ticks between unit moves */
#define UNIT_ATK_CD       5   /* ticks between unit attacks */
#define ENEMY_DETECT_R2 400   /* squared tile radius for enemy detection */

/* ======================================================================
   TYPES
   ====================================================================== */
typedef enum {
    T_DEEP   = 0, /* deep ocean */
    T_WATER  = 1, /* shallow water */
    T_SAND   = 2, /* beach / desert */
    T_PLAIN  = 3, /* grassland */
    T_FOREST = 4, /* forest */
    T_MOUNT  = 5, /* mountain */
    T_LAVA   = 6, /* volcanic */
    T_COUNT  = 7
} Terrain;

typedef enum { E_UNIT=0, E_VILLAGE=1, E_CITY=2, E_MONSTER=3 } EKind;
typedef enum { S_IDLE=0, S_SEEK=1, S_ATTACK=2, S_FLEE=3 }    UState;

typedef struct {
    int    alive;
    EKind  kind;
    int    civ;          /* 0..NCIV-1 for civ units; -1 for monsters */
    int    x, y;
    int    hp, max_hp;
    int    atk;
    UState state;
    int    target;       /* entity index of current target, or -1 */
    int    move_cd;      /* movement cooldown counter */
    int    atk_cd;       /* attack cooldown counter */
    int    spawn_timer;  /* buildings: ticks until next unit spawn */
    int    age;          /* ticks this entity has been alive */
} Ent;

typedef struct {
    Terrain t;
    int     eid;         /* entity index occupying this tile, or -1 */
} Tile;

typedef struct {
    int  active;
    char name[24];
    int  cpair;          /* ncurses colour-pair index */
    int  kills;
    int  units;
    int  villages;
} Civ;

/* ======================================================================
   GLOBALS
   ====================================================================== */
static Tile  W[WH][WW];
static Ent   E[MAX_E];
static Civ   C[NCIV];

static int cam_x = 0, cam_y = 0;
static int cur_x = WW/2, cur_y = WH/2;
static int sel_civ   = 0;
static int sel_power = 1;  /* 1-6 terrain; 7 unit; 8 village; 9 lightning; 10 meteor */
static int paused    = 0;
static int tick      = 0;
static int quitting  = 0;
static int view_w    = 80; /* updated each frame */
static int view_h    = 40;

/* ncurses colour-pair identifiers */
#define CP_DEEP    1
#define CP_WATER   2
#define CP_SAND    3
#define CP_PLAIN   4
#define CP_FOREST  5
#define CP_MOUNT   6
#define CP_LAVA    7
#define CP_CIV0    8   /* Humans  — red      */
#define CP_CIV1    9   /* Elves   — cyan     */
#define CP_CIV2   10   /* Dwarves — yellow   */
#define CP_CIV3   11   /* Orcs    — magenta  */
#define CP_MON    12   /* Monster — bold red */
#define CP_CUR    13   /* cursor highlight   */
#define CP_UI     14   /* side panel / bars  */

/* ======================================================================
   NOISE & WORLD GENERATION
   ====================================================================== */
static float noise_grid[WH+2][WW+2];

static float lerp_f(float a, float b, float t) { return a + t*(b-a); }
static float smooth(float t) { return t*t*(3.0f - 2.0f*t); }

static void noise_init(void)
{
    for (int y = 0; y <= WH; y++)
        for (int x = 0; x <= WW; x++)
            noise_grid[y][x] = (float)rand() / (float)RAND_MAX;
}

static float noise_at(float fx, float fy)
{
    int ix = (int)fx, iy = (int)fy;
    float tx = fx - ix, ty = fy - iy;
    ix = ((ix % WW) + WW) % WW;
    iy = ((iy % WH) + WH) % WH;
    int ix1 = (ix+1) % WW, iy1 = (iy+1) % WH;
    float v00 = noise_grid[iy ][ix ],  v10 = noise_grid[iy ][ix1];
    float v01 = noise_grid[iy1][ix ],  v11 = noise_grid[iy1][ix1];
    return lerp_f(lerp_f(v00, v10, smooth(tx)),
                  lerp_f(v01, v11, smooth(tx)), smooth(ty));
}

static float fbm(float x, float y, int oct)
{
    float val=0, amp=1, freq=1, maxv=0;
    for (int i=0; i<oct; i++) {
        val  += noise_at(x*freq, y*freq) * amp;
        maxv += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }
    return val / maxv;
}

static void world_gen(void)
{
    noise_init();
    for (int y = 0; y < WH; y++) {
        for (int x = 0; x < WW; x++) {
            W[y][x].eid = -1;
            float h = fbm((float)x / 28.0f, (float)y / 18.0f, 6);
            /* bias toward islands by subtracting distance from centre */
            float cx = (float)x/WW - 0.5f;
            float cy = (float)y/WH - 0.5f;
            h -= sqrtf(cx*cx + cy*cy) * 0.55f;
            Terrain t;
            if      (h < 0.22f) t = T_DEEP;
            else if (h < 0.35f) t = T_WATER;
            else if (h < 0.42f) t = T_SAND;
            else if (h < 0.60f) t = T_PLAIN;
            else if (h < 0.73f) t = T_FOREST;
            else if (h < 0.86f) t = T_MOUNT;
            else                t = T_LAVA;
            W[y][x].t = t;
        }
    }
}

/* ======================================================================
   ENTITY MANAGEMENT
   ====================================================================== */
static int ent_alloc(void)
{
    for (int i = 0; i < MAX_E; i++)
        if (!E[i].alive) return i;
    return -1;
}

static void ent_kill(int id)
{
    if (id < 0 || id >= MAX_E || !E[id].alive) return;
    Ent *e = &E[id];
    if (e->x >= 0 && e->x < WW && e->y >= 0 && e->y < WH)
        if (W[e->y][e->x].eid == id)
            W[e->y][e->x].eid = -1;
    if (e->civ >= 0 && e->civ < NCIV) {
        if (e->kind == E_UNIT)                          C[e->civ].units--;
        else if (e->kind == E_VILLAGE || e->kind == E_CITY) C[e->civ].villages--;
    }
    e->alive = 0;
}

static int ent_place(EKind kind, int civ, int x, int y)
{
    if (x < 0 || x >= WW || y < 0 || y >= WH) return -1;
    if (W[y][x].eid >= 0) return -1;
    int id = ent_alloc();
    if (id < 0) return -1;
    Ent *e = &E[id];
    memset(e, 0, sizeof(*e));
    e->alive  = 1;
    e->kind   = kind;
    e->civ    = civ;
    e->x = x; e->y = y;
    e->target = -1;
    e->state  = S_IDLE;
    switch (kind) {
        case E_UNIT:
            e->max_hp = UNIT_HP;    e->atk = UNIT_ATK;   break;
        case E_VILLAGE:
            e->max_hp = VILLAGE_HP; e->atk = 0;
            e->spawn_timer = UNIT_SPAWN_INT;             break;
        case E_CITY:
            e->max_hp = CITY_HP;    e->atk = 0;
            e->spawn_timer = CITY_SPAWN_INT;             break;
        case E_MONSTER:
            e->max_hp = MONSTER_HP; e->atk = MONSTER_ATK;
            e->civ = -1;                                  break;
    }
    e->hp = e->max_hp;
    W[y][x].eid = id;
    if (civ >= 0 && civ < NCIV) {
        if (kind == E_UNIT)                          C[civ].units++;
        else if (kind == E_VILLAGE || kind == E_CITY) C[civ].villages++;
    }
    return id;
}

/* Find a walkable (land) tile at or near (*ox, *oy). */
static int find_nearby_land(int *ox, int *oy)
{
    /* Expanding ring search */
    for (int r = 0; r <= WH/2; r++) {
        for (int attempt = 0; attempt < 25; attempt++) {
            int nx = *ox + (rand() % (2*r+3)) - (r+1);
            int ny = *oy + (rand() % (2*r+3)) - (r+1);
            if (nx < 0 || nx >= WW || ny < 0 || ny >= WH) continue;
            Terrain t = W[ny][nx].t;
            if ((t == T_PLAIN || t == T_FOREST || t == T_SAND) &&
                W[ny][nx].eid < 0) {
                *ox = nx; *oy = ny; return 1;
            }
        }
    }
    /* Full-world fallback */
    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++) {
            Terrain t = W[y][x].t;
            if ((t == T_PLAIN || t == T_FOREST || t == T_SAND) &&
                W[y][x].eid < 0) {
                *ox = x; *oy = y; return 1;
            }
        }
    return 0;
}

/* ======================================================================
   CIVILISATION INITIALISATION
   ====================================================================== */
static const char *CIV_NAMES[NCIV] = { "Humans", "Elves", "Dwarves", "Orcs" };
static const int   CIV_CPAIRS[NCIV] = { CP_CIV0, CP_CIV1, CP_CIV2, CP_CIV3 };

static void civs_init(void)
{
    /* Starting positions in the four quadrants */
    int starts[NCIV][2] = {
        { WW/4,    WH/4   },
        { 3*WW/4,  WH/4   },
        { WW/4,    3*WH/4 },
        { 3*WW/4,  3*WH/4 }
    };
    for (int i = 0; i < NCIV; i++) {
        C[i].active = 1;
        strncpy(C[i].name, CIV_NAMES[i], 23);
        C[i].cpair = CIV_CPAIRS[i];
        int sx = starts[i][0], sy = starts[i][1];
        if (!find_nearby_land(&sx, &sy)) continue;
        ent_place(E_VILLAGE, i, sx, sy);
        for (int j = 0; j < 3; j++) {
            int ux = sx, uy = sy;
            if (find_nearby_land(&ux, &uy))
                ent_place(E_UNIT, i, ux, uy);
        }
    }
}

/* ======================================================================
   UTILITY
   ====================================================================== */
static int dist2(int x1, int y1, int x2, int y2)
{
    int dx = x1-x2, dy = y1-y2;
    return dx*dx + dy*dy;
}

/* Return entity index of nearest enemy (different civ), or -1. */
static int nearest_enemy(int eid)
{
    Ent *me = &E[eid];
    int best = -1, bd = 1<<30;
    for (int i = 0; i < MAX_E; i++) {
        if (i == eid || !E[i].alive) continue;
        Ent *o = &E[i];
        int is_enemy = (me->civ == -1) ? (o->civ >= 0)   /* monster vs all */
                                       : (o->civ != me->civ); /* civ vs others+monsters */
        if (!is_enemy) continue;
        int d = dist2(me->x, me->y, o->x, o->y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Return entity index of nearest friendly village/city, or -1. */
static int nearest_home(int eid)
{
    Ent *me = &E[eid];
    int best = -1, bd = 1<<30;
    for (int i = 0; i < MAX_E; i++) {
        if (!E[i].alive || E[i].civ != me->civ) continue;
        if (E[i].kind != E_VILLAGE && E[i].kind != E_CITY) continue;
        int d = dist2(me->x, me->y, E[i].x, E[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Move entity one step toward (tx,ty), avoiding impassable terrain. */
static void move_towards(int eid, int tx, int ty)
{
    Ent *e = &E[eid];
    int dx = (tx > e->x) ? 1 : (tx < e->x ? -1 : 0);
    int dy = (ty > e->y) ? 1 : (ty < e->y ? -1 : 0);
    /* Preferred direction first, then alternatives */
    int tries[5][2] = { {dx,dy}, {dx,0}, {0,dy}, {-dy,dx}, {dy,-dx} };
    for (int t = 0; t < 5; t++) {
        int nx = e->x + tries[t][0];
        int ny = e->y + tries[t][1];
        if (nx < 0 || nx >= WW || ny < 0 || ny >= WH) continue;
        Terrain tr = W[ny][nx].t;
        if (tr == T_DEEP || tr == T_WATER || tr == T_MOUNT || tr == T_LAVA) continue;
        if (W[ny][nx].eid >= 0) continue; /* occupied */
        W[e->y][e->x].eid = -1;
        e->x = nx; e->y = ny;
        W[ny][nx].eid = eid;
        return;
    }
}

/* ======================================================================
   COMBAT
   ====================================================================== */
static void do_attack(int attacker, int defender)
{
    if (!E[attacker].alive || !E[defender].alive) return;
    Ent *a = &E[attacker];
    Ent *d = &E[defender];
    int dmg = a->atk + (rand() % 5) - 2;
    if (dmg < 1) dmg = 1;
    d->hp -= dmg;
    if (d->hp <= 0) {
        if (a->civ >= 0 && a->civ < NCIV) C[a->civ].kills++;
        ent_kill(defender);
    }
}

/* ======================================================================
   SIMULATION
   ====================================================================== */
static void sim_unit(int eid)
{
    Ent *e = &E[eid];
    if (e->move_cd > 0) e->move_cd--;
    if (e->atk_cd  > 0) e->atk_cd--;
    e->age++;

    /* Invalidate stale target */
    if (e->target >= 0) {
        Ent *tgt = &E[e->target];
        if (!tgt->alive || tgt->civ == e->civ)
            e->target = -1;
    }

    /* Trigger flee on low HP */
    if (e->hp < e->max_hp / 4 && e->state != S_FLEE)
        e->state = S_FLEE;

    switch (e->state) {
        case S_IDLE: {
            /* Random wander */
            if (e->move_cd == 0) {
                int nx = e->x + (rand()%3) - 1;
                int ny = e->y + (rand()%3) - 1;
                if (nx >= 0 && nx < WW && ny >= 0 && ny < WH) {
                    Terrain tr = W[ny][nx].t;
                    if (tr != T_DEEP && tr != T_WATER && tr != T_MOUNT && tr != T_LAVA
                        && W[ny][nx].eid < 0) {
                        W[e->y][e->x].eid = -1;
                        e->x = nx; e->y = ny;
                        W[ny][nx].eid = eid;
                    }
                }
                e->move_cd = UNIT_MOVE_CD;
            }
            /* Scan for nearby enemies every 5 ticks */
            if (tick % 5 == (eid % 5)) {
                int en = nearest_enemy(eid);
                if (en >= 0 && dist2(e->x, e->y, E[en].x, E[en].y) < ENEMY_DETECT_R2) {
                    e->target = en;
                    e->state  = S_SEEK;
                }
            }
            break;
        }
        case S_SEEK: {
            if (e->target < 0) { e->state = S_IDLE; break; }
            int d = dist2(e->x, e->y, E[e->target].x, E[e->target].y);
            if (d <= 2) {
                e->state = S_ATTACK;
            } else if (e->move_cd == 0) {
                move_towards(eid, E[e->target].x, E[e->target].y);
                e->move_cd = UNIT_MOVE_CD;
            }
            break;
        }
        case S_ATTACK: {
            if (e->target < 0) { e->state = S_IDLE; break; }
            int d = dist2(e->x, e->y, E[e->target].x, E[e->target].y);
            if (d > 2) {
                e->state = S_SEEK;
            } else if (e->atk_cd == 0) {
                do_attack(eid, e->target);
                e->atk_cd = UNIT_ATK_CD;
                if (e->target >= 0 && !E[e->target].alive) {
                    e->target = -1;
                    e->state  = S_IDLE;
                }
            }
            break;
        }
        case S_FLEE: {
            if (e->hp >= e->max_hp / 2) { e->state = S_IDLE; break; }
            if (e->civ < 0) {
                /* monsters: just wander in flee state */
                if (e->move_cd == 0) {
                    int nx = e->x + (rand()%3) - 1;
                    int ny = e->y + (rand()%3) - 1;
                    if (nx >= 0 && nx < WW && ny >= 0 && ny < WH
                        && W[ny][nx].t != T_DEEP && W[ny][nx].t != T_WATER
                        && W[ny][nx].eid < 0) {
                        W[e->y][e->x].eid = -1;
                        e->x = nx; e->y = ny;
                        W[ny][nx].eid = eid;
                    }
                    e->move_cd = UNIT_MOVE_CD;
                }
                break;
            }
            int fv = nearest_home(eid);
            if (fv >= 0 && e->move_cd == 0) {
                move_towards(eid, E[fv].x, E[fv].y);
                e->move_cd = UNIT_MOVE_CD - 1;
                /* Heal at home */
                if (dist2(e->x, e->y, E[fv].x, E[fv].y) < 4) {
                    e->hp    = e->max_hp;
                    e->state = S_IDLE;
                }
            }
            break;
        }
    }
}

static void sim_building(int eid)
{
    Ent *e = &E[eid];
    e->age++;
    if (--e->spawn_timer <= 0) {
        e->spawn_timer = (e->kind == E_CITY) ? CITY_SPAWN_INT : UNIT_SPAWN_INT;
        if (e->civ >= 0 && C[e->civ].units < MAX_UNITS_CIV) {
            int ux = e->x, uy = e->y;
            if (find_nearby_land(&ux, &uy))
                ent_place(E_UNIT, e->civ, ux, uy);
        }
        /* Village → City upgrade */
        if (e->kind == E_VILLAGE && e->age >= VILLAGE_AGE_UP) {
            e->kind        = E_CITY;
            e->max_hp      = CITY_HP;
            e->hp          = CITY_HP;
            e->spawn_timer = CITY_SPAWN_INT;
            /* village count unchanged: cities are still tracked as villages in the UI */
        }
    }
}

static void sim_monster_spawn(void)
{
    if (rand() % 150 != 0) return;
    int x = rand() % WW, y = rand() % WH;
    Terrain t = W[y][x].t;
    if ((t == T_PLAIN || t == T_FOREST) && W[y][x].eid < 0)
        ent_place(E_MONSTER, -1, x, y);
}

static void sim_step(void)
{
    tick++;
    sim_monster_spawn();
    for (int i = 0; i < MAX_E; i++) {
        if (!E[i].alive) continue;
        if (E[i].kind == E_UNIT || E[i].kind == E_MONSTER)
            sim_unit(i);
        else
            sim_building(i);
    }
}

/* ======================================================================
   RENDERING
   ====================================================================== */
static void tile_glyph(int wx, int wy, int *ch, int *cp, int *attr)
{
    *attr = A_NORMAL;
    Tile *t = &W[wy][wx];
    if (t->eid >= 0) {
        Ent *e = &E[t->eid];
        *cp   = (e->civ >= 0 && e->civ < NCIV) ? C[e->civ].cpair : CP_MON;
        *attr = A_BOLD;
        switch (e->kind) {
            case E_UNIT:    *ch = (e->civ < 0) ? 'M' : 'u'; return;
            case E_VILLAGE: *ch = 'V'; return;
            case E_CITY:    *ch = 'C'; return;
            case E_MONSTER: *ch = 'M'; *cp = CP_MON; return;
        }
    }
    switch (t->t) {
        case T_DEEP:   *ch = '~'; *cp = CP_DEEP;   *attr = A_BOLD;   return;
        case T_WATER:  *ch = '~'; *cp = CP_WATER;                     return;
        case T_SAND:   *ch = ','; *cp = CP_SAND;                      return;
        case T_PLAIN:  *ch = '.'; *cp = CP_PLAIN;                     return;
        case T_FOREST: *ch = 'T'; *cp = CP_FOREST;  *attr = A_BOLD;  return;
        case T_MOUNT:  *ch = '^'; *cp = CP_MOUNT;   *attr = A_BOLD;  return;
        case T_LAVA:   *ch = '*'; *cp = CP_LAVA;    *attr = A_BOLD;  return;
        default:       *ch = ' '; *cp = CP_PLAIN;                     return;
    }
}

static const char *TERRAIN_NAMES[T_COUNT] = {
    "Deep Water","Water","Sand","Plains","Forest","Mountain","Lava"
};
static const char *ENTITY_KINDS[] = { "Unit","Village","City","Monster" };
static const char *UNIT_STATES[]  = { "Idle","Seek","Attack","Flee" };
static const char *POWER_NAMES[]  = {
    "","Plains","Water","Forest","Mountain","Lava","Sand",
    "Spawn Unit","Spawn Village","Lightning","Meteor Strike"
};

static void render(void)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int panel_w = 26;
    view_w = cols - panel_w;
    view_h = rows - 2;  /* 2 status lines at bottom */

    /* Clamp camera */
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    if (cam_x > WW - view_w) cam_x = WW - view_w;
    if (cam_y > WH - view_h) cam_y = WH - view_h;
    if (view_w > WW) { view_w = WW; cam_x = 0; }
    if (view_h > WH) { view_h = WH; cam_y = 0; }

    /* ── World view ── */
    for (int sy = 0; sy < view_h; sy++) {
        int wy = cam_y + sy;
        if (wy < 0 || wy >= WH) continue;
        for (int sx = 0; sx < view_w; sx++) {
            int wx = cam_x + sx;
            if (wx < 0 || wx >= WW) continue;
            int ch, cp, at;
            tile_glyph(wx, wy, &ch, &cp, &at);
            if (wx == cur_x && wy == cur_y) {
                attron(COLOR_PAIR(CP_CUR) | A_REVERSE | A_BOLD);
                mvaddch(sy, sx, ch);
                attroff(COLOR_PAIR(CP_CUR) | A_REVERSE | A_BOLD);
            } else {
                attron(COLOR_PAIR(cp) | at);
                mvaddch(sy, sx, ch);
                attroff(COLOR_PAIR(cp) | at);
            }
        }
    }

    /* ── Side panel ── */
    int px = view_w;
    attron(COLOR_PAIR(CP_UI));
    for (int y = 0; y < rows; y++)
        mvhline(y, px, ' ', panel_w);

    mvprintw(0, px+1, "===  GOD-CASA  ===");
    mvprintw(1, px+1, "Tick:  %-7d", tick);
    mvprintw(2, px+1, "State: %s", paused ? "PAUSED " : "Running");
    mvprintw(3, px+1, "Cursor: (%3d,%3d)", cur_x, cur_y);
    mvprintw(4, px+1, "Power: [%d] %s",
             sel_power, POWER_NAMES[sel_power < 11 ? sel_power : 0]);
    mvprintw(5, px+1, "Civ:   [Tab]");

    mvprintw(7, px+1, "-- CIVILISATIONS --");
    for (int i = 0; i < NCIV; i++) {
        if (i == sel_civ) {
            attron(COLOR_PAIR(CP_UI) | A_BOLD);
            mvaddch(8 + i*4, px, '>');
        } else {
            attron(COLOR_PAIR(CP_UI));
        }
        attron(COLOR_PAIR(C[i].cpair) | A_BOLD);
        mvprintw(8  + i*4, px+1, "[%d] %s", i+1, C[i].name);
        attroff(COLOR_PAIR(C[i].cpair) | A_BOLD);
        attron(COLOR_PAIR(CP_UI));
        mvprintw(9  + i*4, px+2, "Uni:%-3d Vil:%-3d", C[i].units, C[i].villages);
        mvprintw(10 + i*4, px+2, "Kills: %-4d", C[i].kills);
        attroff(A_BOLD);
    }

    int py = 8 + NCIV*4 + 1;
    mvprintw(py++, px+1, "-- GOD POWERS --");
    mvprintw(py++, px+1, "1-6: Terrain");
    mvprintw(py++, px+2, "1-Plains 2-Water");
    mvprintw(py++, px+2, "3-Forest 4-Mount");
    mvprintw(py++, px+2, "5-Lava   6-Sand");
    mvprintw(py++, px+1, "7: Spawn Unit");
    mvprintw(py++, px+1, "8: Spawn Village");
    mvprintw(py++, px+1, "9: Lightning");
    mvprintw(py++, px+1, "0: Meteor Strike");
    py++;
    mvprintw(py++, px+1, "Enter/F: Apply");
    mvprintw(py++, px+1, "Arrows: Cursor");
    mvprintw(py++, px+1, "WASD: Camera");
    mvprintw(py++, px+1, "Tab: Civ  Spc:Pause");
    mvprintw(py++, px+1, "Q: Quit");
    attroff(COLOR_PAIR(CP_UI));

    /* ── Bottom status bar ── */
    int br = rows - 2;
    attron(COLOR_PAIR(CP_UI) | A_BOLD);
    mvhline(br, 0, ' ', cols);
    mvprintw(br, 0, " [%d] %-14s | Civ: %-7s | Tick: %-6d | %s",
             sel_power, POWER_NAMES[sel_power < 11 ? sel_power : 0],
             C[sel_civ].name, tick, paused ? "PAUSED" : "Running");
    attroff(COLOR_PAIR(CP_UI) | A_BOLD);

    /* ── Entity / terrain info bar ── */
    br++;
    attron(COLOR_PAIR(CP_UI));
    mvhline(br, 0, ' ', cols);
    if (cur_x >= 0 && cur_x < WW && cur_y >= 0 && cur_y < WH) {
        int eid = W[cur_y][cur_x].eid;
        if (eid >= 0) {
            Ent *e = &E[eid];
            mvprintw(br, 0, " (%d,%d) %s %s  HP:%d/%d ATK:%d  %s",
                     cur_x, cur_y,
                     (e->civ >= 0 && e->civ < NCIV) ? C[e->civ].name : "Monster",
                     ENTITY_KINDS[e->kind],
                     e->hp, e->max_hp, e->atk,
                     (e->kind == E_UNIT || e->kind == E_MONSTER)
                         ? UNIT_STATES[e->state] : "");
        } else {
            mvprintw(br, 0, " (%d,%d) %s",
                     cur_x, cur_y, TERRAIN_NAMES[W[cur_y][cur_x].t]);
        }
    }
    attroff(COLOR_PAIR(CP_UI));

    refresh();
}

/* ======================================================================
   GOD POWERS
   ====================================================================== */
static void meteor_strike(int wx, int wy)
{
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx*dx + dy*dy > 9) continue;
            int nx = wx+dx, ny = wy+dy;
            if (nx < 0 || nx >= WW || ny < 0 || ny >= WH) continue;
            if (W[ny][nx].eid >= 0) ent_kill(W[ny][nx].eid);
            W[ny][nx].t = T_LAVA;
        }
    }
}

static void apply_power(int wx, int wy)
{
    if (wx < 0 || wx >= WW || wy < 0 || wy >= WH) return;
    switch (sel_power) {
        case 1: W[wy][wx].t = T_PLAIN;  break;
        case 2:
            if (W[wy][wx].eid >= 0) ent_kill(W[wy][wx].eid);
            W[wy][wx].t = T_WATER;
            break;
        case 3: W[wy][wx].t = T_FOREST; break;
        case 4:
            if (W[wy][wx].eid >= 0) ent_kill(W[wy][wx].eid);
            W[wy][wx].t = T_MOUNT;
            break;
        case 5:
            if (W[wy][wx].eid >= 0) ent_kill(W[wy][wx].eid);
            W[wy][wx].t = T_LAVA;
            break;
        case 6: W[wy][wx].t = T_SAND;   break;
        case 7: { /* Spawn unit */
            Terrain t = W[wy][wx].t;
            if (t != T_DEEP && t != T_WATER && t != T_MOUNT && t != T_LAVA
                && W[wy][wx].eid < 0)
                ent_place(E_UNIT, sel_civ, wx, wy);
            break;
        }
        case 8: { /* Spawn village */
            Terrain t = W[wy][wx].t;
            if ((t == T_PLAIN || t == T_FOREST || t == T_SAND) && W[wy][wx].eid < 0)
                ent_place(E_VILLAGE, sel_civ, wx, wy);
            break;
        }
        case 9: /* Lightning — destroy entity */
            if (W[wy][wx].eid >= 0) ent_kill(W[wy][wx].eid);
            break;
        case 10: /* Meteor strike */
            meteor_strike(wx, wy);
            break;
    }
}

/* ======================================================================
   INPUT
   ====================================================================== */
static void handle_input(int ch)
{
    switch (ch) {
        /* Camera pan */
        case 'w': case 'W': cam_y--; break;
        case 's': case 'S': cam_y++; break;
        case 'a': case 'A': cam_x--; break;
        case 'd': case 'D': cam_x++; break;
        /* Cursor */
        case KEY_UP:    cur_y--; break;
        case KEY_DOWN:  cur_y++; break;
        case KEY_LEFT:  cur_x--; break;
        case KEY_RIGHT: cur_x++; break;
        /* Power selection */
        case '1': sel_power = 1;  break;
        case '2': sel_power = 2;  break;
        case '3': sel_power = 3;  break;
        case '4': sel_power = 4;  break;
        case '5': sel_power = 5;  break;
        case '6': sel_power = 6;  break;
        case '7': sel_power = 7;  break;
        case '8': sel_power = 8;  break;
        case '9': sel_power = 9;  break;
        case '0': sel_power = 10; break;
        /* Civilisation cycle */
        case '\t': sel_civ = (sel_civ + 1) % NCIV; break;
        /* Civ direct select */
        case '!': sel_civ = 0; break; /* shift-1 on some terminals */
        /* Pause */
        case ' ': paused = !paused; break;
        /* Quit */
        case 'q': case 'Q': quitting = 1; break;
        /* Apply power */
        case '\n': case '\r': case 'f': case 'F':
            apply_power(cur_x, cur_y);
            break;
    }

    /* Clamp cursor to world bounds */
    if (cur_x < 0)    cur_x = 0;
    if (cur_y < 0)    cur_y = 0;
    if (cur_x >= WW)  cur_x = WW - 1;
    if (cur_y >= WH)  cur_y = WH - 1;

    /* Auto-scroll camera to keep cursor visible */
    if (cur_x < cam_x)              cam_x = cur_x;
    if (cur_y < cam_y)              cam_y = cur_y;
    if (cur_x >= cam_x + view_w)    cam_x = cur_x - view_w + 1;
    if (cur_y >= cam_y + view_h)    cam_y = cur_y - view_h + 1;
}

/* ======================================================================
   NCURSES SETUP
   ====================================================================== */
static void ncurses_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();

    init_pair(CP_DEEP,   COLOR_BLUE,    COLOR_BLACK);
    init_pair(CP_WATER,  COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_SAND,   COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_PLAIN,  COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_FOREST, COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_MOUNT,  COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_LAVA,   COLOR_RED,     COLOR_BLACK);
    init_pair(CP_CIV0,   COLOR_RED,     COLOR_BLACK);
    init_pair(CP_CIV1,   COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_CIV2,   COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_CIV3,   COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_MON,    COLOR_RED,     COLOR_BLACK);
    init_pair(CP_CUR,    COLOR_WHITE,   COLOR_WHITE);
    init_pair(CP_UI,     COLOR_WHITE,   COLOR_BLACK);
}

/* ======================================================================
   MAIN
   ====================================================================== */
int main(void)
{
    srand((unsigned)time(NULL));

    memset(W, 0, sizeof(W));
    memset(E, 0, sizeof(E));
    memset(C, 0, sizeof(C));

    world_gen();
    civs_init();

    ncurses_init();

    cam_x = WW/2 - 30;
    cam_y = WH/2 - 15;
    cur_x = WW/2;
    cur_y = WH/2;

    struct timespec frame_time = { 0, 50000000L }; /* 50 ms → ~20 fps */

    while (!quitting) {
        int ch = getch();
        if (ch != ERR) handle_input(ch);
        if (!paused) sim_step();
        render();
        nanosleep(&frame_time, NULL);
    }

    endwin();
    printf("Thanks for playing god-casa!\n\n");
    printf("Final standings:\n");
    for (int i = 0; i < NCIV; i++) {
        printf("  %-8s  units:%-4d  villages:%-4d  kills:%-4d\n",
               C[i].name, C[i].units, C[i].villages, C[i].kills);
    }
    return 0;
}
