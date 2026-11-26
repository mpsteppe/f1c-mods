/*
 * Pure C logic for Phase 2: Interactive Mid-Race Setup LCD Screen (Mode 5).
 * Standalone header (no Win32 or game-memory dependencies) for unit testing.
 */
#ifndef ESH_MIDRACE_SETUP_H
#define ESH_MIDRACE_SETUP_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ESH_PARAM_COUNT      12
#define ESH_VISIBLE_ROWS     5
#define ESH_MAX_TORQUE_BINS  256
#define ESH_RADS_TO_RPM      9.549296585513721
#define ESH_ENGBRAKE_MULT    0.09549296585513721

/* setupStruct base offset for parameter array (stride 0x40 bytes each). */
#define ESH_SETUP_ARRAY_BASE   0x0BB0u
#define ESH_SETUP_PARAM_STRIDE 0x0040u

/* Authoritative setup indices from FUN_0042f470 / FUN_0043c250. */
#define ESH_IDX_BRAKE_BIAS     0x1Fu /* +0x1370 */
#define ESH_IDX_DIFF_PUMP      0x23u /* +0x1470 */
#define ESH_IDX_DIFF_POWER     0x24u /* +0x14B0 */
#define ESH_IDX_DIFF_COAST     0x25u /* +0x14F0 */
#define ESH_IDX_DIFF_PRELOAD   0x26u /* +0x1530 */
#define ESH_IDX_REV_LIMIT      0x27u /* +0x1570 */
#define ESH_IDX_ENGINE_MAP     0x28u /* +0x15B0 */
#define ESH_IDX_ENG_BRAKE      0x29u /* +0x15F0 */
#define ESH_IDX_SYNTHETIC      0xFFFFFFFFu

/* HDV struct scalar offsets from FUN_0043c250 / FUN_004f4270. */
#define ESH_HDV_OFF_TC_GRIP_MULT     0x0630u
#define ESH_HDV_OFF_TC_GRIP_ADD      0x0638u
#define ESH_HDV_OFF_ABS_GRIP_MULT    0x0654u
#define ESH_HDV_OFF_ABS_GRIP_ADD     0x065Cu
#define ESH_HDV_OFF_REV_LIMIT_LOGIC  0x06BCu
#define ESH_HDV_OFF_DIFF_PUMP_TORQUE 0x13C8u

typedef enum {
    ESH_PARAM_REV_LIMIT = 0,
    ESH_PARAM_ENGINE_MAP = 1,
    ESH_PARAM_ENG_BRAKE = 2,
    ESH_PARAM_BRAKE_BIAS = 3,
    ESH_PARAM_DIFF_PRELOAD = 4,
    ESH_PARAM_DIFF_POWER = 5,
    ESH_PARAM_DIFF_COAST = 6,
    ESH_PARAM_DIFF_PUMP = 7,
    ESH_PARAM_TC_GRIP_MULT = 8,
    ESH_PARAM_TC_GRIP_ADD = 9,
    ESH_PARAM_ABS_GRIP_MULT = 10,
    ESH_PARAM_ABS_GRIP_ADD = 11
} EshParamId;

typedef struct {
    double base;
    double step;
    int32_t count;
    int32_t setting;
} EshParamSlot;

typedef struct {
    EshParamSlot params[ESH_PARAM_COUNT];
    double rev_limit_logic;
    double diff_pump_torque;
    double base_fuel_consumption;
    double base_combustion_heat;
    double base_engine_speed_heat;
    float base_torque_table[ESH_MAX_TORQUE_BINS * 2];
    int32_t torque_bin_count;
} EshSetupState;

typedef struct {
    int32_t active_ids[ESH_PARAM_COUNT];
    int32_t active_count;
    int32_t selected_row;
    int32_t scroll_top;
    int32_t initialized;
} EshMenuState;

typedef struct {
    const char *label;
    const char *ini_key;
    uint32_t setup_index;
} EshParamMeta;

