/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 Jonathan D.A. Jewell (metadatastician) <j.d.a.jewell@open.ac.uk> */
/*
 * null_host.c — a game with no game in it.
 *
 * This is the acceptance test for Phase 1. It includes the GENERATED header as a
 * C host would, links the kernel as a C library, and ticks it 10,000 times with
 * fabricated snapshots. If this program builds and passes, then:
 *
 *   1. `ee.h` is self-sufficient: it compiles as C11 with no other include, and
 *      its _Static_asserts agree with what this compiler lays out.
 *   2. The Zig kernel's own comptime asserts agree with the same numbers (or the
 *      library would not have compiled).
 *   3. The exported symbols, the status codes and the struct sizes all survive a
 *      real link — no Zig-specific machinery leaks into the host's view.
 *   4. The kernel produces an answer for every one of 10,000 ticks, and the same
 *      answer twice: the replay property ADR-0006 demands.
 *
 * Nothing in here is engine-specific, which is the point: the "universally
 * injectible" claim means a host that can fill in a struct can run this.
 *
 * Build (see `just kernel-host-test`):
 *   gcc -std=c11 -Wall -Wextra -Werror -Iinclude tests/host/null_host.c \
 *       src/interface/ffi/zig-out/lib/libevil_weevil.a -o null_host
 */

#include <evil_weevil/ee.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* A snapshot for tick `t`: the same fixture the Zig harness uses, so the two
 * hosts can be compared directly. */
static ee_snapshot make_snapshot(uint64_t t)
{
    ee_snapshot s;
    memset(&s, 0, sizeof s);
    s.tick = t;
    s.agent_id = 7;
    s.capability_mask = 0xFFFFFFFFu;
    /* Wounded every third tick, healthy otherwise: both sides of the health
     * threshold get exercised, so the fixture reaches every branch of the rule. */
    s.health = (t % 3u == 0u) ? (ee_fx)(EE_FX_ONE / 8) : (ee_fx)(4 * EE_FX_ONE);
    s.ammo = (t % 17u == 0u) ? 0 : (ee_fx)(3 * EE_FX_ONE);
    s.contact_count = (uint16_t)(t % 5u);
    for (uint16_t i = 0; i < s.contact_count; ++i) {
        ee_contact c;
        memset(&c, 0, sizeof c);
        c.id = (uint16_t)(100u + i);
        c.bearing_cos = (uint16_t)(16384u + i * 4096u);
        c.bearing_sin = 8192u;
        /* 3.0, 2.0, 1.0, 0.5: the nearest contact lands in a different band for
         * each contact count, which is what makes the fixture reach attack and flee. */
        c.distance = (ee_fx)(196608 - (int)i * 65536);
        c.threat = EE_FX_ONE / 2;
        switch (i) {
        case 0: s.contact0 = c; break;
        case 1: s.contact1 = c; break;
        case 2: s.contact2 = c; break;
        case 3: s.contact3 = c; break;
        default: break;
        }
    }
    return s;
}

/* Fold a run into one number, so two runs compare with one integer. */
/* Drive 10,000 ticks of the v0 fixture and fold every intent into one number.
 *
 * memory_enabled = 0 clears the agent's MEMORY_VALID bit after every tick, which
 * is all "v0 behaviour" means now: the kernel's memory branch is gated on that bit
 * and nothing else, so a host that never lets it be set gets the amnesiac rule
 * exactly. ADR-0009 claims that; the assertion in main() checks it against the
 * digest v0 shipped, rather than leaving it as prose. */
static uint64_t run_trace(uint64_t seed, ee_context *ctx_out, int memory_enabled)
{
    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.abi_minor = EE_ABI_MINOR;
    desc.capabilities = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT;
    desc.max_agents = 1;
    desc.host_tick_num = 60;
    desc.host_tick_den = 1;
    desc.rng_seed = seed;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;

    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    ee_status st = ee_init(&desc, &ctx);
    check(st == EE_STATUS_OK, "ee_init returned ee_ok");

    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = seed;

    uint64_t digest = 0;
    uint64_t answered = 0;
    for (uint64_t t = 0; t < 10000u; ++t) {
        ee_snapshot snap = make_snapshot(t);
        ee_intent intent;
        memset(&intent, 0, sizeof intent);
        st = ee_tick(&ctx, &snap, &agent, &intent);
        if (st != EE_STATUS_OK) {
            fprintf(stderr, "FAIL: ee_tick returned %s at tick %llu\n",
                    ee_status_name(st), (unsigned long long)t);
            failures++;
            break;
        }
        if (intent.action > EE_ACTION_INVESTIGATE) {
            fprintf(stderr, "FAIL: action %u out of range at tick %llu\n",
                    intent.action, (unsigned long long)t);
            failures++;
            break;
        }
        if (!memory_enabled)
            agent.flags &= ~((uint32_t)EE_AGENT_FLAG_MEMORY_VALID);
        digest = digest * 1099511628211ULL + intent.action;
        digest = digest * 1099511628211ULL + intent.target_id;
        digest = digest * 1099511628211ULL + (uint64_t)(int64_t)intent.move_x;
        digest = digest * 1099511628211ULL + intent.flags;
        answered++;
    }
    check(answered == 10000u, "the kernel answered all 10,000 ticks");
    if (ctx_out) *ctx_out = ctx;
    return digest;
}

/* ── The scenario the v0 fixture cannot reach: losing the contact ────────────
 *
 * The v0 fixture has zero-contact ticks (tick %% 5 == 0) and the memory branch
 * fires on them, but a fresh sighting arrives within five ticks, so the agent
 * never gets near the TTL. This fixture is the other half: a contact is visible
 * for the first 30 ticks of every 100 and gone for the 70 after that, so the
 * agent investigates for exactly memory_ttl ticks and then forgets — a hundred
 * times over the run. Duplicated in src/interface/ffi/test/null_host.zig on
 * purpose: two hosts, two languages, one trace, checked by digest. */
static ee_snapshot make_chase_snapshot(uint64_t t)
{
    const int visible = (t % 100u) < 30u;
    ee_snapshot s = make_snapshot(t);
    s.health = (ee_fx)(4 * EE_FX_ONE);
    s.ammo = (ee_fx)(3 * EE_FX_ONE);
    s.contact_count = visible ? 1u : 0u;
    if (visible) {
        s.contact0.id = 7;
        s.contact0.bearing_cos = 16384;
        s.contact0.bearing_sin = 8192;
        s.contact0.distance = 196608; /* 3.0 — beyond attack range */
        s.contact0.threat = (ee_fx)(EE_FX_ONE / 2);
    }
    return s;
}

