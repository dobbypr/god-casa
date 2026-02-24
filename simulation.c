// Copyright (c) dobbypr. All rights reserved.
// Unauthorized copying or distribution of this file, via any medium, is strictly prohibited.
// See the LICENSE file for permitted use.

/*
 * simulation.c — Batch-processing implementations for all 100 DOD formulas.
 *
 * Every function iterates over the SoA arrays in a tight loop for cache
 * locality.  No global state is used; all data lives in the caller-supplied
 * SoA structs.
 */

#include "simulation.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ======================================================================
   INTERNAL HELPERS
   ====================================================================== */

/* Maximum allowed market price — prevents multiplicative divergence to infinity. */
#define MAX_PRICE 1000.0f

/* Clamp a float to [lo, hi]. */
static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Fast inverse square root (Quake-style) — avoids UB via memcpy. */
static float fast_inv_sqrt_scalar(float x)
{
    float y;
    uint32_t i;
    memcpy(&i, &x, sizeof(i));
    i = 0x5f3759dfu - (i >> 1);
    memcpy(&y, &i, sizeof(y));
    /* one Newton–Raphson refinement */
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

/* Linear-congruential generator step for deterministic noise. */
static uint32_t lcg_next(uint32_t s)
{
    return s * 1664525u + 1013904223u;
}

/* LCG-derived float in [0, 1). */
static float lcg_float(uint32_t *s)
{
    *s = lcg_next(*s);
    return (float)(*s >> 8) / (float)(1u << 24);
}

/* ======================================================================
   1. POPULATION DYNAMICS
   ====================================================================== */

/*
 * pop_logistic_growth — Verhulst logistic model.
 *   dN/dt = r * N * (1 - N/K)
 */
void pop_logistic_growth(PopSoA *p, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float n = p->population[i];
        float k = p->carrying_cap[i];
        float r = p->growth_rate[i];
        float dn = r * n * (1.0f - n / k);
        p->population[i] = clampf(n + dn * dt, 0.0f, k);
    }
}

/*
 * pop_sir_step — Compartmental SIR disease model.
 *   dS = -beta*S*I/N
 *   dI =  beta*S*I/N - gamma*I
 *   dR =  gamma*I
 * Fractions S+I+R are kept normalised to 1.
 */
void pop_sir_step(PopSoA *p, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float n = p->population[i];
        if (n <= 0.0f) continue;
        float s = p->susceptible[i];
        float inf = p->infected[i];
        float r = p->recovered[i];
        float beta = p->beta[i];
        float gam  = p->gamma_rec[i];

        float new_inf = beta * s * inf / n;
        float new_rec = gam * inf;

        s   -= new_inf * dt;
        inf += (new_inf - new_rec) * dt;
        r   += new_rec * dt;

        /* normalise to avoid drift */
        float total = s + inf + r;
        if (total > 0.0f) {
            p->susceptible[i] = clampf(s   / total, 0.0f, 1.0f);
            p->infected[i]    = clampf(inf / total, 0.0f, 1.0f);
            p->recovered[i]   = clampf(r   / total, 0.0f, 1.0f);
        }
    }
}

/*
 * pop_starvation — Reduce population when food falls below the threshold.
 *   rate proportional to the food deficit.
 */
void pop_starvation(PopSoA *p, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float deficit = p->food_threshold[i] - p->food_supply[i];
        if (deficit <= 0.0f) continue;
        float frac = deficit / p->food_threshold[i];
        p->population[i] = clampf(
            p->population[i] - p->population[i] * frac * 0.05f * dt,
            0.0f, p->carrying_cap[i]);
    }
}

/*
 * pop_age_cohort_shift — Advance individuals through Young→Adult→Elder.
 *   A fixed fraction moves to the next cohort each tick.
 */
void pop_age_cohort_shift(PopSoA *p, float dt)
{
    const float shift_rate = 0.002f; /* fraction per unit time */
    for (int i = 0; i < p->count; i++) {
        float young = p->age_young[i];
        float adult = p->age_adult[i];
        float elder = p->age_elder[i];

        float ya = young * shift_rate * dt; /* young → adult */
        float ae = adult * shift_rate * dt; /* adult → elder */

        p->age_young[i] = clampf(young - ya,        0.0f, 1.0f);
        p->age_adult[i] = clampf(adult + ya - ae,   0.0f, 1.0f);
        p->age_elder[i] = clampf(elder + ae,         0.0f, 1.0f);
    }
}

/*
 * pop_birth_rate — New individuals from the adult cohort.
 *   births = birth_coeff * age_adult * population * dt
 */
void pop_birth_rate(PopSoA *p, float dt)
{
    const float birth_coeff = 0.03f;
    for (int i = 0; i < p->count; i++) {
        float births = birth_coeff * p->age_adult[i] * p->population[i] * dt;
        p->age_young[i]  = clampf(p->age_young[i] + births / (p->population[i] + 1.0f),
                                   0.0f, 1.0f);
        p->population[i] = clampf(p->population[i] + births, 0.0f, p->carrying_cap[i]);
    }
}

/*
 * pop_death_rate — Natural mortality, elevated for the elder cohort.
 *   deaths = (base + elder_excess * age_elder) * population * dt
 */
void pop_death_rate(PopSoA *p, float dt)
{
    const float base_death   = 0.01f;
    const float elder_excess = 0.04f;
    for (int i = 0; i < p->count; i++) {
        float rate   = base_death + elder_excess * p->age_elder[i];
        float deaths = rate * p->population[i] * dt;
        p->population[i] = clampf(p->population[i] - deaths, 0.0f, p->carrying_cap[i]);
    }
}

/*
 * pop_migration — Move a fraction of one group's population to another.
 *   amount = rate * src->population[idx] * dt
 */
void pop_migration(PopSoA *src, PopSoA *dst, int idx, float rate, float dt)
{
    if (idx < 0 || idx >= src->count || idx >= dst->count) return;
    float amount = rate * src->population[idx] * dt;
    src->population[idx] = clampf(src->population[idx] - amount, 0.0f, src->carrying_cap[idx]);
    dst->population[idx] = clampf(dst->population[idx] + amount, 0.0f, dst->carrying_cap[idx]);
}

/*
 * pop_carrying_cap_pressure — Hard-clamp population to carrying capacity.
 *   Also scales food_threshold proportionally to K.
 */
void pop_carrying_cap_pressure(PopSoA *p)
{
    for (int i = 0; i < p->count; i++) {
        if (p->population[i] > p->carrying_cap[i])
            p->population[i] = p->carrying_cap[i];
        p->food_threshold[i] = p->carrying_cap[i] * 0.1f;
    }
}

/*
 * pop_epidemic_mortality — Infected population dies at mortality_rate per tick.
 */
