// Copyright (c) dobbypr. All rights reserved.
// Unauthorized copying or distribution of this file, via any medium, is strictly prohibited.
// See the LICENSE file for permitted use.

/*
 * simulation.h — Data-Oriented Design (DOD) simulation formulas
 *
 * Implements 100 batch-processing functions across 10 simulation categories
 * using Structure of Arrays (SoA) layouts for cache-friendly iteration.
 *
 * Categories:
 *  1. Population Dynamics
 *  2. Faith & Religion
 *  3. Combat & Warfare
 *  4. Economy & Resources
 *  5. Environment & Weather
 *  6. Movement & AI
 *  7. Divine Powers
 *  8. NPC Psychology
 *  9. Progression & Tech
 * 10. Engine & End Game
 */

#ifndef SIMULATION_H
#define SIMULATION_H

#include <stdint.h>

/* Incremented each game tick; XORed into LCG seeds so that roll results
   vary between ticks for the same entity index. */
extern uint32_t global_tick;

/* ======================================================================
   1. POPULATION DYNAMICS — SoA
   ====================================================================== */
typedef struct {
    float *population;      /* current population count                    */
    float *carrying_cap;    /* carrying capacity K                         */
    float *growth_rate;     /* intrinsic growth rate r                     */
    float *susceptible;     /* SIR model: susceptible fraction             */
    float *infected;        /* SIR model: infected fraction                */
    float *recovered;       /* SIR model: recovered fraction               */
    float *beta;            /* SIR model: transmission rate                */
    float *gamma_rec;       /* SIR model: recovery rate                    */
    float *food_supply;     /* available food units                        */
    float *food_threshold;  /* minimum food to avoid starvation            */
    float *age_young;       /* fraction in young cohort                    */
    float *age_adult;       /* fraction in adult cohort                    */
    float *age_elder;       /* fraction in elder cohort                    */
    int    count;           /* number of population groups                 */
} PopSoA;

/* ======================================================================
   2. FAITH & RELIGION — SoA
   ====================================================================== */
typedef struct {
    float *faith_level;     /* current faith strength (0..1)               */
    float *mana;            /* divine mana pool                            */
    float *mana_regen;      /* mana regen rate per tick                    */
    float *heresy_rate;     /* rate at which heresy spreads                */
    float *miracle_chance;  /* base probability a miracle triggers         */
    float *devotee_count;   /* number of active devotees                   */
    float *temple_count;    /* number of temples providing a bonus         */
    float *schism_risk;     /* accumulated schism pressure (0..1)          */
    float *conversion_rate; /* rate of converting non-believers            */
    float *divine_favor;    /* current favor with the deity (0..1)         */
    int    count;           /* number of religious factions                */
} FaithSoA;

/* ======================================================================
   3. COMBAT & WARFARE — SoA
   ====================================================================== */
typedef struct {
    float *base_atk;        /* base attack power                           */
    float *armor;           /* armor rating                                */
    float *hp;              /* current hit points                          */
    float *max_hp;          /* maximum hit points                          */
    float *morale;          /* unit morale (0..1)                          */
    float *morale_decay;    /* morale decay rate per tick                  */
    float *hit_chance;      /* base hit probability (0..1)                 */
    float *crit_chance;     /* critical hit probability (0..1)             */
    float *crit_mult;       /* critical damage multiplier                  */
    float *rout_threshold;  /* morale below which the unit routs           */
    int    count;           /* number of combat units                      */
} CombatSoA;

/* ======================================================================
   4. ECONOMY & RESOURCES — SoA
   ====================================================================== */
typedef struct {
    float *resource;        /* current stockpile                           */
    float *max_resource;    /* maximum stockpile capacity                  */
    float *gather_rate;     /* units gathered per tick                     */
    float *depletion_rate;  /* natural depletion per tick                  */
    float *price;           /* current market price per unit               */
    float *demand;          /* current demand level                        */
    float *supply;          /* current supply level                        */
    float *tax_rate;        /* tax fraction (0..1)                         */
    float *tax_collected;   /* accumulated tax revenue                     */
    float *trade_volume;    /* volume of trade processed last tick         */
    int    count;           /* number of resource pools                    */
} EconSoA;