static uint64_t run_chase_trace(uint64_t seed, ee_context *ctx_out,
                                unsigned counts[9], unsigned *from_memory)
{
    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.capabilities = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT;
    desc.max_agents = 1;
    desc.rng_seed = seed;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;

    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return 0;
    }

    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = seed;

    memset(counts, 0, 9u * sizeof counts[0]);
    *from_memory = 0;

    uint64_t digest = 0;
    for (uint64_t t = 0; t < 10000u; ++t) {
        ee_snapshot snap = make_chase_snapshot(t);
        ee_intent intent;
        memset(&intent, 0, sizeof intent);
        if (ee_tick(&ctx, &snap, &agent, &intent) != EE_STATUS_OK) {
            failures++;
            break;
        }
        if (intent.action < 9u)
            counts[intent.action]++;
        if (intent.flags & EE_INTENT_FLAG_FROM_MEMORY)
            (*from_memory)++;
        digest = digest * 1099511628211ULL + intent.action;
        digest = digest * 1099511628211ULL + intent.target_id;
        digest = digest * 1099511628211ULL + (uint64_t)(int64_t)intent.move_x;
        digest = digest * 1099511628211ULL + intent.flags;
    }
    if (ctx_out) *ctx_out = ctx;
    return digest;
}

/* "The wounded duel" (ADR-0012): one contact at 40000 — inside attack range (65536)
 * and inside flee range (131072) — with health oscillating across the wounded
 * threshold (16384) every tick. Two behaviours are legal and adjacent, so an
 * uncommitted rule changes its mind every tick; MEASURED, the graph alone switches 99
 * times in 100 ticks and the committed rule switches zero. */
static ee_snapshot make_flap_snapshot(uint64_t t)
{
    const int wounded = (t % 2u) == 1u;
    ee_snapshot s = make_snapshot(t);
    s.health = (ee_fx)(wounded ? 16383 : 16384);
    s.ammo = (ee_fx)(3 * EE_FX_ONE);
    s.contact_count = 1u;
    s.contact0.id = 11;
    s.contact0.bearing_cos = 16384;
    s.contact0.bearing_sin = 8192;
    s.contact0.distance = 40000;
    s.contact0.threat = (ee_fx)(EE_FX_ONE / 2);
    return s;
}

static uint64_t run_flap_trace(uint64_t seed, ee_context *ctx_out,
                               unsigned counts[9], unsigned *switches,
                               unsigned *mode_switches)
{
    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.capabilities = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT;
    desc.max_agents = 1;
    desc.rng_seed = seed;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;

    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return 0;
    }

    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = seed;

    memset(counts, 0, 9u * sizeof counts[0]);
    *switches = 0;
    *mode_switches = 0;

    uint64_t digest = 0;
    uint32_t prev_action = 0;
    int have_prev = 0;
    uint32_t prev_mode = 0;
    for (uint64_t t = 0; t < 100u; ++t) {
        ee_snapshot snap = make_flap_snapshot(t);
        ee_intent intent;
        memset(&intent, 0, sizeof intent);
        if (ee_tick(&ctx, &snap, &agent, &intent) != EE_STATUS_OK) {
            failures++;
            break;
        }
        if (intent.action < 9u)
            counts[intent.action]++;
        if (have_prev && prev_action != intent.action)
            (*switches)++;
        const uint32_t mode = (agent.flags & EE_AGENT_MODE_MASK) >> EE_AGENT_MODE_SHIFT;
        if (have_prev && prev_mode != mode)
            (*mode_switches)++;
        prev_action = intent.action;
        prev_mode = mode;
        have_prev = 1;

        digest = digest * 1099511628211ULL + intent.action;
        digest = digest * 1099511628211ULL + intent.target_id;
        digest = digest * 1099511628211ULL + (uint64_t)(int64_t)intent.move_x;
        digest = digest * 1099511628211ULL + intent.flags;
        digest = digest * 1099511628211ULL + mode;
    }
    if (ctx_out) *ctx_out = ctx;
    return digest;
}

/* ── ABI 1.1: the mind (ADR-0013) ─────────────────────────────────────────────
 *
 * The same four fixtures as src/interface/ffi/test/null_host.zig, through the
 * exported ee_tick_mind, with the same digest. Commitment override is DERIVED here
 * from the trace's ranking, incumbent and selected mode — the kernel does not
 * assert it, exactly as enaction-mind-trace/1 requires of a reader. */

enum mind_fixture { GRUDGE, FLINCH, FLINCH_CONTROL };

static ee_contact mind_contact(uint16_t id, uint16_t flags, uint16_t c, uint16_t sn,
                               ee_fx distance, ee_fx threat)
{
    ee_contact k;
    memset(&k, 0, sizeof k);
    k.id = id;
    k.flags = flags;
    k.bearing_cos = c;
    k.bearing_sin = sn;
    k.distance = distance;
    k.threat = threat;
    return k;
}

/* "The grudge": A (id 21) at 100000 hits the agent on ticks 10..12 and leaves at 13;
 * B (id 22) appears at 40000 — nearer, in attack range, a lower threat — at tick 20. */
static ee_snapshot make_grudge_snapshot(uint64_t t)
{
    ee_snapshot s = make_snapshot(t);
    s.health = t < 10u ? EE_FX_ONE : t == 10u ? 57344 : t == 11u ? 49152 : 40960;
    s.ammo = (ee_fx)(3 * EE_FX_ONE);
    memset(&s.contact0, 0, sizeof s.contact0);
    memset(&s.contact1, 0, sizeof s.contact1);
    uint16_t n = 0;
    if (t < 13u) {
        const uint16_t fl = (t >= 10u && t <= 12u) ? (uint16_t)EE_CONTACT_FLAG_DAMAGE_SOURCE : 0u;
        s.contact0 = mind_contact(21, fl, 16384, 8192, 100000, EE_FX_ONE / 2);
        n++;
    }
    if (t >= 20u) {
        ee_contact b = mind_contact(22, 0, 8192, 16384, 40000, EE_FX_ONE / 8);
        if (n == 0) s.contact0 = b; else s.contact1 = b;
        n++;
    }
    s.contact_count = n;
    return s;
}

/* "The flinch": C (id 31) visible on ticks 10..19 at `distance`; 40000 is a surprise
 * at close range, the control's 98304 is not. */
static ee_snapshot make_flinch_snapshot(uint64_t t, ee_fx distance)
{
    ee_snapshot s = make_snapshot(t);
    s.health = EE_FX_ONE;
    s.ammo = (ee_fx)(3 * EE_FX_ONE);
    memset(&s.contact0, 0, sizeof s.contact0);
    memset(&s.contact1, 0, sizeof s.contact1);
    memset(&s.contact2, 0, sizeof s.contact2);
    memset(&s.contact3, 0, sizeof s.contact3);
    s.contact_count = 0;
    if (t >= 10u && t < 20u) {
        s.contact0 = mind_contact(31, 0, 16384, 8192, distance, EE_FX_ONE / 2);
        s.contact_count = 1;
    }
    return s;
}