void pop_epidemic_mortality(PopSoA *p, float mortality_rate, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float deaths = mortality_rate * p->infected[i] * p->population[i] * dt;
        p->population[i] = clampf(p->population[i] - deaths, 0.0f, p->carrying_cap[i]);
    }
}

/*
 * pop_recovery_bonus — Small population growth bonus for recovered individuals.
 *   Represents herd immunity boosting survivors.
 */
void pop_recovery_bonus(PopSoA *p, float dt)
{
    const float bonus = 0.005f;
    for (int i = 0; i < p->count; i++) {
        float gain = bonus * p->recovered[i] * p->population[i] * dt;
        p->population[i] = clampf(p->population[i] + gain, 0.0f, p->carrying_cap[i]);
    }
}

/* ======================================================================
   2. FAITH & RELIGION
   ====================================================================== */

/*
 * faith_generate — Faith grows proportionally to devotees and temples.
 *   d(faith)/dt = devotees * (1 + temple_count * 0.1) * 0.001
 */
void faith_generate(FaithSoA *f, float dt)
{
    for (int i = 0; i < f->count; i++) {
        float gain = f->devotee_count[i] * (1.0f + f->temple_count[i] * 0.1f) * 0.001f * dt;
        f->faith_level[i] = clampf(f->faith_level[i] + gain, 0.0f, 1.0f);
    }
}

/*
 * faith_mana_regen — Mana regenerates faster with higher divine favor.
 *   mana += mana_regen * divine_favor * dt
 */
void faith_mana_regen(FaithSoA *f, float dt)
{
    for (int i = 0; i < f->count; i++) {
        float gain = f->mana_regen[i] * f->divine_favor[i] * dt;
        f->mana[i] = clampf(f->mana[i] + gain, 0.0f, 1000.0f);
    }
}

/*
 * faith_heresy_spread — Heresy grows logistically among low-faith populations.
 *   d(heresy)/dt = heresy_rate * (1 - faith_level) * heresy * (1 - heresy)
 *   Heresy is tracked implicitly as (1 - faith_level).
 */
void faith_heresy_spread(FaithSoA *f, float dt)
{
    for (int i = 0; i < f->count; i++) {
        float heresy = 1.0f - f->faith_level[i];
        float d = f->heresy_rate[i] * (1.0f - f->faith_level[i]) * heresy * (1.0f - heresy);
        heresy = clampf(heresy + d * dt, 0.0f, 1.0f);
        f->faith_level[i] = 1.0f - heresy;
    }
}

/*
 * faith_miracle_check — Set miracle_out[i] = 1 if a miracle triggers.
 *   Probability = miracle_chance * divine_favor.
 *   Uses a simple LCG keyed to the index for deterministic output.
 */
void faith_miracle_check(FaithSoA *f, int *miracle_out)
{
    for (int i = 0; i < f->count; i++) {
        uint32_t seed = (uint32_t)(i + 1) * 2654435761u;
        float roll = lcg_float(&seed);
        miracle_out[i] = (roll < f->miracle_chance[i] * f->divine_favor[i]) ? 1 : 0;
    }
}

/*
 * faith_conversion_tick — Convert non-devotees at conversion_rate * faith.
 *   devotee_count grows toward a cap proportional to faith.
 */
void faith_conversion_tick(FaithSoA *f, float dt)
{
    const float pop_cap = 1000.0f;
    for (int i = 0; i < f->count; i++) {
        float target = pop_cap * f->faith_level[i];
        float delta  = f->conversion_rate[i] * (target - f->devotee_count[i]) * dt;
        f->devotee_count[i] = clampf(f->devotee_count[i] + delta, 0.0f, pop_cap);
    }
}

/*
 * faith_schism_accumulate — Schism risk rises when heresy is high.
 *   d(schism_risk)/dt = (1 - faith_level) * 0.01
 */
void faith_schism_accumulate(FaithSoA *f, float dt)
{
    for (int i = 0; i < f->count; i++) {
        float rise = (1.0f - f->faith_level[i]) * 0.01f * dt;
        f->schism_risk[i] = clampf(f->schism_risk[i] + rise, 0.0f, 1.0f);
    }
}

/*
 * faith_divine_favor_update — Adjust all factions' divine favor by piety_delta.
 */
void faith_divine_favor_update(FaithSoA *f, float piety_delta)
{
    for (int i = 0; i < f->count; i++)
        f->divine_favor[i] = clampf(f->divine_favor[i] + piety_delta, 0.0f, 1.0f);
}

/*
 * faith_temple_bonus — Recalculate miracle_chance from temple_count.
 *   miracle_chance = base * (1 + temple_count * 0.05)
 */
void faith_temple_bonus(FaithSoA *f)
{
    const float base_miracle = 0.01f;
    for (int i = 0; i < f->count; i++)
        f->miracle_chance[i] = base_miracle * (1.0f + f->temple_count[i] * 0.05f);
}

/*
 * faith_ritual_cost — Deduct ritual_mana from faction idx's mana pool.
 */
void faith_ritual_cost(FaithSoA *f, int idx, float ritual_mana)
{
    if (idx < 0 || idx >= f->count) return;
    f->mana[idx] = clampf(f->mana[idx] - ritual_mana, 0.0f, 1000.0f);
}

/*
 * faith_devotee_update — Devotees slowly drift toward faith_level * 1000.
 */
void faith_devotee_update(FaithSoA *f, float dt)
{
    const float target_scale = 1000.0f;
    const float drift_rate   = 0.05f;
    for (int i = 0; i < f->count; i++) {
        float target = f->faith_level[i] * target_scale;
        f->devotee_count[i] += drift_rate * (target - f->devotee_count[i]) * dt;
        f->devotee_count[i]  = clampf(f->devotee_count[i], 0.0f, target_scale);
    }
}

/* ======================================================================
   3. COMBAT & WARFARE
   ====================================================================== */

/*
 * combat_apply_damage — Deal raw_dmg to defender, reduced by half their armour.
 *   Minimum 1 damage always applied.
 */
void combat_apply_damage(CombatSoA *c, int attacker, int defender, float raw_dmg)
{
    if (attacker < 0 || attacker >= c->count) return;
    if (defender < 0 || defender >= c->count) return;
    (void)attacker;
    float dmg = raw_dmg - c->armor[defender] * 0.5f;
    if (dmg < 1.0f) dmg = 1.0f;
    c->hp[defender] = clampf(c->hp[defender] - dmg, 0.0f, c->max_hp[defender]);
}

/*
 * combat_armor_mitigation — Reduce each element of dmg_inout using the
 *   standard mitigation formula: mitigated = raw * armor / (armor + 100).
 */
void combat_armor_mitigation(const CombatSoA *c, float *dmg_inout)
{
    for (int i = 0; i < c->count; i++) {
        float mit = c->armor[i] / (c->armor[i] + 100.0f);
        dmg_inout[i] = dmg_inout[i] * (1.0f - mit);
    }
}