static inline const EshParamMeta *esh_param_meta(EshParamId id)
{
    static const EshParamMeta kMeta[ESH_PARAM_COUNT] = {
        { "REV LIMIT:",     "ShowRevLimit",    ESH_IDX_REV_LIMIT    },
        { "ENGINE MAP:",    "ShowEngineMap",   ESH_IDX_ENGINE_MAP   },
        { "ENG BRAKE:",     "ShowEngineBrake", ESH_IDX_ENG_BRAKE    },
        { "BRAKE BIAS:",    "ShowBrakeBias",   ESH_IDX_BRAKE_BIAS   },
        { "DIFF PRELOAD:",  "ShowDiffPreload", ESH_IDX_DIFF_PRELOAD },
        { "DIFF POWER:",    "ShowDiffPower",   ESH_IDX_DIFF_POWER   },
        { "DIFF COAST:",    "ShowDiffCoast",   ESH_IDX_DIFF_COAST   },
        { "DIFF PUMP:",     "ShowDiffPump",    ESH_IDX_DIFF_PUMP    },
        { "TC GRIP MULT:",  "ShowTcGripMult",  ESH_IDX_SYNTHETIC    },
        { "TC GRIP ADD:",   "ShowTcGripAdd",   ESH_IDX_SYNTHETIC    },
        { "ABS GRIP MULT:", "ShowAbsGripMult", ESH_IDX_SYNTHETIC    },
        { "ABS GRIP ADD:",  "ShowAbsGripAdd",  ESH_IDX_SYNTHETIC    }
    };
    if ((int)id < 0 || (int)id >= ESH_PARAM_COUNT)
        return &kMeta[0];
    return &kMeta[(int)id];
}

static inline uint32_t esh_setup_block_offset(uint32_t setup_index)
{
    return ESH_SETUP_ARRAY_BASE + setup_index * ESH_SETUP_PARAM_STRIDE;
}

static inline void esh_clamp_slot(EshParamSlot *slot)
{
    if (slot->count < 1)
        slot->count = 1;
    if (slot->setting < 0)
        slot->setting = 0;
    if (slot->setting >= slot->count)
        slot->setting = slot->count - 1;
}

static inline void esh_init_synthetic_slot(
    EshParamSlot *slot,
    double center_val,
    double step,
    int32_t half_steps)
{
    if (!slot)
        return;
    slot->step = step;
    slot->count = half_steps * 2 + 1;
    slot->setting = half_steps;
    slot->base = center_val - (double)half_steps * step;
}

static inline void esh_setup_snapshot(
    EshSetupState *state,
    const void *setup_struct,
    const void *hdv_struct)
{
    const uint8_t *sb = (const uint8_t *)setup_struct;
    const uint8_t *hb = (const uint8_t *)hdv_struct;
    int i;
    if (!state || !sb || !hb)
        return;

    for (i = 0; i < ESH_PARAM_COUNT; i++) {
        uint32_t idx = esh_param_meta((EshParamId)i)->setup_index;
        if (idx != ESH_IDX_SYNTHETIC) {
            const uint8_t *blk = sb + esh_setup_block_offset(idx);
            state->params[i].base = *(const double *)(blk + 0x00);
            state->params[i].step = *(const double *)(blk + 0x08);
            state->params[i].count = *(const int32_t *)(blk + 0x10);
            state->params[i].setting = *(const int32_t *)(blk + 0x14);
            esh_clamp_slot(&state->params[i]);
        }
    }

    esh_init_synthetic_slot(
        &state->params[ESH_PARAM_TC_GRIP_MULT],
        *(const double *)(hb + ESH_HDV_OFF_TC_GRIP_MULT), 0.02, 20);
    esh_init_synthetic_slot(
        &state->params[ESH_PARAM_TC_GRIP_ADD],
        *(const double *)(hb + ESH_HDV_OFF_TC_GRIP_ADD), 0.01, 20);
    esh_init_synthetic_slot(
        &state->params[ESH_PARAM_ABS_GRIP_MULT],
        *(const double *)(hb + ESH_HDV_OFF_ABS_GRIP_MULT), 0.02, 20);
    esh_init_synthetic_slot(
        &state->params[ESH_PARAM_ABS_GRIP_ADD],
        *(const double *)(hb + ESH_HDV_OFF_ABS_GRIP_ADD), 0.01, 20);

    state->rev_limit_logic = *(const double *)(hb + ESH_HDV_OFF_REV_LIMIT_LOGIC);
    state->diff_pump_torque = *(const double *)(hb + ESH_HDV_OFF_DIFF_PUMP_TORQUE);
}