struct mind_run {
    uint64_t digest;
    unsigned overrides, unexplained, uncaused, startles, hesitates, fixates, over_mass;
    unsigned investigate;
    uint32_t max_grievance;
};

static struct mind_run run_mind(enum mind_fixture fx, uint32_t caps, uint64_t ticks)
{
    struct mind_run r;
    memset(&r, 0, sizeof r);

    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.abi_minor = EE_ABI_MINOR;
    desc.capabilities = caps;
    desc.max_agents = 1;
    desc.rng_seed = 0xEE0000000000001ULL;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;
    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return r;
    }
    check(ee_mind_fingerprint() == EE_MIND_FINGERPRINT, "the exported mind fingerprint matches the header");

    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = 0xEE0000000000001ULL;
    ee_mind mind;
    memset(&mind, 0, sizeof mind);
    mind.struct_size = (uint32_t)sizeof mind;

    for (uint64_t t = 0; t < ticks; ++t) {
        ee_snapshot snap = fx == GRUDGE ? make_grudge_snapshot(t)
                         : fx == FLINCH ? make_flinch_snapshot(t, 40000)
                                        : make_flinch_snapshot(t, 98304);
        ee_intent i;
        ee_mind_trace tr;
        memset(&i, 0, sizeof i);
        memset(&tr, 0, sizeof tr);
        tr.struct_size = (uint32_t)sizeof tr;
        if (ee_tick_mind(&ctx, &snap, &agent, &mind, &i, &tr) != EE_STATUS_OK) {
            failures++;
            break;
        }
        const uint32_t first = tr.ranking & 0xFu;
        if (tr.selected == tr.incumbent && tr.selected != first) r.overrides++;
        if (tr.selected != first && tr.selected != tr.incumbent) r.unexplained++;
        if ((tr.token != tr.token_prev) != (tr.cause != EE_CAUSE_NONE)) r.uncaused++;
        if (tr.attention > EE_MASS_FULL) r.over_mass++;
        if (tr.tells & EE_TELL_STARTLE) r.startles++;
        if (tr.tells & EE_TELL_HESITATE) r.hesitates++;
        if (tr.tells & EE_TELL_FIXATE) r.fixates++;
        if (((i.flags >> 4) & 0x7u) != tr.tells) failures++;
        if (i.action == EE_ACTION_INVESTIGATE) r.investigate++;
        if (mind.grievance > r.max_grievance) r.max_grievance = mind.grievance;

        r.digest = r.digest * 1099511628211ULL + i.action;
        r.digest = r.digest * 1099511628211ULL + i.target_id;
        r.digest = r.digest * 1099511628211ULL + (uint64_t)(int64_t)i.move_x;
        r.digest = r.digest * 1099511628211ULL + (uint64_t)(int64_t)i.look_x;
        r.digest = r.digest * 1099511628211ULL + i.flags;
        r.digest = r.digest * 1099511628211ULL + tr.ranking;
        r.digest = r.digest * 1099511628211ULL + tr.ranking_count;
        r.digest = r.digest * 1099511628211ULL + tr.selected;
        r.digest = r.digest * 1099511628211ULL + tr.incumbent;
        r.digest = r.digest * 1099511628211ULL + tr.token;
        r.digest = r.digest * 1099511628211ULL + tr.cause;
        r.digest = r.digest * 1099511628211ULL + tr.cause_contact;
        r.digest = r.digest * 1099511628211ULL + tr.attention;
        r.digest = r.digest * 1099511628211ULL + tr.tells;
    }
    return r;
}

static void print_mind(const char *name, const struct mind_run *r)
{
    printf("  mind %-15s digest %llu (overrides=%u startles=%u hesitates=%u fixates=%u investigate=%u max_grievance=%u)\n",
           name, (unsigned long long)r->digest, r->overrides, r->startles, r->hesitates,
           r->fixates, r->investigate, (unsigned)r->max_grievance);
}

/* "The frightened duel": the 1.0 wounded duel (pinned under ee_tick at
 * 5191914376283395890) through ee_tick_mind. Low health fills fear; fear raises evade
 * AND widens the wound threshold, so the agent flees and keeps fleeing. MEASURED with
 * the bonus alone: 75 switches in 100 ticks. With the widening: zero. */
struct duel_run { uint64_t digest; unsigned flee, switches, unexplained, uncaused; };

static struct duel_run run_frightened_duel(void)
{
    struct duel_run r;
    memset(&r, 0, sizeof r);
    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.abi_minor = EE_ABI_MINOR;
    desc.capabilities = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT | EE_CAP_DAMAGE_EVENTS;
    desc.max_agents = 1;
    desc.rng_seed = 0xEE0000000000001ULL;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;
    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return r;
    }
    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = 0xEE0000000000001ULL;
    ee_mind mind;
    memset(&mind, 0, sizeof mind);
    mind.struct_size = (uint32_t)sizeof mind;
    uint32_t prev = 0;
    for (uint64_t t = 0; t < 100u; ++t) {
        ee_snapshot snap = make_flap_snapshot(t);
        ee_intent i;
        ee_mind_trace tr;
        memset(&tr, 0, sizeof tr);
        tr.struct_size = (uint32_t)sizeof tr;
        if (ee_tick_mind(&ctx, &snap, &agent, &mind, &i, &tr) != EE_STATUS_OK) {
            failures++;
            break;
        }
        if (i.action == EE_ACTION_FLEE) r.flee++;
        if (t > 0 && prev != i.action) r.switches++;
        prev = i.action;
        const uint32_t first = tr.ranking & 0xFu;
        if (tr.selected != first && tr.selected != tr.incumbent) r.unexplained++;
        if ((tr.token != tr.token_prev) != (tr.cause != EE_CAUSE_NONE)) r.uncaused++;
        r.digest = r.digest * 1099511628211ULL + i.action;
        r.digest = r.digest * 1099511628211ULL + i.target_id;
        r.digest = r.digest * 1099511628211ULL + (uint64_t)(int64_t)i.move_x;
        r.digest = r.digest * 1099511628211ULL + i.flags;
        r.digest = r.digest * 1099511628211ULL + tr.ranking;
        r.digest = r.digest * 1099511628211ULL + tr.selected;
        r.digest = r.digest * 1099511628211ULL + tr.incumbent;
        r.digest = r.digest * 1099511628211ULL + tr.token;
        r.digest = r.digest * 1099511628211ULL + tr.cause;
        r.digest = r.digest * 1099511628211ULL + tr.attention;
    }
    return r;
}