/*
 * combat_hit_roll — Set hit_out[attacker] = 1 if the attack lands.
 *   Uses hit_chance[attacker] as probability (0..1).
 */
void combat_hit_roll(const CombatSoA *c, int attacker, int *hit_out)
{
    if (attacker < 0 || attacker >= c->count) { *hit_out = 0; return; }
    uint32_t seed = (uint32_t)(attacker + 1) * 2246822519u;
    *hit_out = (lcg_float(&seed) < c->hit_chance[attacker]) ? 1 : 0;
}

/*
 * combat_crit_roll — Set *dmg_mult_out to crit_mult if critical hit, else 1.
 */
void combat_crit_roll(const CombatSoA *c, int attacker, float *dmg_mult_out)
{
    if (attacker < 0 || attacker >= c->count) { *dmg_mult_out = 1.0f; return; }
    uint32_t seed = (uint32_t)(attacker + 1) * 3266489917u;
    *dmg_mult_out = (lcg_float(&seed) < c->crit_chance[attacker])
                    ? c->crit_mult[attacker] : 1.0f;
}

/*
 * combat_morale_decay — Morale degrades over time at each unit's decay rate.
 */
void combat_morale_decay(CombatSoA *c, float dt)
{
    for (int i = 0; i < c->count; i++)
        c->morale[i] = clampf(c->morale[i] - c->morale_decay[i] * dt, 0.0f, 1.0f);
}

/*
 * combat_morale_boost — Instantly raise one unit's morale by amount.
 */
void combat_morale_boost(CombatSoA *c, int unit, float amount)
{
    if (unit < 0 || unit >= c->count) return;
    c->morale[unit] = clampf(c->morale[unit] + amount, 0.0f, 1.0f);
}

/*
 * combat_rout_check — Set rout_flags[i] = 1 when morale < rout_threshold.
 */
void combat_rout_check(const CombatSoA *c, int *rout_flags)
{
    for (int i = 0; i < c->count; i++)
        rout_flags[i] = (c->morale[i] < c->rout_threshold[i]) ? 1 : 0;
}

/*
 * combat_hp_regen — Heal all units by regen_rate * max_hp per second.
 */
void combat_hp_regen(CombatSoA *c, float regen_rate, float dt)
{
    for (int i = 0; i < c->count; i++) {
        float heal = regen_rate * c->max_hp[i] * dt;
        c->hp[i] = clampf(c->hp[i] + heal, 0.0f, c->max_hp[i]);
    }
}

/*
 * combat_aoe_damage — Deal dmg to every unit within radius of (cx, cy).
 *   Damage falls off linearly with distance.
 */
void combat_aoe_damage(CombatSoA *c, const float *pos_x, const float *pos_y,
                       float cx, float cy, float radius, float dmg)
{
    float r2 = radius * radius;
    for (int i = 0; i < c->count; i++) {
        float dx = pos_x[i] - cx;
        float dy = pos_y[i] - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= r2) continue;
        float falloff = 1.0f - sqrtf(d2) / radius;
        float actual  = dmg * falloff;
        if (actual < 1.0f) actual = 1.0f;
        c->hp[i] = clampf(c->hp[i] - actual, 0.0f, c->max_hp[i]);
    }
}

/*
 * combat_siege_damage — Structural damage to a building over time.
 *   hp[building] -= siege_power * dt
 */
void combat_siege_damage(CombatSoA *c, int building, float siege_power, float dt)
{
    if (building < 0 || building >= c->count) return;
    c->hp[building] = clampf(c->hp[building] - siege_power * dt, 0.0f, c->max_hp[building]);
}

/* ======================================================================
   4. ECONOMY & RESOURCES
   ====================================================================== */

/*
 * econ_gather — Accumulate resources at gather_rate per tick.
 */
void econ_gather(EconSoA *e, float dt)
{
    for (int i = 0; i < e->count; i++) {
        e->resource[i] = clampf(e->resource[i] + e->gather_rate[i] * dt,
                                 0.0f, e->max_resource[i]);
    }
}

/*
 * econ_deplete — Natural resource depletion each tick.
 */
void econ_deplete(EconSoA *e, float dt)
{
    for (int i = 0; i < e->count; i++) {
        e->resource[i] = clampf(e->resource[i] - e->depletion_rate[i] * dt,
                                 0.0f, e->max_resource[i]);
        /* supply tracks current stockpile */
        e->supply[i] = e->resource[i];
    }
}

/*
 * econ_market_price — Price adjusts by square-root of demand/supply ratio.
 *   price_new = clamp(price * sqrt(demand / max(supply, 1)), 0.01, MAX_PRICE)
 */
void econ_market_price(EconSoA *e)
{
    for (int i = 0; i < e->count; i++) {
        float sup = e->supply[i] > 1.0f ? e->supply[i] : 1.0f;
        float base = e->price[i] > 0.0f ? e->price[i] : 1.0f;
        e->price[i] = clampf(base * sqrtf(e->demand[i] / sup), 0.01f, MAX_PRICE);
    }
}

/*
 * econ_collect_tax — tax_collected[i] += resource[i] * tax_rate[i] * population[i].
 */
void econ_collect_tax(EconSoA *e, const float *population)
{
    for (int i = 0; i < e->count; i++) {
        float tax = e->resource[i] * e->tax_rate[i] * population[i] * 0.001f;
        e->tax_collected[i] += tax;
        e->resource[i]       = clampf(e->resource[i] - tax, 0.0f, e->max_resource[i]);
    }
}

/*
 * econ_trade — Transfer amount of resource from seller[si] to buyer[bi].
 *   The buyer pays at the seller's price; trade_volume is updated.
 */
void econ_trade(EconSoA *seller, int si, EconSoA *buyer, int bi, float amount)
{
    if (si < 0 || si >= seller->count) return;
    if (bi < 0 || bi >= buyer->count)  return;
    float actual = amount < seller->resource[si] ? amount : seller->resource[si];
    seller->resource[si] = clampf(seller->resource[si] - actual,
                                   0.0f, seller->max_resource[si]);
    buyer->resource[bi]  = clampf(buyer->resource[bi]  + actual,
                                   0.0f, buyer->max_resource[bi]);
    seller->trade_volume[si] += actual;
    buyer->trade_volume[bi]  += actual;
}

/*
 * econ_resource_cap — Hard-clamp all stockpiles to [0, max_resource].
 */
void econ_resource_cap(EconSoA *e)
{
    for (int i = 0; i < e->count; i++)
        e->resource[i] = clampf(e->resource[i], 0.0f, e->max_resource[i]);
}

/*
 * econ_demand_update — Demand rises or falls with population change.
 *   demand += 0.01 * population_delta
 */