static inline double esh_param_raw_value(const EshParamSlot *slot)
{
    double v;
    if (!slot)
        return 0.0;
    v = (double)slot->setting * slot->step + slot->base;
    return v;
}

static inline double esh_param_nonneg_value(const EshParamSlot *slot)
{
    double v = esh_param_raw_value(slot);
    return v < 0.0 ? 0.0 : v;
}

static inline int esh_param_adjust(EshSetupState *state, EshParamId id, int delta)
{
    EshParamSlot *slot;
    int32_t next;
    if (!state || (int)id < 0 || (int)id >= ESH_PARAM_COUNT)
        return 0;
    slot = &state->params[(int)id];
    if (slot->count <= 1)
        return 0;
    next = slot->setting + delta;
    if (next < 0)
        next = 0;
    if (next >= slot->count)
        next = slot->count - 1;
    if (next == slot->setting)
        return 0;
    slot->setting = next;
    return 1;
}

static inline double esh_engine_map_delta(const EshParamSlot *slot)
{
    int32_t mid;
    if (!slot || slot->count <= 1)
        return 0.0;
    mid = (slot->count - 1) / 2;
    return (double)(slot->setting - mid);
}

static inline double esh_engine_map_fuel_scale(
    const EshParamSlot *slot,
    double boost_fuel)
{
    double scale = 1.0 + esh_engine_map_delta(slot) * boost_fuel;
    return scale < 0.1 ? 0.1 : scale;
}

static inline double esh_engine_map_heat_scale(
    const EshParamSlot *slot,
    double boost_heat)
{
    double scale = 1.0 + esh_engine_map_delta(slot) * boost_heat;
    return scale < 0.1 ? 0.1 : scale;
}

static inline void esh_engine_map_scale_torque_table(
    const float *base_table,
    float *live_table,
    int num_bins,
    const EshParamSlot *slot,
    double boost_torque,
    double boost_power)
{
    double d;
    int k;
    if (!base_table || !live_table || num_bins <= 0)
        return;
    d = esh_engine_map_delta(slot);
    for (k = 0; k < num_bins; k++) {
        float coast = base_table[2 * k + 0];
        float base_delta = base_table[2 * k + 1];
        float t_orig = coast + base_delta;
        live_table[2 * k + 0] = coast;
        if (t_orig > 0.0f && d != 0.0) {
            double t1 = (double)t_orig * (1.0 + d * boost_torque);
            double t2 = t1 * (1.0 + d * (double)k * boost_power);
            if (t2 < (double)coast)
                t2 = (double)coast;
            live_table[2 * k + 1] = (float)(t2 - (double)coast);
        } else {
            live_table[2 * k + 1] = base_delta;
        }
    }
}

static inline void esh_menu_configure(
    EshMenuState *menu,
    const int enabled_flags[ESH_PARAM_COUNT])
{
    int i;
    if (!menu)
        return;
    menu->active_count = 0;
    for (i = 0; i < ESH_PARAM_COUNT; i++) {
        if (!enabled_flags || enabled_flags[i] != 0)
            menu->active_ids[menu->active_count++] = i;
    }
    menu->selected_row = 0;
    menu->scroll_top = 0;
}

static inline int esh_menu_visible_rows(const EshMenuState *menu)
{
    if (!menu || menu->active_count <= 0)
        return 0;
    return menu->active_count < ESH_VISIBLE_ROWS
        ? (int)menu->active_count
        : ESH_VISIBLE_ROWS;
}