/* "The crowd" — the fixture that exercises fear (see null_host.zig for the phases):
 * X (id 41) at 100000 until tick 90 and at 40000 after; health 1.0, then 0.1875 from
 * 10, 1.0 from 80, 0.244 from 120; three more contacts at 150000 from tick 30; an
 * empty magazine on 100..109. */
static ee_snapshot make_crowd_snapshot(uint64_t t)
{
    ee_snapshot s = make_snapshot(t);
    s.health = t < 10u ? EE_FX_ONE : t < 80u ? 12288 : t < 120u ? EE_FX_ONE : 16000;
    s.ammo = (t >= 100u && t < 110u) ? 0 : (ee_fx)(3 * EE_FX_ONE);
    memset(&s.contact0, 0, sizeof s.contact0);
    memset(&s.contact1, 0, sizeof s.contact1);
    memset(&s.contact2, 0, sizeof s.contact2);
    memset(&s.contact3, 0, sizeof s.contact3);
    s.contact0 = mind_contact(41, 0, 16384, 8192, t < 90u ? 100000 : 40000, EE_FX_ONE / 2);
    s.contact_count = 1;
    if (t >= 30u) {
        s.contact1 = mind_contact(42, 0, 8192, 8192, 150000, EE_FX_ONE / 2);
        s.contact2 = mind_contact(43, 0, 4096, 16384, 150000, EE_FX_ONE / 2);
        s.contact3 = mind_contact(44, 0, 16384, 4096, 150000, EE_FX_ONE / 2);
        s.contact_count = 4;
    }
    return s;
}

struct crowd_run { uint64_t digest; unsigned hesitates, reload, attacks_90_119, switches_after_120;
                   int first_flee, first_flee_after_120; uint32_t fear_at_119; unsigned unexplained, uncaused; };

static struct crowd_run run_crowd(void)
{
    struct crowd_run r;
    memset(&r, 0, sizeof r);
    r.first_flee = -1;
    r.first_flee_after_120 = -1;
    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.abi_minor = EE_ABI_MINOR;
    desc.capabilities = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT | EE_CAP_DAMAGE_EVENTS;
    desc.max_agents = 1;
    desc.rng_seed = 0xEE0000000000001ULL;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;
    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return r;
    }
    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = 0xEE0000000000001ULL;
    ee_mind mind;
    memset(&mind, 0, sizeof mind);
    mind.struct_size = (uint32_t)sizeof mind;
    uint32_t prev = 0;
    for (uint64_t t = 0; t < 200u; ++t) {
        ee_snapshot snap = make_crowd_snapshot(t);
        ee_intent i;
        ee_mind_trace tr;
        memset(&tr, 0, sizeof tr);
        tr.struct_size = (uint32_t)sizeof tr;
        if (ee_tick_mind(&ctx, &snap, &agent, &mind, &i, &tr) != EE_STATUS_OK) {
            failures++;
            break;
        }
        if (tr.tells & EE_TELL_HESITATE) r.hesitates++;
        if (i.action == EE_ACTION_FLEE && r.first_flee < 0) r.first_flee = (int)t;
        if (i.action == EE_ACTION_RELOAD) r.reload++;
        if (t >= 90u && t < 120u && i.action == EE_ACTION_ATTACK) r.attacks_90_119++;
        if (t == 119u) r.fear_at_119 = mind.fear;
        if (t >= 120u && i.action == EE_ACTION_FLEE && r.first_flee_after_120 < 0) r.first_flee_after_120 = (int)t;
        if (t > 120u && i.action != prev) r.switches_after_120++;
        prev = i.action;
        const uint32_t first = tr.ranking & 0xFu;
        if (tr.selected != first && tr.selected != tr.incumbent) r.unexplained++;
        if ((tr.token != tr.token_prev) != (tr.cause != EE_CAUSE_NONE)) r.uncaused++;
        r.digest = r.digest * 1099511628211ULL + i.action;
        r.digest = r.digest * 1099511628211ULL + i.target_id;
        r.digest = r.digest * 1099511628211ULL + (uint64_t)(int64_t)i.move_x;
        r.digest = r.digest * 1099511628211ULL + i.flags;
        r.digest = r.digest * 1099511628211ULL + tr.ranking;
        r.digest = r.digest * 1099511628211ULL + tr.selected;
        r.digest = r.digest * 1099511628211ULL + tr.incumbent;
        r.digest = r.digest * 1099511628211ULL + tr.token;
        r.digest = r.digest * 1099511628211ULL + tr.cause;
        r.digest = r.digest * 1099511628211ULL + tr.attention;
    }
    return r;
}

/* ── ABI 1.2: the model of the target's knowledge (ADR-0014) ─────────────────
 *
 * The same four fixtures as src/interface/ffi/test/null_host.zig, each with a control
 * that withholds EE_CAP_OBSERVATION_CUES and changes nothing else, through the exported
 * ee_tick_mind with a 1.2 record: `&model.mind`, `mind.struct_size = sizeof model`. */

enum model_fixture { STALKER, CAUGHT, FALSE_EXPOSURE, AMBUSH };

#define MODEL_CLOSE_STEP 2048
#define MODEL_CAUGHT_TICK 100u
#define MODEL_GLANCE_TICK 20u

struct model_run {
    uint64_t digest;
    long first_engage, caught_tick, first_exposed, first_stalk_after_cue, expiry_tick;
    ee_fx engage_distance;
    unsigned caught, exposed_ticks, exposed_no_cue, stalk_selected, stalk_ranked;
    unsigned stalk_after_cue, own_attack, max_rank, uncaused, unexplained, tells_mismatch;
};

static int model_cue_at(enum model_fixture fx, uint64_t t)
{
    return (fx == CAUGHT && t == MODEL_CAUGHT_TICK) || (fx == FALSE_EXPOSURE && t == MODEL_GLANCE_TICK);
}

static ee_snapshot make_model_snapshot(enum model_fixture fx, uint64_t t, ee_fx distance)
{
    ee_snapshot s;
    memset(&s, 0, sizeof s);
    s.tick = t;
    s.agent_id = 7;
    s.capability_mask = 0xFFFFFFFFu;
    s.health = EE_FX_ONE;
    s.ammo = (ee_fx)(3 * EE_FX_ONE);
    if (fx != AMBUSH || t < 30u) {
        const uint16_t fl = model_cue_at(fx, t) ? (uint16_t)EE_CONTACT_FLAG_OBSERVES : 0u;
        s.contact0 = mind_contact(41, fl, 16384, 8192, distance, EE_FX_ONE / 2);
        s.contact_count = 1;
    }
    return s;
}

#define FOLD(x) (r.digest = r.digest * 1099511628211ULL + (uint64_t)(x))