void econ_demand_update(EconSoA *e, float population_delta)
{
    for (int i = 0; i < e->count; i++) {
        e->demand[i] = clampf(e->demand[i] + 0.01f * population_delta, 0.0f, 1e9f);
    }
}

/*
 * econ_supply_shock — Suddenly reduce all supplies by a shock_factor (0..1).
 */
void econ_supply_shock(EconSoA *e, float shock_factor)
{
    float keep = clampf(1.0f - shock_factor, 0.0f, 1.0f);
    for (int i = 0; i < e->count; i++) {
        e->resource[i] *= keep;
        e->supply[i]    = e->resource[i];
    }
}

/*
 * econ_inflation — Prices rise continuously: price *= (1 + inflation_rate * dt).
 */
void econ_inflation(EconSoA *e, float inflation_rate, float dt)
{
    float factor = 1.0f + inflation_rate * dt;
    for (int i = 0; i < e->count; i++)
        e->price[i] = clampf(e->price[i] * factor, 0.01f, 1e6f);
}

/*
 * econ_scarcity_penalty — output_mult[i] = resource[i] / max_resource[i].
 *   A value < 1 signals reduced production capacity.
 */
void econ_scarcity_penalty(const EconSoA *e, float *output_mult)
{
    for (int i = 0; i < e->count; i++) {
        float cap = e->max_resource[i] > 0.0f ? e->max_resource[i] : 1.0f;
        output_mult[i] = clampf(e->resource[i] / cap, 0.0f, 1.0f);
    }
}

/* ======================================================================
   5. ENVIRONMENT & WEATHER
   ====================================================================== */

/*
 * env_temperature_diffuse — Temperatures relax toward their local targets.
 *   dT/dt = rate * (temp_target - temperature)
 */
void env_temperature_diffuse(EnvSoA *e, float rate, float dt)
{
    for (int i = 0; i < e->count; i++) {
        float diff = e->temp_target[i] - e->temperature[i];
        e->temperature[i] += rate * diff * dt;
    }
}

/*
 * env_rainfall_update — Rainfall proportional to humidity and wind magnitude.
 *   rainfall = humidity * sqrt(wind_x^2 + wind_y^2) * 0.5
 */
void env_rainfall_update(EnvSoA *e, float dt)
{
    for (int i = 0; i < e->count; i++) {
        float wind_mag = sqrtf(e->wind_x[i] * e->wind_x[i] +
                                e->wind_y[i] * e->wind_y[i]);
        float target_rain = e->humidity[i] * wind_mag * 0.5f;
        float diff = target_rain - e->rainfall[i];
        e->rainfall[i] = clampf(e->rainfall[i] + diff * dt, 0.0f, 100.0f);
    }
}

/*
 * env_fire_spread — Fire intensity grows when neighbouring fuel is present.
 *   intensity += spread_prob * fuel * dt
 */
void env_fire_spread(EnvSoA *e, float spread_prob, float dt)
{
    for (int i = 0; i < e->count; i++) {
        if (e->fire_intensity[i] <= 0.0f) continue;
        float spread = spread_prob * e->fuel[i] * e->fire_intensity[i] * dt;
        e->fire_intensity[i] = clampf(e->fire_intensity[i] + spread, 0.0f, 1.0f);
    }
}

/*
 * env_fire_consume — Fire burns available fuel; intensity drops when fuel runs out.
 *   fuel -= consume_rate * fire_intensity * dt
 *   intensity decays by a fixed small rate while fuel remains
 */
void env_fire_consume(EnvSoA *e, float dt)
{
    const float consume_rate  = 0.1f;
    const float decay_rate    = 0.01f; /* fixed intensity loss per unit time while fuel remains */
    for (int i = 0; i < e->count; i++) {
        if (e->fire_intensity[i] <= 0.0f) continue;
        float burned = consume_rate * e->fire_intensity[i] * dt;
        e->fuel[i] = clampf(e->fuel[i] - burned, 0.0f, 1.0f);
        /* fire dies when fuel is exhausted */
        if (e->fuel[i] <= 0.0f)
            e->fire_intensity[i] = 0.0f;
        else
            e->fire_intensity[i] = clampf(e->fire_intensity[i] - decay_rate * dt, 0.0f, 1.0f);
    }
}

/*
 * env_humidity_evaporate — High temperature drives humidity down.
 *   d(humidity)/dt = -temperature * 0.001
 */
void env_humidity_evaporate(EnvSoA *e, float dt)
{
    for (int i = 0; i < e->count; i++) {
        float loss = e->temperature[i] * 0.001f * dt;
        e->humidity[i] = clampf(e->humidity[i] - loss, 0.0f, 1.0f);
    }
}

/*
 * env_wind_advect — Wind vectors evolve under simple inertia + small noise.
 *   wind = wind * 0.99 + (pressure_gradient contribution handled separately)
 */
void env_wind_advect(EnvSoA *e, float dt)
{
    const float dampen = 0.99f;
    for (int i = 0; i < e->count; i++) {
        e->wind_x[i] *= dampen;
        e->wind_y[i] *= dampen;
        /* Keep wind bounded */
        (void)dt;
    }
}

/*
 * env_pressure_gradient — High pressure pushes wind outward.
 *   wind += pressure_excess * 0.01  (simple isotropic approximation)
 */
void env_pressure_gradient(EnvSoA *e)
{
    const float base_pressure = 1013.25f;
    for (int i = 0; i < e->count; i++) {
        float excess = (e->pressure[i] - base_pressure) * 0.01f;
        e->wind_x[i] += excess;
        e->wind_y[i] += excess;
    }
}

/*
 * env_elevation_temp_bias — Higher tiles are colder.
 *   temp_target -= elevation * 0.5 (lapse rate approximation)
 */
void env_elevation_temp_bias(EnvSoA *e)
{
    const float lapse = 0.5f;
    for (int i = 0; i < e->count; i++)
        e->temp_target[i] -= e->elevation[i] * lapse;
}

/*
 * env_drought_check — Flag tiles where rainfall is below threshold.
 */
void env_drought_check(const EnvSoA *e, float threshold, int *drought_flags)
{
    for (int i = 0; i < e->count; i++)
        drought_flags[i] = (e->rainfall[i] < threshold) ? 1 : 0;
}

/*
 * env_flood_check — Flag tiles where rainfall exceeds threshold.
 */
void env_flood_check(const EnvSoA *e, float threshold, int *flood_flags)
{
    for (int i = 0; i < e->count; i++)
        flood_flags[i] = (e->rainfall[i] > threshold) ? 1 : 0;
}

/* ======================================================================
   6. MOVEMENT & AI
   ====================================================================== */

/*
 * move_velocity_verlet — Symplectic velocity Verlet integration.
 *   pos += vel * dt + 0.5 * acc * dt^2
 *   vel += acc * dt
 */