/* ======================================================================
   5. ENVIRONMENT & WEATHER — SoA
   ====================================================================== */
typedef struct {
    float *temperature;     /* current temperature                         */
    float *temp_target;     /* equilibrium temperature                     */
    float *rainfall;        /* current rainfall level                      */
    float *humidity;        /* humidity fraction (0..1)                    */
    float *wind_x;          /* wind vector x-component                     */
    float *wind_y;          /* wind vector y-component                     */
    float *fire_intensity;  /* active fire intensity                       */
    float *fuel;            /* combustible material remaining              */
    float *elevation;       /* terrain elevation                           */
    float *pressure;        /* atmospheric pressure                        */
    int    count;           /* number of tiles/cells                       */
} EnvSoA;

/* ======================================================================
   6. MOVEMENT & AI — SoA
   ====================================================================== */
typedef struct {
    float *pos_x;           /* x world position                            */
    float *pos_y;           /* y world position                            */
    float *vel_x;           /* x velocity component                        */
    float *vel_y;           /* y velocity component                        */
    float *acc_x;           /* x acceleration component                    */
    float *acc_y;           /* y acceleration component                    */
    float *heading;         /* facing angle in radians                     */
    float *speed;           /* current scalar speed                        */
    float *max_speed;       /* speed cap                                   */
    float *h_cost;          /* A* heuristic cost from last evaluation      */
    int    count;           /* number of mobile agents                     */
} MoveSoA;

/* ======================================================================
   7. DIVINE POWERS — SoA
   ====================================================================== */
typedef struct {
    float *energy;          /* divine energy stored                        */
    float *energy_cap;      /* maximum energy capacity                     */
    float *regen_rate;      /* energy regenerated per tick                 */
    float *meteor_cost;     /* energy cost to call a meteor                */
    float *heal_amount;     /* current heal strength                       */
    float *heal_decay;      /* rate at which heal effectiveness fades      */
    float *terraform_cost;  /* energy cost per tile terraformed            */
    float *smite_power;     /* base smite damage                           */
    float *blessing_mult;   /* stat multiplier applied by a blessing       */
    float *cooldown;        /* remaining cooldown ticks before reuse       */
    int    count;           /* number of gods / divine actors              */
} DivineSoA;

/* ======================================================================
   8. NPC PSYCHOLOGY — SoA
   ====================================================================== */
typedef struct {
    float *happiness;       /* general wellbeing (0..1)                    */
    float *fear;            /* current fear level (0..1)                   */
    float *loyalty;         /* loyalty to current faction (0..1)           */
    float *aggression;      /* aggression tendency (0..1)                  */
    float *utility_work;    /* utility score for working                   */
    float *utility_fight;   /* utility score for fighting                  */
    float *utility_flee;    /* utility score for fleeing                   */
    float *threat_level;    /* perceived incoming threat (0..1)            */
    float *memory_decay;    /* rate at which events fade from memory       */
    float *social_bond;     /* social bond strength (0..1)                 */
    int    count;           /* number of NPCs                              */
} PsychSoA;

/* ======================================================================
   9. PROGRESSION & TECH — SoA
   ====================================================================== */
typedef struct {
    float *research_pts;    /* accumulated research points                 */
    float *research_rate;   /* research points generated per tick          */
    float *tech_cost;       /* cost to reach next tech level               */
    float *tech_level;      /* current integer tech level (as float)       */
    float *golden_age_mult; /* research/culture multiplier in golden ages  */
    float *golden_age_timer;/* ticks remaining in current golden age       */
    float *culture;         /* cultural advancement score                  */
    float *culture_spread;  /* rate at which culture spreads outward       */
    float *era;             /* current era index (integer as float)        */
    float *pop_bonus;       /* population-derived research bonus           */
    int    count;           /* number of civilisations                     */
} TechSoA;

/* ======================================================================
   10. ENGINE & END GAME — SoA
   ====================================================================== */