static struct model_run run_model(enum model_fixture fx, uint32_t caps, uint64_t ticks)
{
    struct model_run r;
    memset(&r, 0, sizeof r);
    r.first_engage = r.caught_tick = r.first_exposed = r.first_stalk_after_cue = r.expiry_tick = -1;

    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.abi_minor = EE_ABI_MINOR;
    desc.capabilities = caps;
    desc.max_agents = 1;
    desc.rng_seed = 0xEE0000000000001ULL;
    desc.abi_fingerprint = EE_ABI_FINGERPRINT;
    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return r;
    }
    check(ee_model_fingerprint() == EE_MODEL_FINGERPRINT, "the exported model fingerprint matches the header");

    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = 0xEE0000000000001ULL;
    ee_mind mind;
    memset(&mind, 0, sizeof mind);
    mind.struct_size = (uint32_t)sizeof mind;

    ee_fx distance = fx == AMBUSH ? 24576 : 196608;
    long first_cue = -1;
    for (uint64_t t = 0; t < ticks; ++t) {
        const ee_snapshot snap = make_model_snapshot(fx, t, distance);
        const int cue = model_cue_at(fx, t) && snap.contact_count > 0;
        if (cue && first_cue < 0) first_cue = (long)t;
        ee_intent i;
        ee_model_trace m;
        memset(&i, 0, sizeof i);
        memset(&m, 0, sizeof m);
        m.mind.struct_size = (uint32_t)sizeof m;
        if (ee_tick_mind(&ctx, &snap, &agent, &mind, &i, &m.mind) != EE_STATUS_OK) {
            failures++;
            break;
        }
        if (m.mind.struct_size != sizeof m) failures++;
        const ee_mind_trace *tr = &m.mind;

        if (i.action == EE_ACTION_ATTACK && r.first_engage < 0) {
            r.first_engage = (long)t;
            r.engage_distance = distance;
        }
        if (tr->tells & EE_TELL_CAUGHT) {
            r.caught++;
            if (r.caught_tick < 0) r.caught_tick = (long)t;
        }
        if (m.exposure == 1u) {
            r.exposed_ticks++;
            if (r.first_exposed < 0) r.first_exposed = (long)t;
            if (!cue) r.exposed_no_cue++;
        }
        if (tr->selected == EE_AGENT_MODE_STALK) {
            r.stalk_selected++;
            if (first_cue >= 0 && (long)t > first_cue) {
                r.stalk_after_cue++;
                if (r.first_stalk_after_cue < 0) r.first_stalk_after_cue = (long)t;
            }
        }
        for (unsigned k = 0; k < tr->ranking_count; ++k)
            if (((tr->ranking >> (4u * k)) & 0xFu) == EE_AGENT_MODE_STALK) r.stalk_ranked++;
        if (tr->ranking_count > r.max_rank) r.max_rank = tr->ranking_count;
        if (m.exposure_cause == EE_EXPOSURE_CAUSE_OWN_ATTACK) r.own_attack++;
        if (m.exposure_cause == EE_EXPOSURE_CAUSE_EXPIRY && r.expiry_tick < 0) r.expiry_tick = (long)t;
        if ((m.exposure != m.exposure_prev) != (m.exposure_cause != EE_EXPOSURE_CAUSE_NONE)) r.uncaused++;
        const uint32_t first = tr->ranking & 0xFu;
        if (tr->selected != first && tr->selected != tr->incumbent) r.unexplained++;
        if (((i.flags >> 4) & 0xFu) != tr->tells) r.tells_mismatch++;

        FOLD(i.action);
        FOLD(i.target_id);
        FOLD((int64_t)i.move_x);
        FOLD((int64_t)i.move_y);
        FOLD((int64_t)i.look_x);
        FOLD((int64_t)i.speed);
        FOLD(i.priority);
        FOLD(i.flags);
        FOLD(tr->ranking);
        FOLD(tr->ranking_count);
        FOLD(tr->selected);
        FOLD(tr->incumbent);
        FOLD(tr->token);
        FOLD(tr->cause);
        FOLD(tr->tells);
        FOLD(m.exposure);
        FOLD(m.exposure_prev);
        FOLD(m.exposure_cause);
        FOLD(m.exposure_contact);

        /* The world: the range closes while the agent moves toward the target; in
         * "false exposure" the target keeps its distance. */
        if (fx != FALSE_EXPOSURE && (i.action == EE_ACTION_ADVANCE || i.action == EE_ACTION_TAKE_COVER)) {
            distance -= (ee_fx)(((int64_t)i.speed * MODEL_CLOSE_STEP) >> 16);
            if (distance < 0) distance = 0;
        }
    }
    return r;
}

#undef FOLD

/* The 1.0 chase through ee_tick_mind with the reservoirs held at zero (Zig: "a mind held
 * at rest reproduces 1.0's chase trace exactly"). With the ten capabilities 1.1 defined
 * it is the 1.0 chase digest; with 0xFFFFFFFF — which now includes
 * EE_CAP_OBSERVATION_CUES and never gives a cue — the agent stalks (ADR-0014 §9). */
static uint64_t run_chase_at_rest(uint32_t caps, unsigned *stalks)
{
    ee_init_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = (uint32_t)sizeof desc;
    desc.abi_major = EE_ABI_MAJOR;
    desc.abi_minor = EE_ABI_MINOR;
    desc.capabilities = caps;
    desc.max_agents = 1;
    desc.rng_seed = 0xEE0000000000001ULL;
    ee_context ctx;
    memset(&ctx, 0, sizeof ctx);
    if (ee_init(&desc, &ctx) != EE_STATUS_OK) {
        failures++;
        return 0;
    }
    ee_agent agent;
    memset(&agent, 0, sizeof agent);
    agent.rng_state = 0xEE0000000000001ULL;
    ee_mind mind;
    memset(&mind, 0, sizeof mind);
    mind.struct_size = (uint32_t)sizeof mind;
    uint64_t digest = 0;
    *stalks = 0;
    for (uint64_t t = 0; t < 10000u; ++t) {
        const ee_snapshot snap = make_chase_snapshot(t);
        ee_intent i;
        memset(&i, 0, sizeof i);
        if (ee_tick_mind(&ctx, &snap, &agent, &mind, &i, NULL) != EE_STATUS_OK) {
            failures++;
            break;
        }
        mind.arousal = 0;
        mind.fear = 0;
        mind.grievance = 0;
        if (i.action == EE_ACTION_TAKE_COVER) (*stalks)++;
        digest = digest * 1099511628211ULL + i.action;
        digest = digest * 1099511628211ULL + i.target_id;
        digest = digest * 1099511628211ULL + (uint64_t)(int64_t)i.move_x;
        digest = digest * 1099511628211ULL + i.flags;
    }
    return digest;
}