void move_velocity_verlet(MoveSoA *m, float dt)
{
    float dt2_half = 0.5f * dt * dt;
    for (int i = 0; i < m->count; i++) {
        m->pos_x[i] += m->vel_x[i] * dt + m->acc_x[i] * dt2_half;
        m->pos_y[i] += m->vel_y[i] * dt + m->acc_y[i] * dt2_half;
        m->vel_x[i] += m->acc_x[i] * dt;
        m->vel_y[i] += m->acc_y[i] * dt;
    }
}

/*
 * move_flock_separation — Steer away from neighbours closer than radius.
 *   Accumulates repulsion forces into each agent's acceleration.
 */
void move_flock_separation(MoveSoA *m, float radius, float strength)
{
    float r2 = radius * radius;
    for (int i = 0; i < m->count; i++) {
        float fx = 0.0f, fy = 0.0f;
        for (int j = 0; j < m->count; j++) {
            if (i == j) continue;
            float dx = m->pos_x[i] - m->pos_x[j];
            float dy = m->pos_y[i] - m->pos_y[j];
            float d2 = dx * dx + dy * dy;
            if (d2 > r2 || d2 < 1e-6f) continue;
            float inv_d = fast_inv_sqrt_scalar(d2);
            fx += dx * inv_d;
            fy += dy * inv_d;
        }
        m->acc_x[i] += strength * fx;
        m->acc_y[i] += strength * fy;
    }
}

/*
 * move_flock_alignment — Steer toward the average velocity of neighbours.
 */
void move_flock_alignment(MoveSoA *m, float radius, float strength)
{
    float r2 = radius * radius;
    for (int i = 0; i < m->count; i++) {
        float avg_vx = 0.0f, avg_vy = 0.0f;
        int   n      = 0;
        for (int j = 0; j < m->count; j++) {
            if (i == j) continue;
            float dx = m->pos_x[i] - m->pos_x[j];
            float dy = m->pos_y[i] - m->pos_y[j];
            if (dx * dx + dy * dy > r2) continue;
            avg_vx += m->vel_x[j];
            avg_vy += m->vel_y[j];
            n++;
        }
        if (n > 0) {
            m->acc_x[i] += strength * (avg_vx / n - m->vel_x[i]);
            m->acc_y[i] += strength * (avg_vy / n - m->vel_y[i]);
        }
    }
}

/*
 * move_flock_cohesion — Steer toward the centre of mass of neighbours.
 */
void move_flock_cohesion(MoveSoA *m, float radius, float strength)
{
    float r2 = radius * radius;
    for (int i = 0; i < m->count; i++) {
        float cx = 0.0f, cy = 0.0f;
        int   n  = 0;
        for (int j = 0; j < m->count; j++) {
            if (i == j) continue;
            float dx = m->pos_x[i] - m->pos_x[j];
            float dy = m->pos_y[i] - m->pos_y[j];
            if (dx * dx + dy * dy > r2) continue;
            cx += m->pos_x[j];
            cy += m->pos_y[j];
            n++;
        }
        if (n > 0) {
            m->acc_x[i] += strength * (cx / n - m->pos_x[i]);
            m->acc_y[i] += strength * (cy / n - m->pos_y[i]);
        }
    }
}

/*
 * move_seek_target — Apply a steering force toward (tx, ty).
 */
void move_seek_target(MoveSoA *m, int unit, float tx, float ty, float strength)
{
    if (unit < 0 || unit >= m->count) return;
    float dx = tx - m->pos_x[unit];
    float dy = ty - m->pos_y[unit];
    float d2 = dx * dx + dy * dy;
    if (d2 < 1e-6f) return;
    float inv_d = fast_inv_sqrt_scalar(d2);
    m->acc_x[unit] += strength * dx * inv_d;
    m->acc_y[unit] += strength * dy * inv_d;
}

/*
 * move_flee_target — Apply a steering force away from (tx, ty).
 */
void move_flee_target(MoveSoA *m, int unit, float tx, float ty, float strength)
{
    move_seek_target(m, unit, tx, ty, -strength);
}

/*
 * move_astar_heuristic — Euclidean heuristic h = dist(pos, goal).
 *   Stores result in h_cost[unit].
 */
void move_astar_heuristic(MoveSoA *m, int unit, float gx, float gy)
{
    if (unit < 0 || unit >= m->count) return;
    float dx = gx - m->pos_x[unit];
    float dy = gy - m->pos_y[unit];
    m->h_cost[unit] = sqrtf(dx * dx + dy * dy);
}

/*
 * move_clamp_speed — Enforce per-agent speed cap; rescale velocity accordingly.
 */
void move_clamp_speed(MoveSoA *m)
{
    for (int i = 0; i < m->count; i++) {
        float spd2 = m->vel_x[i] * m->vel_x[i] + m->vel_y[i] * m->vel_y[i];
        float max2 = m->max_speed[i] * m->max_speed[i];
        if (spd2 > max2 && spd2 > 1e-9f) {
            float scale = m->max_speed[i] * fast_inv_sqrt_scalar(spd2);
            m->vel_x[i] *= scale;
            m->vel_y[i] *= scale;
        }
        m->speed[i] = sqrtf(m->vel_x[i] * m->vel_x[i] + m->vel_y[i] * m->vel_y[i]);
    }
}

/*
 * move_heading_update — Compute heading from current velocity using atan2.
 */
void move_heading_update(MoveSoA *m)
{
    for (int i = 0; i < m->count; i++) {
        if (m->speed[i] > 1e-6f)
            m->heading[i] = atan2f(m->vel_y[i], m->vel_x[i]);
    }
}

/*
 * move_arrival_brake — Reduce speed linearly when within slow_radius of target.
 */
void move_arrival_brake(MoveSoA *m, int unit, float tx, float ty, float slow_radius)
{
    if (unit < 0 || unit >= m->count) return;
    float dx   = tx - m->pos_x[unit];
    float dy   = ty - m->pos_y[unit];
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < slow_radius && dist > 1e-6f) {
        float factor = dist / slow_radius;
        m->vel_x[unit] *= factor;
        m->vel_y[unit] *= factor;
        m->speed[unit] *= factor;
    }
}

/* ======================================================================
   7. DIVINE POWERS
   ====================================================================== */

/*
 * divine_energy_regen — Restore energy at regen_rate scaled by faith's divine favor.
 */
void divine_energy_regen(DivineSoA *d, const FaithSoA *f, float dt)
{
    for (int i = 0; i < d->count; i++) {
        float favor = (i < f->count) ? f->divine_favor[i] : 1.0f;
        float gain  = d->regen_rate[i] * favor * dt;
        d->energy[i] = clampf(d->energy[i] + gain, 0.0f, d->energy_cap[i]);
    }
}