static inline int esh_menu_selected_param_id(
    const EshMenuState *menu,
    EshParamId *out_id)
{
    if (!menu || menu->active_count <= 0 ||
        menu->selected_row < 0 || menu->selected_row >= menu->active_count)
        return 0;
    if (out_id)
        *out_id = (EshParamId)menu->active_ids[menu->selected_row];
    return 1;
}

static inline void esh_menu_reset(EshMenuState *menu, int32_t initialized)
{
    int i;
    if (!menu)
        return;
    if (menu->active_count < 0 || menu->active_count > ESH_PARAM_COUNT) {
        for (i = 0; i < ESH_PARAM_COUNT; i++)
            menu->active_ids[i] = i;
        menu->active_count = ESH_PARAM_COUNT;
    }
    menu->selected_row = 0;
    menu->scroll_top = 0;
    menu->initialized = initialized;
}

static inline void esh_menu_move_selection(EshMenuState *menu, int delta)
{
    int vis_rows;
    int max_scroll;
    int next;
    if (!menu || menu->active_count <= 0)
        return;
    next = (menu->selected_row + delta) % menu->active_count;
    if (next < 0)
        next += menu->active_count;
    menu->selected_row = next;

    vis_rows = esh_menu_visible_rows(menu);
    if (menu->selected_row < menu->scroll_top)
        menu->scroll_top = menu->selected_row;
    if (menu->selected_row >= menu->scroll_top + vis_rows)
        menu->scroll_top = menu->selected_row - vis_rows + 1;

    max_scroll = menu->active_count - vis_rows;
    if (max_scroll < 0)
        max_scroll = 0;
    if (menu->scroll_top < 0)
        menu->scroll_top = 0;
    if (menu->scroll_top > max_scroll)
        menu->scroll_top = max_scroll;
}