static void print_model(const char *name, const struct model_run *r)
{
    printf("  model %-22s digest %llu (first_engage=%ld at=%d caught=%u@%ld exposed=%u exposed_no_cue=%u stalk=%u expiry=%ld own_attack=%u)\n",
           name, (unsigned long long)r->digest, r->first_engage, (int)r->engage_distance, r->caught,
           r->caught_tick, r->exposed_ticks, r->exposed_no_cue, r->stalk_selected, r->expiry_tick,
           r->own_attack);
}

static int model_invariants(const struct model_run *r)
{
    return r->uncaused == 0u && r->unexplained == 0u && r->tells_mismatch == 0u;
}

static int model_is_one_one(const struct model_run *r)
{
    return r->exposed_ticks == 0u && r->stalk_ranked == 0u && r->caught == 0u && r->max_rank <= 5u;
}

int main(void)
{
    /* Layout, as this compiler produced it. The header already asserted these at
     * compile time; printing them makes a failure legible rather than a wall of
     * notes from the compiler. */
    printf("evil-weevil ABI %u.%u, fingerprint %llu\n",
           (unsigned)EE_ABI_MAJOR, (unsigned)EE_ABI_MINOR,
           (unsigned long long)ee_abi_fingerprint());
    printf("  ee_version   %3zu   ee_init_desc %3zu   ee_contact %2zu\n",
           sizeof(ee_version), sizeof(ee_init_desc), sizeof(ee_contact));
    printf("  ee_snapshot  %3zu   ee_agent     %3zu   ee_intent  %2zu   ee_context %2zu\n",
           sizeof(ee_snapshot), sizeof(ee_agent), sizeof(ee_intent), sizeof(ee_context));

    const ee_version v = ee_version_info();
    check(v.major == EE_ABI_MAJOR && v.minor == EE_ABI_MINOR, "version_info matches the header");
    check(ee_abi_fingerprint() == EE_ABI_FINGERPRINT, "the exported fingerprint matches the header");
    check(ee_abi_fingerprint() == (uint64_t)(ee_init_desc){0}.abi_fingerprint + ee_abi_fingerprint(),
          "the fingerprint is a usable value (non-degenerate)");

    /* Refusals: a host that links must be told when it does not match. */
    {
        ee_context ctx;
        ee_init_desc desc;
        memset(&ctx, 0, sizeof ctx);
        memset(&desc, 0, sizeof desc);
        desc.struct_size = (uint32_t)sizeof desc;
        desc.abi_major = EE_ABI_MAJOR + 1;
        check(ee_init(&desc, &ctx) == EE_STATUS_VERSION_MISMATCH, "a future major is refused");
        desc.abi_major = EE_ABI_MAJOR;
        desc.abi_fingerprint = EE_ABI_FINGERPRINT + 1;
        check(ee_init(&desc, &ctx) == EE_STATUS_FINGERPRINT_MISMATCH, "a stale layout is refused");
        desc.struct_size = 4;
        desc.abi_fingerprint = EE_ABI_FINGERPRINT;
        check(ee_init(&desc, &ctx) == EE_STATUS_BUFFER_TOO_SMALL, "a short struct is refused");
        check(ee_init(NULL, &ctx) == EE_STATUS_BAD_PARAM, "a null descriptor is refused");
    }

    /* Determinism: two seeded runs, one digest. Plus the third run that makes
     * ADR-0009's compatibility claim a checked fact rather than a promise. */
    ee_context ctx_a, ctx_b, ctx_v0;
    const uint64_t first = run_trace(0xEE0000000000001ULL, &ctx_a, 1);
    const uint64_t second = run_trace(0xEE0000000000001ULL, &ctx_b, 1);
    const uint64_t v0 = run_trace(0xEE0000000000001ULL, &ctx_v0, 0);
    printf("  digest after 10,000 ticks (memory enabled): %llu\n", (unsigned long long)first);
    printf("  the same trace with the memory flag cleared: %llu\n", (unsigned long long)v0);
    check(first == second, "two runs of 10,000 ticks agree exactly");
    /* Pinned, not merely self-consistent: this number is asserted by BOTH hosts
     * (this one and src/interface/ffi/test/null_host.zig), so the C and Zig views
     * of the same fixture must agree bit for bit. Changing the rule changes this
     * number — that is the point. When you change it on purpose, change it in both
     * places and say why in ADR-0006.
     *
     * It has MOVED TWICE, both times deliberately, as ADR-0006 requires:
     *   4061121875253101873  ->  14165495496352896129  memory wired in (ADR-0009)
     *   14165495496352896129 ->  11339491356780309808  modes and the margin (ADR-0012)
     * A digest that moves when policy changes is the mechanism working; a digest that
     * moves with no recorded reason is the mechanism failing. */
    check(first == 11339491356780309808ULL, "the trace digest matches the pinned value");
    check(v0 == 1235117735680515552ULL,
          "the memory-off trace is stable and pinned (the v0 claim is RETIRED: modes changed it — ADR-0012)");
    check(first != v0, "memory is consulted in the default configuration — the check above is not vacuous");
    check(ctx_a.ticks_run == 10000u, "the context counted every tick");

    /* The scenario the v0 fixture cannot reach: a contact that leaves. */
    {
        ee_context ctx_m;
        unsigned counts[9];
        unsigned from_memory = 0;
        const uint64_t chase = run_chase_trace(0xEE0000000000001ULL, &ctx_m, counts, &from_memory);
        printf("  chase digest: %llu (investigate=%u advance=%u from_memory=%u)\n",
               (unsigned long long)chase, counts[EE_ACTION_INVESTIGATE],
               counts[EE_ACTION_ADVANCE], from_memory);
        check(chase == 12283675074724005768ULL, "the chase digest matches the pinned value");
        /* 36 is memory_ttl: this assertion is Abi.Memory's
         * freshJustBeforeTtl/staleAtTtl boundary, counted in a real run. */
        check(counts[EE_ACTION_INVESTIGATE] == 3600u, "the agent investigates for exactly 36 ticks per 100");
        check(counts[EE_ACTION_ADVANCE] == 6400u, "and advances for the other 64");
        check(counts[EE_ACTION_ATTACK] == 0u && counts[EE_ACTION_FLEE] == 0u &&
                  counts[EE_ACTION_RELOAD] == 0u,
              "no other action fires in the chase fixture");
        check(from_memory == counts[EE_ACTION_INVESTIGATE],
              "the FROM_MEMORY flag is set on investigate and nowhere else");
        check(ctx_m.ticks_run == 10000u, "the chase context counted every tick");
    }

    /* The S2 layer's own fixture: a moment where two behaviours are both legal and
     * adjacent, so an uncommitted rule flaps. Measured both ways on this fixture over
     * 100 ticks: 99 action switches for the graph alone, 0 with the margin. */
    {
        ee_context ctx_f;
        unsigned counts[9];
        unsigned switches = 0, mode_switches = 0;
        const uint64_t flap = run_flap_trace(0xEE0000000000001ULL, &ctx_f, counts,
                                             &switches, &mode_switches);
        printf("  flap digest: %llu (attack=%u flee=%u switches=%u mode_switches=%u)\n",
               (unsigned long long)flap, counts[EE_ACTION_ATTACK],
               counts[EE_ACTION_FLEE], switches, mode_switches);
        check(flap == 5191914376283395890ULL, "the flap digest matches the pinned value");
        check(switches == 0u, "the committed agent never changes its mind in 100 ticks");
        check(mode_switches == 0u, "and its mode never changes either");
        check(counts[EE_ACTION_ATTACK] == 100u, "it attacks on every tick");
        check(counts[EE_ACTION_FLEE] == 0u, "and never flees, though fleeing is legal here");
        check(ctx_f.ticks_run == 100u, "the flap context counted every tick");
    }

    /* The TTL boundary, watched at the agent rather than only in the model: 65
     * ticks in, the memory is one tick short of the TTL; the 66th reaches it and
     * forgets, clearing the payload rather than only lowering the flag. */
    {
        ee_context ctx;
        memset(&ctx, 0, sizeof ctx);
        ee_init_desc desc;
        memset(&desc, 0, sizeof desc);
        desc.struct_size = (uint32_t)sizeof desc;
        desc.abi_major = EE_ABI_MAJOR;
        desc.capabilities = EE_CAP_NAVMESH;
        desc.max_agents = 1;
        desc.abi_fingerprint = EE_ABI_FINGERPRINT;
        check(ee_init(&desc, &ctx) == EE_STATUS_OK, "memory probe context initialises");

        ee_agent agent;
        memset(&agent, 0, sizeof agent);
        ee_intent intent;
        for (uint64_t t = 0; t < 65u; ++t) {
            ee_snapshot snap = make_chase_snapshot(t);
            (void)ee_tick(&ctx, &snap, &agent, &intent);
        }
        check((agent.flags & (uint32_t)EE_AGENT_FLAG_MEMORY_VALID) != 0u,
              "the memory is still valid one tick short of the TTL");
        check(agent.memory_age == 35u, "the memory has aged to 35 (memory_ttl - 1)");

        {
            ee_snapshot snap = make_chase_snapshot(65);
            (void)ee_tick(&ctx, &snap, &agent, &intent);
        }
        check((agent.flags & (uint32_t)EE_AGENT_FLAG_MEMORY_VALID) == 0u,
              "the TTL is reached and the memory is forgotten");
        check(agent.memory_x == 0 && agent.memory_y == 0 && agent.memory_age == 0u,
              "forgetting cleared the payload, not just the flag");
    }

    /* A minimal sanity check on the output stream: the fixture must actually make
     * the agent do more than one thing, or "deterministic" would be vacuous. */
    {
        ee_context ctx;
        memset(&ctx, 0, sizeof ctx);
        ee_init_desc desc;
        memset(&desc, 0, sizeof desc);
        desc.struct_size = (uint32_t)sizeof desc;
        desc.abi_major = EE_ABI_MAJOR;
        desc.capabilities = EE_CAP_NAVMESH;
        desc.max_agents = 1;
        desc.abi_fingerprint = EE_ABI_FINGERPRINT;
        check(ee_init(&desc, &ctx) == EE_STATUS_OK, "sanity context initialises");

        unsigned seen[9];
        memset(seen, 0, sizeof seen);
        ee_agent agent;
        memset(&agent, 0, sizeof agent);
        for (uint64_t t = 0; t < 200u; ++t) {
            ee_snapshot snap = make_snapshot(t);
            ee_intent intent;
            if (ee_tick(&ctx, &snap, &agent, &intent) == EE_STATUS_OK && intent.action < 9u)
                seen[intent.action]++;
        }
        check(seen[EE_ACTION_RELOAD] > 0, "the agent reloads when out of ammo");
        check(seen[EE_ACTION_ADVANCE] > 0, "the agent advances with nothing to fight");
        check(seen[EE_ACTION_ATTACK] > 0, "the agent attacks when in range");
        check(seen[EE_ACTION_INVESTIGATE] > 0, "the agent investigates where a contact went");
        printf("  actions in 200 ticks: reload=%u advance=%u attack=%u flee=%u hold=%u investigate=%u\n",
               seen[EE_ACTION_RELOAD], seen[EE_ACTION_ADVANCE], seen[EE_ACTION_ATTACK],
               seen[EE_ACTION_FLEE], seen[EE_ACTION_HOLD], seen[EE_ACTION_INVESTIGATE]);
    }

    /* ABI 1.1: the mind. Four fixtures, four pinned digests shared with the Zig host,
     * and the two measured override counts the slice is judged on. */
    {
        const uint32_t caps = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT | EE_CAP_DAMAGE_EVENTS;
        const uint32_t caps_no_damage = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT;
        const struct mind_run g = run_mind(GRUDGE, caps, 200u);
        const struct mind_run gc = run_mind(GRUDGE, caps_no_damage, 200u);
        const struct mind_run f = run_mind(FLINCH, caps, 300u);
        const struct mind_run fc = run_mind(FLINCH_CONTROL, caps, 300u);
        print_mind("grudge", &g);
        print_mind("grudge-control", &gc);
        print_mind("flinch", &f);
        print_mind("flinch-control", &fc);

        check(g.digest == 5543746349701543370ULL, "the grudge digest matches the pinned value");
        check(gc.digest == 7615732920466510316ULL, "the grudge-control digest matches the pinned value");
        check(f.digest == 8915218119681936373ULL, "the flinch digest matches the pinned value");
        check(fc.digest == 5970917412374617350ULL, "the flinch-control digest matches the pinned value");

        check(g.overrides == 37u, "the grudge shows commitment override on 37 ticks");
        check(gc.overrides == 0u, "its control, without DAMAGE_EVENTS, shows none");
        check(gc.max_grievance == 0u, "without DAMAGE_EVENTS grievance stays at zero");
        check(f.startles == 1u && f.hesitates == 3u, "the flinch startles once and hesitates three ticks");
        check(fc.startles == 0u && fc.hesitates == 0u, "its control shows no tell");
        check(f.investigate == 39u && fc.investigate == 36u,
              "arousal lengthens the memory: 39 investigations against the control's 36");
        check(g.unexplained + gc.unexplained + f.unexplained + fc.unexplained == 0u,
              "every selection is first in the ranking or the incumbent");
        check(g.uncaused + gc.uncaused + f.uncaused + fc.uncaused == 0u,
              "every token change has a cause, and no unchanged token has one");
        check(g.over_mass + gc.over_mass + f.over_mass + fc.over_mass == 0u,
              "attention never exceeds full mass");
    }

    {
        const struct duel_run d = run_frightened_duel();
        printf("  mind %-15s digest %llu (flee=%u switches=%u)\n", "frightened-duel",
               (unsigned long long)d.digest, d.flee, d.switches);
        check(d.digest == 11063771574841757445ULL, "the frightened-duel digest matches the pinned value");
        check(d.switches == 0u && d.flee == 100u, "a frightened agent flees and does not flap");
        check(d.unexplained == 0u && d.uncaused == 0u, "the frightened duel's trace is explained and caused");
    }

    {
        const struct crowd_run c = run_crowd();
        printf("  mind %-15s digest %llu (hesitates=%u first_flee=%d reload=%u flee_after_120=%d fear@119=%u)\n",
               "crowd", (unsigned long long)c.digest, c.hesitates, c.first_flee, c.reload,
               c.first_flee_after_120, (unsigned)c.fear_at_119);
        check(c.digest == 2365169153326385312ULL, "the crowd digest matches the pinned value");
        check(c.hesitates == 3u && c.first_flee == 13, "flight waits three ticks, then goes");
        check(c.reload == 10u && c.attacks_90_119 == 20u, "the empty magazine is reloaded mid-engagement");
        check(c.first_flee_after_120 == 120 && c.switches_after_120 == 0u,
              "fear breaks the engagement when the wound returns, and the agent stays in flight");
        check(c.fear_at_119 == 43413u, "fear before the contest is the pinned value");
        check(c.unexplained == 0u && c.uncaused == 0u, "the crowd's trace is explained and caused");
    }

    /* ABI 1.2: the model. Four fixtures and four controls, eight pinned digests shared
     * with the Zig host, and the measured figures the slice is judged on. */
    {
        const uint32_t caps = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT | EE_CAP_OBSERVATION_CUES;
        const uint32_t no_cues = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT;
        const struct model_run st = run_model(STALKER, caps, 200u);
        const struct model_run stc = run_model(STALKER, no_cues, 200u);
        const struct model_run ca = run_model(CAUGHT, caps, 200u);
        const struct model_run cac = run_model(CAUGHT, no_cues, 200u);
        const struct model_run fe = run_model(FALSE_EXPOSURE, caps, 200u);
        const struct model_run fec = run_model(FALSE_EXPOSURE, no_cues, 200u);
        const struct model_run am = run_model(AMBUSH, caps, 200u);
        const struct model_run amc = run_model(AMBUSH, no_cues, 200u);
        print_model("stalker", &st);
        print_model("stalker-control", &stc);
        print_model("caught", &ca);
        print_model("caught-control", &cac);
        print_model("false-exposure", &fe);
        print_model("false-exposure-control", &fec);
        print_model("ambush", &am);
        print_model("ambush-control", &amc);

        const uint32_t caps_1_1 = EE_CAP_NAVMESH | EE_CAP_LINE_OF_SIGHT | EE_CAP_RAYCAST
            | EE_CAP_WAYPOINT_GRAPH | EE_CAP_SPAWN_RIGHTS | EE_CAP_ANIMATION_HOOKS | EE_CAP_DAMAGE_EVENTS
            | EE_CAP_SOUND_EVENTS | EE_CAP_VEHICLE_BOARDING | EE_CAP_TERRAIN_HEIGHT;
        unsigned rest_stalks = 0, all_stalks = 0;
        const uint64_t rest = run_chase_at_rest(caps_1_1, &rest_stalks);
        const uint64_t all_bits = run_chase_at_rest(0xFFFFFFFFu, &all_stalks);
        printf("  model %-22s digest %llu (stalks=%u)\n", "chase-at-rest-1.1-caps", (unsigned long long)rest, rest_stalks);
        printf("  model %-22s digest %llu (stalks=%u)\n", "chase-all-bits", (unsigned long long)all_bits, all_stalks);
        check(rest == 12283675074724005768ULL && rest_stalks == 0u,
              "a mind at rest with every 1.1 capability reproduces the 1.0 chase digest");
        check(all_bits == 421035655961441872ULL && all_stalks > 0u,
              "declaring every bit declares the cue capability: false concealment, pinned");

        check(st.digest == 5067313941334346860ULL, "the stalker digest matches the pinned value");
        check(stc.digest == 5359463348611598049ULL, "the stalker-control digest matches the pinned value");
        check(ca.digest == 3395485040061069912ULL, "the caught digest matches the pinned value");
        check(cac.digest == 5359463348611598049ULL, "the caught control is the stalker control: the cue bit is ignored without the capability");
        check(fe.digest == 11105738738098638480ULL, "the false-exposure digest matches the pinned value");
        check(fec.digest == 11366903142131820018ULL, "the false-exposure-control digest matches the pinned value");
        check(am.digest == 1370696199260345134ULL, "the ambush digest matches the pinned value");
        check(amc.digest == 10128845661911672413ULL, "the ambush-control digest matches the pinned value");

        check(st.first_engage == 160 && st.engage_distance == 32768, "the stalker engages only inside strike range, at tick 160");
        check(stc.first_engage == 64 && stc.engage_distance == 65536, "its control engages earlier, at tick 64, at attack range");
        check(ca.caught == 1u && ca.caught_tick == 100 && ca.first_exposed == 100, "one CAUGHT tell, on the cue tick, exposed from it");
        check(ca.stalk_after_cue == 0u, "after being caught the agent never stalks again");
        check(fe.exposed_no_cue == 90u && fec.exposed_no_cue == 0u, "false exposure: 90 exposed ticks with no cue, against 0");
        check(fe.expiry_tick == 111 && fe.first_stalk_after_cue == 111, "the belief expires at 111 and the stalk resumes");
        check(am.first_exposed == 0 && am.own_attack == 1u && am.expiry_tick == 120, "the agent's own attack sets exposure, which then expires");
        check(model_is_one_one(&stc) && model_is_one_one(&cac) && model_is_one_one(&fec) && model_is_one_one(&amc),
              "every control is ABI 1.1: no exposure, no stalk, no CAUGHT");
        check(model_invariants(&st) && model_invariants(&stc) && model_invariants(&ca) && model_invariants(&cac)
              && model_invariants(&fe) && model_invariants(&fec) && model_invariants(&am) && model_invariants(&amc),
              "every exposure change has a cause, every selection is explained, every tell is on the intent");
    }

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("null host: OK — the kernel ran with no engine behind it.\n");
    return 0;
}