/*
 * divine_meteor_cost — Check whether god has enough energy to call a meteor.
 *   Sets *can_cast = 1 if yes and deducts the cost.
 */
void divine_meteor_cost(DivineSoA *d, int god, int *can_cast)
{
    if (god < 0 || god >= d->count) { *can_cast = 0; return; }
    if (d->energy[god] >= d->meteor_cost[god]) {
        *can_cast = 1;
        d->energy[god] = clampf(d->energy[god] - d->meteor_cost[god], 0.0f, d->energy_cap[god]);
    } else {
        *can_cast = 0;
    }
}

/*
 * divine_heal_apply — Heal target_unit's HP by heal_amount[god], then decay it.
 */
void divine_heal_apply(DivineSoA *d, CombatSoA *c, int god, int target_unit)
{
    if (god < 0 || god >= d->count) return;
    if (target_unit < 0 || target_unit >= c->count) return;
    c->hp[target_unit] = clampf(c->hp[target_unit] + d->heal_amount[god],
                                 0.0f, c->max_hp[target_unit]);
    /* Each cast weakens the heal slightly */
    d->heal_amount[god] = clampf(d->heal_amount[god] * (1.0f - d->heal_decay[god]),
                                  1.0f, 1e6f);
}

/*
 * divine_heal_decay — Heal effectiveness slowly restores over time.
 *   heal_amount grows back toward max_heal at rate heal_decay.
 */
void divine_heal_decay(DivineSoA *d, float dt)
{
    for (int i = 0; i < d->count; i++) {
        float target = d->energy_cap[i] * 0.1f; /* heal scales with energy cap */
        float diff   = target - d->heal_amount[i];
        d->heal_amount[i] = clampf(d->heal_amount[i] + diff * d->heal_decay[i] * dt,
                                    1.0f, 1e6f);
    }
}

/*
 * divine_terraform_cost — Check energy for terraforming `tiles` tiles.
 *   Sets *can_cast = 1 and deducts cost if feasible.
 */
void divine_terraform_cost(DivineSoA *d, int god, int tiles, int *can_cast)
{
    if (god < 0 || god >= d->count) { *can_cast = 0; return; }
    float total = d->terraform_cost[god] * (float)tiles;
    if (d->energy[god] >= total) {
        *can_cast = 1;
        d->energy[god] = clampf(d->energy[god] - total, 0.0f, d->energy_cap[god]);
    } else {
        *can_cast = 0;
    }
}

/*
 * divine_smite — Deal smite_power[god] damage to target, reduced by armour.
 */
void divine_smite(DivineSoA *d, CombatSoA *c, int god, int target)
{
    if (god < 0 || god >= d->count) return;
    if (target < 0 || target >= c->count) return;
    float dmg = d->smite_power[god] - c->armor[target] * 0.25f;
    if (dmg < 1.0f) dmg = 1.0f;
    c->hp[target] = clampf(c->hp[target] - dmg, 0.0f, c->max_hp[target]);
    d->energy[god] = clampf(d->energy[god] - d->smite_power[god] * 0.1f,
                             0.0f, d->energy_cap[god]);
}

/*
 * divine_blessing — Multiply target's base_atk and max_hp by blessing_mult[god].
 */
void divine_blessing(DivineSoA *d, CombatSoA *c, int god, int target)
{
    if (god < 0 || god >= d->count) return;
    if (target < 0 || target >= c->count) return;
    float mult = d->blessing_mult[god];
    c->base_atk[target] *= mult;
    c->max_hp[target]   *= mult;
    c->hp[target]        = clampf(c->hp[target] * mult, 0.0f, c->max_hp[target]);
    d->energy[god]       = clampf(d->energy[god] - 10.0f, 0.0f, d->energy_cap[god]);
}

/*
 * divine_cooldown_tick — Decrement all cooldown timers by dt.
 */
void divine_cooldown_tick(DivineSoA *d, float dt)
{
    for (int i = 0; i < d->count; i++)
        d->cooldown[i] = clampf(d->cooldown[i] - dt, 0.0f, 1e6f);
}

/*
 * divine_energy_cap — Clamp all energy values to [0, energy_cap].
 */
void divine_energy_cap(DivineSoA *d)
{
    for (int i = 0; i < d->count; i++)
        d->energy[i] = clampf(d->energy[i], 0.0f, d->energy_cap[i]);
}

/*
 * divine_favor_scale — Scale each god's regen_rate by faith's divine_favor.
 */
void divine_favor_scale(DivineSoA *d, const FaithSoA *f)
{
    for (int i = 0; i < d->count; i++) {
        if (i >= f->count) break;
        d->regen_rate[i] *= (0.5f + 0.5f * f->divine_favor[i]);
    }
}

/* ======================================================================
   8. NPC PSYCHOLOGY
   ====================================================================== */

/*
 * psych_utility_evaluate — Choose the action with the highest utility score.
 *   Stores 0=work, 1=fight, 2=flee in a virtual "chosen" encoded into
 *   aggression (fight utility dominates when aggression > others).
 */
void psych_utility_evaluate(PsychSoA *p)
{
    for (int i = 0; i < p->count; i++) {
        float uw = p->utility_work[i];
        float uf = p->utility_fight[i];
        float ul = p->utility_flee[i];
        /* Simple argmax drives aggression towards chosen behaviour */
        if (ul > uf && ul > uw)
            p->aggression[i] = clampf(p->aggression[i] - 0.1f, 0.0f, 1.0f);
        else if (uf > uw)
            p->aggression[i] = clampf(p->aggression[i] + 0.05f, 0.0f, 1.0f);
    }
}

/*
 * psych_threat_assess — Assess a specific threat unit and update threat_level.
 *   threat = enemy_hp_fraction * proximity_factor
 */
void psych_threat_assess(PsychSoA *p, const CombatSoA *c, int npc, int threat_unit)
{
    if (npc < 0 || npc >= p->count) return;
    if (threat_unit < 0 || threat_unit >= c->count) return;
    float hp_frac   = c->hp[threat_unit] / (c->max_hp[threat_unit] + 1.0f);
    float atk_norm  = c->base_atk[threat_unit] / 20.0f; /* normalised to expected max */
    float threat    = clampf(hp_frac * atk_norm, 0.0f, 1.0f);
    p->threat_level[npc] = clampf(p->threat_level[npc] + threat * 0.3f, 0.0f, 1.0f);
    p->fear[npc]         = clampf(p->fear[npc]   + threat * 0.1f,         0.0f, 1.0f);
}

/*
 * psych_loyalty_shift — Adjust loyalty by event_loyalty_delta, scaled by social bond.
 */
void psych_loyalty_shift(PsychSoA *p, int npc, float event_loyalty_delta)
{
    if (npc < 0 || npc >= p->count) return;
    float scaled = event_loyalty_delta * (0.5f + 0.5f * p->social_bond[npc]);
    p->loyalty[npc] = clampf(p->loyalty[npc] + scaled, 0.0f, 1.0f);
}