typedef struct {
    float    *entropy;      /* chaos / entropy level (0..1)                */
    float    *entropy_rate; /* rate of entropy increase per tick           */
    float    *grid_x;       /* spatial hash grid x-bucket index            */
    float    *grid_y;       /* spatial hash grid y-bucket index            */
    float    *inv_sqrt_val; /* input values for fast inverse-sqrt          */
    float    *inv_sqrt_out; /* output results from fast inverse-sqrt       */
    float    *stability;    /* world stability (0..1)                      */
    float    *end_timer;    /* countdown ticks to an end condition         */
    float    *victory_pts;  /* victory points per faction                  */
    float    *chaos_mult;   /* chaos multiplier applied to random events   */
    uint32_t *rng_state;    /* per-faction deterministic RNG state         */
    int       count;        /* number of factions / engine slots           */
} EngineSoA;

/* ======================================================================
   FUNCTION DECLARATIONS — 100 total (10 per category)
   ====================================================================== */

/* --- 1. Population Dynamics --- */
void pop_logistic_growth(PopSoA *p, float dt);
void pop_sir_step(PopSoA *p, float dt);
void pop_starvation(PopSoA *p, float dt);
void pop_age_cohort_shift(PopSoA *p, float dt);
void pop_birth_rate(PopSoA *p, float dt);
void pop_death_rate(PopSoA *p, float dt);
void pop_migration(PopSoA *src, PopSoA *dst, int idx, float rate, float dt);
void pop_carrying_cap_pressure(PopSoA *p);
void pop_epidemic_mortality(PopSoA *p, float mortality_rate, float dt);
void pop_recovery_bonus(PopSoA *p, float dt);

/* --- 2. Faith & Religion --- */
void faith_generate(FaithSoA *f, float dt);
void faith_mana_regen(FaithSoA *f, float dt);
void faith_heresy_spread(FaithSoA *f, float dt);
void faith_miracle_check(FaithSoA *f, int *miracle_out);
void faith_conversion_tick(FaithSoA *f, float dt);
void faith_schism_accumulate(FaithSoA *f, float dt);
void faith_divine_favor_update(FaithSoA *f, float piety_delta);
void faith_temple_bonus(FaithSoA *f);
void faith_ritual_cost(FaithSoA *f, int idx, float ritual_mana);
void faith_devotee_update(FaithSoA *f, float dt);

/* --- 3. Combat & Warfare --- */
void combat_apply_damage(CombatSoA *c, int attacker, int defender, float raw_dmg);
void combat_armor_mitigation(const CombatSoA *c, float *dmg_inout);
void combat_hit_roll(const CombatSoA *c, int attacker, int *hit_out);
void combat_crit_roll(const CombatSoA *c, int attacker, float *dmg_mult_out);
void combat_morale_decay(CombatSoA *c, float dt);
void combat_morale_boost(CombatSoA *c, int unit, float amount);
void combat_rout_check(const CombatSoA *c, int *rout_flags);
void combat_hp_regen(CombatSoA *c, float regen_rate, float dt);
void combat_aoe_damage(CombatSoA *c, const float *pos_x, const float *pos_y,
                       float cx, float cy, float radius, float dmg);
void combat_siege_damage(CombatSoA *c, int building, float siege_power, float dt);

/* --- 4. Economy & Resources --- */
void econ_gather(EconSoA *e, float dt);
void econ_deplete(EconSoA *e, float dt);
void econ_market_price(EconSoA *e);
void econ_collect_tax(EconSoA *e, const float *population);
void econ_trade(EconSoA *seller, int si, EconSoA *buyer, int bi, float amount);
void econ_resource_cap(EconSoA *e);
void econ_demand_update(EconSoA *e, float population_delta);
void econ_supply_shock(EconSoA *e, float shock_factor);
void econ_inflation(EconSoA *e, float inflation_rate, float dt);
void econ_scarcity_penalty(const EconSoA *e, float *output_mult);

/* --- 5. Environment & Weather --- */
void env_temperature_diffuse(EnvSoA *e, float rate, float dt);
void env_rainfall_update(EnvSoA *e, float dt);
void env_fire_spread(EnvSoA *e, float spread_prob, float dt);
void env_fire_consume(EnvSoA *e, float dt);
void env_humidity_evaporate(EnvSoA *e, float dt);
void env_wind_advect(EnvSoA *e, float dt);
void env_pressure_gradient(EnvSoA *e);
void env_elevation_temp_bias(EnvSoA *e);
void env_drought_check(const EnvSoA *e, float threshold, int *drought_flags);
void env_flood_check(const EnvSoA *e, float threshold, int *flood_flags);