static inline void esh_param_format_value(
    const EshSetupState *state,
    EshParamId id,
    char *buf,
    size_t buf_size)
{
    const EshParamSlot *slot;
    double raw;
    if (!buf || buf_size == 0)
        return;
    if (!state || (int)id < 0 || (int)id >= ESH_PARAM_COUNT) {
        buf[0] = '\0';
        return;
    }
    slot = &state->params[(int)id];
    raw = esh_param_raw_value(slot);

    switch (id) {
    case ESH_PARAM_REV_LIMIT: {
        int rpm = (int)floor(raw * ESH_RADS_TO_RPM + 0.5);
        if (rpm < 0)
            rpm = 0;
        if (rpm >= 1000)
            snprintf(buf, buf_size, "%d,%03d", rpm / 1000, rpm % 1000);
        else
            snprintf(buf, buf_size, "%d", rpm);
        break;
    }
    case ESH_PARAM_ENGINE_MAP:
    case ESH_PARAM_DIFF_PRELOAD:
        snprintf(buf, buf_size, "%d", (int)(slot->setting + 1));
        break;
    case ESH_PARAM_ENG_BRAKE:
        snprintf(buf, buf_size, "%d", (int)slot->setting);
        break;
    case ESH_PARAM_BRAKE_BIAS: {
        double rear_pct = raw * 100.0;
        double front_pct = 100.0 - rear_pct;
        snprintf(buf, buf_size, "%.1f:%.1f", front_pct, rear_pct);
        break;
    }
    case ESH_PARAM_DIFF_POWER:
    case ESH_PARAM_DIFF_COAST:
    case ESH_PARAM_DIFF_PUMP: {
        int pct = (int)floor(raw * 100.0 + 0.5);
        snprintf(buf, buf_size, "%d%%", pct);
        break;
    }
    case ESH_PARAM_TC_GRIP_MULT:
    case ESH_PARAM_TC_GRIP_ADD:
    case ESH_PARAM_ABS_GRIP_MULT:
    case ESH_PARAM_ABS_GRIP_ADD:
        snprintf(buf, buf_size, "%.2f", esh_param_nonneg_value(slot));
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

static inline int esh_compute_last_vanilla_cockpit_mode(uint8_t lcd_mask)
{
    if ((lcd_mask & 0x04u) != 0)
        return 4;
    if ((lcd_mask & 0x02u) != 0)
        return 3;
    if ((lcd_mask & 0x01u) != 0)
        return 2;
    return 1;
}

typedef struct {
    int32_t session_id;         /* 0=Test, 1..4=FP1..4, 5=Qual, 6=Warmup, 7=Race */
    int32_t map_count;          /* number of engine maps (default 5 -> settings 0..4) */
    int32_t slot_seed;          /* deterministic per-car/per-lap seed */
    int32_t in_pit_or_caution;  /* 1 if in pit lane or non-green session phase */
    int32_t total_stops;        /* planned pit stops (1, 2, 3...) */
    int32_t completed_laps;     /* laps completed by this car */
    int32_t total_race_laps;    /* total race laps */
    int32_t next_pit_lap;       /* target pit lap for current stint (or total_race_laps) */
    float stint_laps_remaining; /* exact fractional laps left in current stint */
    float current_fuel_liters;  /* current fuel in tank */
    float fuel_per_lap_liters;  /* estimated fuel burned per lap */
    float engine_life_seconds;  /* remaining engine life timer (< 1e30 when finite) */
    float est_race_seconds_left;/* estimated seconds left in race */
    int32_t engine_wounded;     /* 1 if engine already smoking (-789.0f latch) */
    float gap_ahead_seconds;    /* time gap to car in pos - 1 (or >99 if none) */
    float gap_behind_seconds;   /* time gap to car in pos + 1 (or >99 if none) */
    int32_t is_yielding;        /* 1 if yielding to teammate (orders) or lead-lap car */
} EshAiContext;

static inline double esh_ai_race_fuel_buffer_mult(
    int32_t total_stops,
    double fuel_step)
{
    double factor = 1.0;
    if (fuel_step < 0.0)
        fuel_step = 0.0;
    if (total_stops <= 1)
        factor = 0.50;
    else if (total_stops == 2)
        factor = 1.00;
    else
        factor = 1.75;
    return 1.0 + factor * fuel_step;
}

static inline int32_t esh_ai_clamp_map(int32_t setting, int32_t min_v, int32_t max_v)
{
    if (setting < min_v)
        return min_v;
    if (setting > max_v)
        return max_v;
    return setting;
}

static inline int32_t esh_ai_decide_engine_map(const EshAiContext *ctx)
{
    int32_t count, mid, max_set, max_allowed, target;
    float fuel_needed, fuel_margin_laps;
    uint32_t seed;
    if (!ctx)
        return 0;

    count = ctx->map_count > 0 ? ctx->map_count : 5;
    max_set = count - 1;
    mid = max_set / 2;
    seed = (uint32_t)(ctx->slot_seed < 0 ? -ctx->slot_seed : ctx->slot_seed);

    /* Yielding to teammate (driver orders) or lead-lap car (blue flag):
     * drop to lowest engine map so the passing car gets by cleanly. */
    if (ctx->is_yielding)
        return 0;

    /* Qualifying: max performance unless in pit lane or engine wounded */
    if (ctx->session_id == 5) {
        if (ctx->in_pit_or_caution || ctx->engine_wounded)
            return 0;
        return max_set;
    }

    /* Test Day (0) and Warm-Up (6): Map 3 baseline (conserve if low life/wounded) */
    if (ctx->session_id == 0 || ctx->session_id == 6) {
        if (ctx->in_pit_or_caution || ctx->engine_wounded ||
            (ctx->engine_life_seconds > 0.0f && ctx->engine_life_seconds < 240.0f))
            return 0;
        return mid;
    }

    /* Free Practice (FP1..FP4): Maps 1, 2, or 3 only (never show full power) */
    if (ctx->session_id >= 1 && ctx->session_id <= 4) {
        int32_t fp_pick;
        if (ctx->in_pit_or_caution || ctx->engine_wounded ||
            (ctx->engine_life_seconds > 0.0f && ctx->engine_life_seconds < 360.0f))
            return 0;
        if ((seed & 3u) == 0u)
            fp_pick = 0;
        else if ((seed & 3u) == 3u)
            fp_pick = mid;
        else
            fp_pick = mid > 0 ? mid - 1 : 0;
        return esh_ai_clamp_map(fp_pick, 0, mid);
    }

    /* Race (7) */
    if (ctx->in_pit_or_caution || ctx->engine_wounded)
        return 0;

    fuel_needed = ctx->stint_laps_remaining * ctx->fuel_per_lap_liters;
    fuel_margin_laps = (ctx->fuel_per_lap_liters > 0.01f)
        ? ((ctx->current_fuel_liters - fuel_needed) / ctx->fuel_per_lap_liters)
        : 2.0f;

    /* Hard safety override: critical fuel shortage */
    if (fuel_margin_laps < 0.25f)
        return 0;

    max_allowed = max_set;

    /* Engine lifetime preservation rule */
    if (ctx->engine_life_seconds > 0.0f && ctx->engine_life_seconds < 1.0e30f) {
        if (ctx->engine_life_seconds < 180.0f ||
            (ctx->est_race_seconds_left > 0.0f &&
             ctx->engine_life_seconds < 1.05f * ctx->est_race_seconds_left)) {
            return 0;
        }
        if (ctx->engine_life_seconds < 360.0f ||
            (ctx->est_race_seconds_left > 0.0f &&
             ctx->engine_life_seconds < 1.25f * ctx->est_race_seconds_left)) {
            int32_t cap = mid > 0 ? mid - 1 : 0;
            if (cap < max_allowed)
                max_allowed = cap;
        }
    }

    if (fuel_margin_laps < 0.65f) {
        int32_t cap = mid > 0 ? mid - 1 : 0;
        if (cap < max_allowed)
            max_allowed = cap;
    }

    /* Strategy baseline */
    target = mid;
    if (ctx->total_stops == 1)
        target = mid > 0 ? mid - 1 : 0;
    else if (ctx->total_stops >= 3 && fuel_margin_laps > 0.90f)
        target = mid < max_set ? mid + 1 : max_set;

    /* Situational modifiers */
    if (ctx->completed_laps == 0) {
        /* Lap 1 opening push */
        target = (seed & 1u) ? max_set : (mid < max_set ? mid + 1 : max_set);
    } else if (ctx->next_pit_lap > 0 &&
               ctx->completed_laps == ctx->next_pit_lap - 1 &&
               ctx->next_pit_lap < ctx->total_race_laps &&
               fuel_margin_laps > 0.45f) {
        /* Final lap of stint (in-lap) */
        target = (fuel_margin_laps > 0.85f)
            ? max_set
            : (mid < max_set ? mid + 1 : max_set);
    } else if (ctx->total_race_laps > 0 &&
               (ctx->total_race_laps - ctx->completed_laps) <= 3 &&
               fuel_margin_laps > 0.40f) {
        /* Final 3 laps of race */
        if (ctx->gap_ahead_seconds < 2.5f || ctx->gap_behind_seconds < 2.0f ||
            fuel_margin_laps > 0.80f) {
            target = (fuel_margin_laps > 0.70f)
                ? max_set
                : (mid < max_set ? mid + 1 : max_set);
        }
    } else if (ctx->gap_ahead_seconds < 1.2f || ctx->gap_behind_seconds < 1.0f) {
        /* Attacking or defending battle (not always, ~75% engagement) */
        if ((seed % 4u) != 0u) {
            if ((ctx->gap_ahead_seconds < 0.6f || ctx->gap_behind_seconds < 0.5f) &&
                fuel_margin_laps > 0.85f)
                target = max_set;
            else
                target = mid < max_set ? mid + 1 : max_set;
        }
    } else if (ctx->gap_ahead_seconds > 3.5f && ctx->gap_behind_seconds > 3.5f) {
        /* Not under threat: save fuel and cool engine */
        if (ctx->gap_ahead_seconds > 7.0f && ctx->gap_behind_seconds > 7.0f &&
            fuel_margin_laps < 1.10f)
            target = 0;
        else
            target = mid > 0 ? mid - 1 : 0;
    }

    return esh_ai_clamp_map(target, 0, max_allowed);
}

#endif /* ESH_MIDRACE_SETUP_H */