/*
 * psych_fear_decay — Fear fades exponentially at memory_decay rate.
 *   fear *= exp(-memory_decay * dt)  ≈ fear * (1 - memory_decay * dt)
 */
void psych_fear_decay(PsychSoA *p, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float k = p->memory_decay[i] * dt;
        p->fear[i] = clampf(p->fear[i] * (1.0f - k), 0.0f, 1.0f);
    }
}

/*
 * psych_happiness_update — Happiness correlates with relative resource abundance.
 *   happiness = 0.5 * (1 + resource_ratio - fear)
 */
void psych_happiness_update(PsychSoA *p, const EconSoA *e)
{
    for (int i = 0; i < p->count && i < e->count; i++) {
        float cap   = e->max_resource[i] > 0.0f ? e->max_resource[i] : 1.0f;
        float ratio = clampf(e->resource[i] / cap, 0.0f, 1.0f);
        float happy = 0.5f * (1.0f + ratio - p->fear[i]);
        p->happiness[i] = clampf(p->happiness[i] * 0.9f + happy * 0.1f, 0.0f, 1.0f);
    }
}

/*
 * psych_aggression_trigger — A provocation event raises aggression inversely
 *   with happiness.
 */
void psych_aggression_trigger(PsychSoA *p, int npc, float provocation)
{
    if (npc < 0 || npc >= p->count) return;
    float rise = provocation * (1.0f - p->happiness[npc]);
    p->aggression[npc] = clampf(p->aggression[npc] + rise, 0.0f, 1.0f);
}

/*
 * psych_social_bond_update — Bonds strengthen when loyalty is high, weaken otherwise.
 */
void psych_social_bond_update(PsychSoA *p, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float delta = (p->loyalty[i] - 0.5f) * 0.01f * dt;
        p->social_bond[i] = clampf(p->social_bond[i] + delta, 0.0f, 1.0f);
    }
}

/*
 * psych_memory_fade — All emotional states decay slowly toward neutral (0.5 for
 *   happiness/loyalty, 0 for fear/aggression/threat).
 */
void psych_memory_fade(PsychSoA *p, float dt)
{
    for (int i = 0; i < p->count; i++) {
        float k = p->memory_decay[i] * dt;
        p->fear[i]        = clampf(p->fear[i]        * (1.0f - k), 0.0f, 1.0f);
        p->aggression[i]  = clampf(p->aggression[i]  * (1.0f - k), 0.0f, 1.0f);
        p->threat_level[i]= clampf(p->threat_level[i]* (1.0f - k), 0.0f, 1.0f);
    }
}

/*
 * psych_morale_from_psych — Set CombatSoA morale from psychological state.
 *   morale = happiness * (1 - fear) * loyalty
 */
void psych_morale_from_psych(const PsychSoA *p, CombatSoA *c)
{
    int n = p->count < c->count ? p->count : c->count;
    for (int i = 0; i < n; i++) {
        c->morale[i] = clampf(
            p->happiness[i] * (1.0f - p->fear[i]) * p->loyalty[i],
            0.0f, 1.0f);
    }
}

/*
 * psych_defection_check — Flag NPCs whose loyalty has fallen below 0.2.
 */
void psych_defection_check(const PsychSoA *p, int *defect_flags)
{
    for (int i = 0; i < p->count; i++)
        defect_flags[i] = (p->loyalty[i] < 0.2f) ? 1 : 0;
}

/* ======================================================================
   9. PROGRESSION & TECH
   ====================================================================== */

/*
 * tech_research_tick — Accumulate research points.
 *   pts += rate * pop_bonus * golden_age_mult * dt
 */
void tech_research_tick(TechSoA *t, const PopSoA *p, float dt)
{
    for (int i = 0; i < t->count; i++) {
        float bonus  = (i < p->count) ? t->pop_bonus[i] : 1.0f;
        float mult   = t->golden_age_timer[i] > 0.0f ? t->golden_age_mult[i] : 1.0f;
        float gained = t->research_rate[i] * bonus * mult * dt;
        t->research_pts[i] += gained;
    }
}

/*
 * tech_cost_scale — tech_cost grows exponentially with tech_level.
 *   cost = 100 * exp(clamp(tech_level * 0.3, 0, 20))
 *   Exponent is clamped to 20 (exp(20) ≈ 485M) to prevent overflow to
 *   INFINITY at high tech levels, which would permanently stall progression.
 */
void tech_cost_scale(TechSoA *t)
{
    for (int i = 0; i < t->count; i++) {
        float exponent = clampf(t->tech_level[i] * 0.3f, 0.0f, 20.0f);
        t->tech_cost[i] = 100.0f * expf(exponent);
    }
}

/*
 * tech_unlock_check — Advance tech_level when research_pts >= tech_cost.
 *   Sets unlock_flags[i] = 1 on advancement.
 */
void tech_unlock_check(TechSoA *t, int *unlock_flags)
{
    for (int i = 0; i < t->count; i++) {
        unlock_flags[i] = 0;
        if (t->research_pts[i] >= t->tech_cost[i]) {
            t->research_pts[i] -= t->tech_cost[i];
            t->tech_level[i]   += 1.0f;
            unlock_flags[i]     = 1;
        }
    }
}

/*
 * tech_golden_age_tick — Count down the golden age timer.
 */
void tech_golden_age_tick(TechSoA *t, float dt)
{
    for (int i = 0; i < t->count; i++) {
        if (t->golden_age_timer[i] > 0.0f)
            t->golden_age_timer[i] = clampf(t->golden_age_timer[i] - dt, 0.0f, 1e6f);
    }
}

/*
 * tech_golden_age_trigger — Start a golden age if culture exceeds threshold.
 *   Duration = 500 ticks; multiplier = 2.0.
 */
void tech_golden_age_trigger(TechSoA *t, int nation, float threshold)
{
    if (nation < 0 || nation >= t->count) return;
    if (t->culture[nation] >= threshold && t->golden_age_timer[nation] <= 0.0f) {
        t->golden_age_timer[nation] = 500.0f;
        t->golden_age_mult[nation]  = 2.0f;
    }
}

/*
 * tech_culture_spread — Culture grows logistically and spreads outward.
 *   d(culture)/dt = culture_spread * culture * (1 - culture / 1000)
 */
void tech_culture_spread(TechSoA *t, float dt)
{
    const float cap = 1000.0f;
    for (int i = 0; i < t->count; i++) {
        float c = t->culture[i];
        float dc = t->culture_spread[i] * c * (1.0f - c / cap);
        t->culture[i] = clampf(c + dc * dt, 0.0f, cap);
    }
}

/*
 * tech_era_advance — Advance era when tech_level crosses era * 10 boundary.
 */