/* --- 6. Movement & AI --- */
void move_velocity_verlet(MoveSoA *m, float dt);
void move_flock_separation(MoveSoA *m, float radius, float strength);
void move_flock_alignment(MoveSoA *m, float radius, float strength);
void move_flock_cohesion(MoveSoA *m, float radius, float strength);
void move_seek_target(MoveSoA *m, int unit, float tx, float ty, float strength);
void move_flee_target(MoveSoA *m, int unit, float tx, float ty, float strength);
void move_astar_heuristic(MoveSoA *m, int unit, float gx, float gy);
void move_clamp_speed(MoveSoA *m);
void move_heading_update(MoveSoA *m);
void move_arrival_brake(MoveSoA *m, int unit, float tx, float ty, float slow_radius);

/* --- 7. Divine Powers --- */
void divine_energy_regen(DivineSoA *d, const FaithSoA *f, float dt);
void divine_meteor_cost(DivineSoA *d, int god, int *can_cast);
void divine_heal_apply(DivineSoA *d, CombatSoA *c, int god, int target_unit);
void divine_heal_decay(DivineSoA *d, float dt);
void divine_terraform_cost(DivineSoA *d, int god, int tiles, int *can_cast);
void divine_smite(DivineSoA *d, CombatSoA *c, int god, int target);
void divine_blessing(DivineSoA *d, CombatSoA *c, int god, int target);
void divine_cooldown_tick(DivineSoA *d, float dt);
void divine_energy_cap(DivineSoA *d);
void divine_favor_scale(DivineSoA *d, const FaithSoA *f);

/* --- 8. NPC Psychology --- */
void psych_utility_evaluate(PsychSoA *p);
void psych_threat_assess(PsychSoA *p, const CombatSoA *c, int npc, int threat_unit);
void psych_loyalty_shift(PsychSoA *p, int npc, float event_loyalty_delta);
void psych_fear_decay(PsychSoA *p, float dt);
void psych_happiness_update(PsychSoA *p, const EconSoA *e);
void psych_aggression_trigger(PsychSoA *p, int npc, float provocation);
void psych_social_bond_update(PsychSoA *p, float dt);
void psych_memory_fade(PsychSoA *p, float dt);
void psych_morale_from_psych(const PsychSoA *p, CombatSoA *c);
void psych_defection_check(const PsychSoA *p, int *defect_flags);

/* --- 9. Progression & Tech --- */
void tech_research_tick(TechSoA *t, const PopSoA *p, float dt);
void tech_cost_scale(TechSoA *t);
void tech_unlock_check(TechSoA *t, int *unlock_flags);
void tech_golden_age_tick(TechSoA *t, float dt);
void tech_golden_age_trigger(TechSoA *t, int nation, float threshold);
void tech_culture_spread(TechSoA *t, float dt);
void tech_era_advance(TechSoA *t);
void tech_pop_research_bonus(TechSoA *t, const PopSoA *p);
void tech_decay(TechSoA *t, float dt);
void tech_diffusion(const TechSoA *src, TechSoA *dst, int si, int di, float rate, float dt);

/* --- 10. Engine & End Game --- */
void engine_fast_inv_sqrt(EngineSoA *e);
void engine_entropy_increase(EngineSoA *e, float dt);
void engine_stability_update(EngineSoA *e, const PopSoA *p, const TechSoA *t);
void engine_spatial_grid_assign(EngineSoA *e, const MoveSoA *m, float cell_size);
void engine_end_timer_tick(EngineSoA *e, float dt);
void engine_victory_pts_update(EngineSoA *e, const PopSoA *p, const TechSoA *t);
void engine_chaos_event(EngineSoA *e, int faction);
void engine_entropy_reset(EngineSoA *e, int faction);
void engine_determinism_seed(EngineSoA *e, int faction, uint32_t seed);
void engine_end_condition_check(const EngineSoA *e, int *end_flags);

#endif /* SIMULATION_H */