void tech_era_advance(TechSoA *t)
{
    for (int i = 0; i < t->count; i++) {
        float expected_era = floorf(t->tech_level[i] / 10.0f);
        if (expected_era > t->era[i])
            t->era[i] = expected_era;
    }
}

/*
 * tech_pop_research_bonus — pop_bonus = log(1 + population / 1000).
 */
void tech_pop_research_bonus(TechSoA *t, const PopSoA *p)
{
    int n = t->count < p->count ? t->count : p->count;
    for (int i = 0; i < n; i++)
        t->pop_bonus[i] = logf(1.0f + p->population[i] / 1000.0f);
}

/*
 * tech_decay — Without ongoing research, tech slowly degrades.
 *   tech_level -= 0.0001 * dt  (very slow, models knowledge loss)
 */
void tech_decay(TechSoA *t, float dt)
{
    for (int i = 0; i < t->count; i++) {
        if (t->research_pts[i] <= 0.0f)
            t->tech_level[i] = clampf(t->tech_level[i] - 0.0001f * dt, 0.0f, 1e6f);
    }
}

/*
 * tech_diffusion — Neighbouring civilisations share a fraction of their tech.
 *   dst->research_pts[di] += rate * src->tech_level[si] * dt
 */
void tech_diffusion(const TechSoA *src, TechSoA *dst, int si, int di, float rate, float dt)
{
    if (si < 0 || si >= src->count) return;
    if (di < 0 || di >= dst->count) return;
    dst->research_pts[di] += rate * src->tech_level[si] * dt;
}

/* ======================================================================
   10. ENGINE & END GAME
   ====================================================================== */

/*
 * engine_fast_inv_sqrt — Batch fast inverse square root over inv_sqrt_val[].
 */
void engine_fast_inv_sqrt(EngineSoA *e)
{
    for (int i = 0; i < e->count; i++)
        e->inv_sqrt_out[i] = fast_inv_sqrt_scalar(e->inv_sqrt_val[i]);
}

/*
 * engine_entropy_increase — Entropy rises over time, scaled by chaos_mult.
 *   entropy += entropy_rate * chaos_mult * dt
 */
void engine_entropy_increase(EngineSoA *e, float dt)
{
    for (int i = 0; i < e->count; i++) {
        e->entropy[i] = clampf(
            e->entropy[i] + e->entropy_rate[i] * e->chaos_mult[i] * dt,
            0.0f, 1.0f);
    }
}

/*
 * engine_stability_update — Stability is the complement of entropy, boosted by tech,
 *   and reduced by population pressure (overpopulation destabilises a civilisation).
 *   pop_pressure  = population / (carrying_cap + 1)  clamped to [0, 1]
 *   stability = (1 - entropy) * (0.5 + 0.5 * tech_level_norm) * (1 - 0.5 * pop_pressure)
 */
void engine_stability_update(EngineSoA *e, const TechSoA *t)
{
    for (int i = 0; i < e->count; i++) {
        float tech_norm = (i < t->count)
                          ? clampf(t->tech_level[i] / 50.0f, 0.0f, 1.0f)
                          : 0.5f;
        float pop_pressure = (i < p->count)
                             ? clampf(p->population[i] / (p->carrying_cap[i] + 1.0f), 0.0f, 1.0f)
                             : 0.0f;
        e->stability[i] = clampf(
            (1.0f - e->entropy[i]) * (0.5f + 0.5f * tech_norm) * (1.0f - 0.5f * pop_pressure),
            0.0f, 1.0f);
    }
}

/*
 * engine_spatial_grid_assign — Bin each moving agent into a grid cell.
 *   grid_x[i] = floor(pos_x[i] / cell_size),  similarly for y.
 */
void engine_spatial_grid_assign(EngineSoA *e, const MoveSoA *m, float cell_size)
{
    int n = e->count < m->count ? e->count : m->count;
    float inv = (cell_size > 0.0f) ? (1.0f / cell_size) : 1.0f;
    for (int i = 0; i < n; i++) {
        e->grid_x[i] = floorf(m->pos_x[i] * inv);
        e->grid_y[i] = floorf(m->pos_y[i] * inv);
    }
}

/*
 * engine_end_timer_tick — Count down end-game timers when stability is critical.
 */
void engine_end_timer_tick(EngineSoA *e, float dt)
{
    for (int i = 0; i < e->count; i++) {
        if (e->stability[i] < 0.1f)
            e->end_timer[i] = clampf(e->end_timer[i] - dt, 0.0f, 1e6f);
    }
}

/*
 * engine_victory_pts_update — Victory points accumulate from population and tech.
 *   pts += (population * 0.001 + tech_level) * dt
 */
void engine_victory_pts_update(EngineSoA *e, const PopSoA *p, const TechSoA *t)
{
    for (int i = 0; i < e->count; i++) {
        float pop_contrib  = (i < p->count) ? p->population[i] * 0.001f : 0.0f;
        float tech_contrib = (i < t->count) ? t->tech_level[i]          : 0.0f;
        e->victory_pts[i] += (pop_contrib + tech_contrib);
    }
}

/*
 * engine_chaos_event — High entropy randomly amplifies chaos_mult.
 *   Uses the stored per-faction RNG state.
 */
void engine_chaos_event(EngineSoA *e, int faction)
{
    if (faction < 0 || faction >= e->count) return;
    float roll = lcg_float(&e->rng_state[faction]);
    if (roll < e->entropy[faction]) {
        /* Trigger a chaos spike */
        e->chaos_mult[faction] = clampf(
            e->chaos_mult[faction] * (1.0f + roll),
            1.0f, 10.0f);
    } else {
        /* Gradually dampen */
        e->chaos_mult[faction] = clampf(
            e->chaos_mult[faction] * 0.99f,
            1.0f, 10.0f);
    }
}

/*
 * engine_entropy_reset — Reset entropy (and chaos_mult) for one faction.
 */
void engine_entropy_reset(EngineSoA *e, int faction)
{
    if (faction < 0 || faction >= e->count) return;
    e->entropy[faction]    = 0.0f;
    e->chaos_mult[faction] = 1.0f;
}

/*
 * engine_determinism_seed — Seed the per-faction LCG for reproducible chaos.
 */
void engine_determinism_seed(EngineSoA *e, int faction, uint32_t seed)
{
    if (faction < 0 || faction >= e->count) return;
    e->rng_state[faction] = seed != 0u ? seed : 1u;
}

/*
 * engine_end_condition_check — Set end_flags[i] = 1 when end_timer[i] reaches 0.
 */
void engine_end_condition_check(const EngineSoA *e, int *end_flags)
{
    for (int i = 0; i < e->count; i++)
        end_flags[i] = (e->end_timer[i] <= 0.0f) ? 1 : 0;
}
