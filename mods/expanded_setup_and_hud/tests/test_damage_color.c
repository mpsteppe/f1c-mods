#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/esh_damage_color.h"
#include "../src/esh_midrace_setup.h"

#define ASSERT_EQ_U32(actual, expected) do { \
    uint32_t _a = (actual); \
    uint32_t _e = (expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: got 0x%08X, expected 0x%08X\n", \
                __FILE__, __LINE__, _a, _e); \
        return 1; \
    } \
} while (0)

#define ASSERT_NEAR(actual, expected, tol) do { \
    double _a = (actual); \
    double _e = (expected); \
    if (fabs(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: got %.9f, expected %.9f\n", \
                __FILE__, __LINE__, _a, _e); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_I32(actual, expected) do { \
    int32_t _a = (actual); \
    int32_t _e = (expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: got %d, expected %d\n", \
                __FILE__, __LINE__, (int)_a, (int)_e); \
        return 1; \
    } \
} while (0)

#define ASSERT_STR_EQ(actual, expected) do { \
    const char *_a = (actual); \
    const char *_e = (expected); \
    if (strcmp(_a, _e) != 0) { \
        fprintf(stderr, "FAIL %s:%d: got '%s', expected '%s'\n", \
                __FILE__, __LINE__, _a, _e); \
        return 1; \
    } \
} while (0)

static void write_setup_slot(
    uint8_t *setup_buf,
    uint32_t index,
    double base,
    double step,
    int32_t count,
    int32_t setting)
{
    uint8_t *blk = setup_buf + esh_setup_block_offset(index);
    *(double *)(blk + 0x00) = base;
    *(double *)(blk + 0x08) = step;
    *(int32_t *)(blk + 0x10) = count;
    *(int32_t *)(blk + 0x14) = setting;
}

static int test_midrace_setup(void)
{
    uint8_t fake_setup[0x2000];
    uint8_t fake_hdv[0x2000];
    EshSetupState st;
    EshMenuState menu;
    char buf[32];

    memset(fake_setup, 0, sizeof fake_setup);
    memset(fake_hdv, 0, sizeof fake_hdv);

    /* 18,000 RPM = 1884.95559 rad/s; step 100 RPM = 10.4719755 rad/s */
    write_setup_slot(fake_setup, ESH_IDX_REV_LIMIT, 1780.235837, 10.471975512, 11, 10);
    write_setup_slot(fake_setup, ESH_IDX_ENGINE_MAP, 0.0, 1.0, 5, 2);
    write_setup_slot(fake_setup, ESH_IDX_ENG_BRAKE, 0.0, 1.0, 6, 3);
    write_setup_slot(fake_setup, ESH_IDX_BRAKE_BIAS, 0.20, 0.005, 121, 42);
    write_setup_slot(fake_setup, ESH_IDX_DIFF_PRELOAD, 20.0, 10.0, 8, 3);
    write_setup_slot(fake_setup, ESH_IDX_DIFF_POWER, 0.10, 0.05, 19, 9);
    write_setup_slot(fake_setup, ESH_IDX_DIFF_COAST, 0.10, 0.05, 19, 4);
    write_setup_slot(fake_setup, ESH_IDX_DIFF_PUMP, 0.0, 0.05, 1, 0); /* locked */

    *(double *)(fake_hdv + ESH_HDV_OFF_TC_GRIP_MULT) = 1.20;
    *(double *)(fake_hdv + ESH_HDV_OFF_TC_GRIP_ADD) = 0.16;
    *(double *)(fake_hdv + ESH_HDV_OFF_ABS_GRIP_MULT) = 1.00;
    *(double *)(fake_hdv + ESH_HDV_OFF_ABS_GRIP_ADD) = 0.10;
    *(double *)(fake_hdv + ESH_HDV_OFF_REV_LIMIT_LOGIC) = 25.0;
    *(double *)(fake_hdv + ESH_HDV_OFF_DIFF_PUMP_TORQUE) = 400.0;

    esh_setup_snapshot(&st, fake_setup, fake_hdv);
    ASSERT_NEAR(st.rev_limit_logic, 25.0, 1e-12);
    ASSERT_NEAR(st.diff_pump_torque, 400.0, 1e-12);

    esh_param_format_value(&st, ESH_PARAM_REV_LIMIT, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "18,000");
    esh_param_format_value(&st, ESH_PARAM_ENGINE_MAP, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "3");
    esh_param_format_value(&st, ESH_PARAM_ENG_BRAKE, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "3");
    esh_param_format_value(&st, ESH_PARAM_BRAKE_BIAS, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "59.0:41.0");
    esh_param_format_value(&st, ESH_PARAM_DIFF_PRELOAD, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "4");
    esh_param_format_value(&st, ESH_PARAM_DIFF_POWER, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "55%");
    esh_param_format_value(&st, ESH_PARAM_DIFF_COAST, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "30%");
    esh_param_format_value(&st, ESH_PARAM_DIFF_PUMP, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "0%");
    esh_param_format_value(&st, ESH_PARAM_TC_GRIP_MULT, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "1.20");
    esh_param_format_value(&st, ESH_PARAM_TC_GRIP_ADD, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "0.16");
    esh_param_format_value(&st, ESH_PARAM_ABS_GRIP_MULT, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "1.00");
    esh_param_format_value(&st, ESH_PARAM_ABS_GRIP_ADD, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "0.10");

    /* Fuel Mixture Engine Map fuel, heat, and torque/power curve scaling (Option A) */
    {
        float base_tbl[120];
        float live_tbl[120];
        int k;
        for (k = 0; k < 60; k++) {
            base_tbl[2 * k + 0] = -50.0f;
            base_tbl[2 * k + 1] = 350.0f; /* T_orig = 300.0 Nm */
        }
        /* Setting 2 (Map 3 of 5) -> exact vanilla */
        ASSERT_NEAR(esh_engine_map_fuel_scale(&st.params[ESH_PARAM_ENGINE_MAP], 0.020), 1.0, 1e-12);
        ASSERT_NEAR(esh_engine_map_heat_scale(&st.params[ESH_PARAM_ENGINE_MAP], 0.025), 1.0, 1e-12);
        esh_engine_map_scale_torque_table(base_tbl, live_tbl, 60, &st.params[ESH_PARAM_ENGINE_MAP], 0.0075, 0.000250);
        ASSERT_NEAR(live_tbl[0], -50.0f, 1e-6);
        ASSERT_NEAR(live_tbl[1], 350.0f, 1e-6);
        ASSERT_NEAR(live_tbl[101], 350.0f, 1e-6);

        /* Setting 4 (Map 5, +2 steps): +4.0% fuel, +5.0% heat, +1.5% torque at bin 0, +4.04% at bin 50 */
        ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_ENGINE_MAP, +2), 1);
        ASSERT_NEAR(esh_engine_map_fuel_scale(&st.params[ESH_PARAM_ENGINE_MAP], 0.020), 1.04, 1e-12);
        ASSERT_NEAR(esh_engine_map_heat_scale(&st.params[ESH_PARAM_ENGINE_MAP], 0.025), 1.05, 1e-12);
        esh_engine_map_scale_torque_table(base_tbl, live_tbl, 60, &st.params[ESH_PARAM_ENGINE_MAP], 0.0075, 0.000250);
        ASSERT_NEAR(live_tbl[0] + live_tbl[1], 300.0 * (1.0 + 2.0 * 0.0075), 1e-3);
        ASSERT_NEAR(live_tbl[100] + live_tbl[101],
                    300.0 * (1.0 + 2.0 * 0.0075) * (1.0 + 2.0 * 50.0 * 0.000250), 1e-3);

        /* Setting 0 (Map 1, -2 steps): -4.0% fuel, -5.0% heat, -1.5% torque at bin 0 */
        ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_ENGINE_MAP, -4), 1);
        ASSERT_NEAR(esh_engine_map_fuel_scale(&st.params[ESH_PARAM_ENGINE_MAP], 0.020), 0.96, 1e-12);
        ASSERT_NEAR(esh_engine_map_heat_scale(&st.params[ESH_PARAM_ENGINE_MAP], 0.025), 0.95, 1e-12);
        esh_engine_map_scale_torque_table(base_tbl, live_tbl, 60, &st.params[ESH_PARAM_ENGINE_MAP], 0.0075, 0.000250);
        ASSERT_NEAR(live_tbl[0] + live_tbl[1], 300.0 * (1.0 - 2.0 * 0.0075), 1e-3);
        ASSERT_NEAR(live_tbl[100] + live_tbl[101],
                    300.0 * (1.0 - 2.0 * 0.0075) * (1.0 - 2.0 * 50.0 * 0.000250), 1e-3);
    }

    /* Clamping & locked count <= 1 */
    ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_REV_LIMIT, +1), 0);
    ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_REV_LIMIT, -1), 1);
    esh_param_format_value(&st, ESH_PARAM_REV_LIMIT, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "17,900");
    ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_DIFF_PUMP, +1), 0);
    ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_ENG_BRAKE, -10), 1);
    ASSERT_EQ_I32(st.params[ESH_PARAM_ENG_BRAKE].setting, 0);
    ASSERT_EQ_I32(esh_param_adjust(&st, ESH_PARAM_ENG_BRAKE, -1), 0);

    /* 5-row scrolling window across 12 rows + wrap-around */
    {
        const int all_on[ESH_PARAM_COUNT] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        const int subset[ESH_PARAM_COUNT] = { 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0 };
        const int none[ESH_PARAM_COUNT] = { 0 };
        EshParamId sel_id = ESH_PARAM_REV_LIMIT;

        esh_menu_configure(&menu, all_on);
        esh_menu_reset(&menu, 1);
        ASSERT_EQ_I32(menu.active_count, 12);
        ASSERT_EQ_I32(esh_menu_visible_rows(&menu), 5);
        ASSERT_EQ_I32(menu.selected_row, 0);
        ASSERT_EQ_I32(menu.scroll_top, 0);

        esh_menu_move_selection(&menu, +4);
        ASSERT_EQ_I32(menu.selected_row, 4);
        ASSERT_EQ_I32(menu.scroll_top, 0);

        esh_menu_move_selection(&menu, +1);
        ASSERT_EQ_I32(menu.selected_row, 5);
        ASSERT_EQ_I32(menu.scroll_top, 1);

        esh_menu_move_selection(&menu, -1);
        ASSERT_EQ_I32(menu.selected_row, 4);
        ASSERT_EQ_I32(menu.scroll_top, 1);

        esh_menu_move_selection(&menu, -5);
        ASSERT_EQ_I32(menu.selected_row, 11);
        ASSERT_EQ_I32(menu.scroll_top, 7);

        esh_menu_move_selection(&menu, +1);
        ASSERT_EQ_I32(menu.selected_row, 0);
        ASSERT_EQ_I32(menu.scroll_top, 0);

        /* Subset filtering (3 parameters enabled) */
        esh_menu_configure(&menu, subset);
        esh_menu_reset(&menu, 1);
        ASSERT_EQ_I32(menu.active_count, 3);
        ASSERT_EQ_I32(esh_menu_visible_rows(&menu), 3);
        ASSERT_EQ_I32(esh_menu_selected_param_id(&menu, &sel_id), 1);
        ASSERT_EQ_I32((int32_t)sel_id, (int32_t)ESH_PARAM_REV_LIMIT);
        esh_menu_move_selection(&menu, +1);
        ASSERT_EQ_I32(esh_menu_selected_param_id(&menu, &sel_id), 1);
        ASSERT_EQ_I32((int32_t)sel_id, (int32_t)ESH_PARAM_BRAKE_BIAS);
        esh_menu_move_selection(&menu, +1);
        ASSERT_EQ_I32(esh_menu_selected_param_id(&menu, &sel_id), 1);
        ASSERT_EQ_I32((int32_t)sel_id, (int32_t)ESH_PARAM_DIFF_PUMP);
        ASSERT_EQ_I32(menu.scroll_top, 0);
        esh_menu_move_selection(&menu, +1);
        ASSERT_EQ_I32(menu.selected_row, 0);

        /* All disabled -> active_count == 0 */
        esh_menu_configure(&menu, none);
        ASSERT_EQ_I32(menu.active_count, 0);
        ASSERT_EQ_I32(esh_menu_visible_rows(&menu), 0);
        ASSERT_EQ_I32(esh_menu_selected_param_id(&menu, &sel_id), 0);
        esh_menu_move_selection(&menu, +1);
        ASSERT_EQ_I32(menu.selected_row, 0);
    }

    /* Mode cycling bitmask */
    ASSERT_EQ_I32(esh_compute_last_vanilla_cockpit_mode(0x00), 1);
    ASSERT_EQ_I32(esh_compute_last_vanilla_cockpit_mode(0x01), 2);
    ASSERT_EQ_I32(esh_compute_last_vanilla_cockpit_mode(0x02), 3);
    ASSERT_EQ_I32(esh_compute_last_vanilla_cockpit_mode(0x03), 3);
    ASSERT_EQ_I32(esh_compute_last_vanilla_cockpit_mode(0x04), 4);
    ASSERT_EQ_I32(esh_compute_last_vanilla_cockpit_mode(0x07), 4);

    /* AI Dynamic Engine Map & Extra Fueling */
    {
        EshAiContext ai;
        int s;
        memset(&ai, 0, sizeof ai);
        ai.map_count = 5;
        ai.engine_life_seconds = 1.0e38f;
        ai.gap_ahead_seconds = 2.0f;
        ai.gap_behind_seconds = 2.0f;
        ai.fuel_per_lap_liters = 3.0f;
        ai.stint_laps_remaining = 10.0f;
        ai.current_fuel_liters = 33.5f; /* +1.17 laps margin */
        ai.total_race_laps = 58;
        ai.next_pit_lap = 20;
        ai.completed_laps = 5;

        /* FP1..FP4: only Maps 1, 2, or 3 (settings 0, 1, 2) */
        for (s = 1; s <= 4; s++) {
            int seed;
            ai.session_id = s;
            for (seed = 0; seed < 8; seed++) {
                int32_t m;
                ai.slot_seed = seed;
                m = esh_ai_decide_engine_map(&ai);
                if (m < 0 || m > 2) {
                    fprintf(stderr, "FAIL: FP map out of 0..2: %d\n", (int)m);
                    return 1;
                }
            }
        }

        /* Qualifying: always Map 5 (setting 4) on track */
        ai.session_id = 5;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 4);

        /* Warm-Up & Test Day: Map 3 (setting 2) */
        ai.session_id = 0;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 2);
        ai.session_id = 6;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 2);

        /* Race rules */
        ai.session_id = 7;
        ai.total_stops = 2;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 2); /* normal Map 3 */

        ai.total_stops = 1;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 1); /* 1-stop -> Map 2 */

        ai.total_stops = 3;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 3); /* 3-stop -> Map 4 */

        /* Attacking/Defending close battle */
        ai.total_stops = 2;
        ai.slot_seed = 1;
        ai.gap_ahead_seconds = 0.45f;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 4); /* Map 5 */

        /* Not under threat -> lower map */
        ai.gap_ahead_seconds = 5.0f;
        ai.gap_behind_seconds = 5.0f;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 1); /* Map 2 */

        /* In-lap push */
        ai.gap_ahead_seconds = 2.0f;
        ai.gap_behind_seconds = 2.0f;
        ai.completed_laps = 19;
        ai.stint_laps_remaining = 0.5f;
        ai.current_fuel_liters = 5.0f; /* +1.17 laps */
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 4);

        /* Final 3 laps push */
        ai.completed_laps = 56;
        ai.next_pit_lap = 58;
        ai.stint_laps_remaining = 2.0f;
        ai.current_fuel_liters = 9.0f;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 4);

        /* Critical fuel shortage -> Map 1 */
        ai.completed_laps = 10;
        ai.next_pit_lap = 20;
        ai.stint_laps_remaining = 10.0f;
        ai.current_fuel_liters = 30.2f; /* +0.067 laps margin */
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 0);

        /* Low engine lifetime -> preserve engine */
        ai.current_fuel_liters = 35.0f;
        ai.gap_ahead_seconds = 0.4f; /* even in battle */
        ai.engine_life_seconds = 300.0f; /* < 360s -> capped to Map 2 */
        ai.est_race_seconds_left = 1000.0f;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 0); /* < 1.05 * race_left -> Map 1 */
        ai.est_race_seconds_left = 260.0f; /* 300s < 1.25 * 260s -> capped to Map 2 */
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 1);

        /* Yielding to teammate (driver orders) or lead-lap car -> lowest map */
        ai.engine_life_seconds = 1e31f;
        ai.gap_ahead_seconds = 999.0f;
        ai.gap_behind_seconds = 0.3f;
        ai.is_yielding = 1;
        ASSERT_EQ_I32(esh_ai_decide_engine_map(&ai), 0);
        ai.is_yielding = 0;

        /* Extra fuel buffer multipliers */
        ASSERT_NEAR(esh_ai_race_fuel_buffer_mult(1, 0.020), 1.010, 1e-12);
        ASSERT_NEAR(esh_ai_race_fuel_buffer_mult(2, 0.020), 1.020, 1e-12);
        ASSERT_NEAR(esh_ai_race_fuel_buffer_mult(3, 0.020), 1.035, 1e-12);
    }

    return 0;
}

int main(void)
{
    /* Undamaged -> Green */
    ASSERT_NEAR(esh_normalize_damage(1.0, 1.0, 0.25), 0.0, 1e-12);
    ASSERT_NEAR(esh_normalize_damage(1.0 + 1e-7, 1.0, 0.25), 0.0, 1e-12);
    ASSERT_NEAR(esh_normalize_damage(0.9, 1.0, 0.25), 0.0, 1e-12);
    ASSERT_NEAR(esh_normalize_damage(NAN, 1.0, 0.25), 0.0, 1e-12);

    ASSERT_EQ_U32(esh_damage_to_argb(0.0), 0xFF00FF00u);
    ASSERT_EQ_U32(esh_damage_to_argb(-0.1), 0xFF00FF00u);
    ASSERT_EQ_U32(esh_damage_to_argb(NAN), 0xFF00FF00u);

    /* Any slight damage > 0 -> Yellow immediately */
    ASSERT_EQ_U32(esh_damage_to_argb(0.001), 0xFFFFFF00u);

    /* Smooth Yellow -> Orange -> Red gradient */
    ASSERT_EQ_U32(esh_damage_to_argb(0.50), 0xFFFF8000u);
    ASSERT_EQ_U32(esh_damage_to_argb(0.75), 0xFFFF4000u);

    /* Max damage & over-cap -> Pure Red */
    ASSERT_EQ_U32(esh_damage_to_argb(1.0), 0xFFFF0000u);
    ASSERT_EQ_U32(esh_damage_to_argb(1.5), 0xFFFF0000u);

    /* Component max caps */
    ASSERT_NEAR(esh_pair_damage_norm(0.125, 0.0, 0.25, 0.02, 0.0, 0.20), 0.5, 1e-12);
    ASSERT_NEAR(esh_pair_damage_norm(0.05, 0.0, 0.25, 0.20, 0.0, 0.20), 1.0, 1e-12);
    ASSERT_NEAR(esh_normalize_damage(0.20, 0.0, 0.40), 0.5, 1e-12);
    ASSERT_NEAR(esh_normalize_damage(0.45, 0.0, 0.45), 1.0, 1e-12);

    /* Suspension bend normalization (default 5 deg cap) */
    ASSERT_EQ_U32(esh_damage_to_argb(esh_suspension_damage_norm(0.0, 5.0)), 0xFF00FF00u);
    ASSERT_EQ_U32(esh_damage_to_argb(esh_suspension_damage_norm(esh_deg_to_rad(0.005), 5.0)), 0xFFFFFF00u);
    ASSERT_EQ_U32(esh_damage_to_argb(esh_suspension_damage_norm(esh_deg_to_rad(2.5), 5.0)), 0xFFFF8000u);
    ASSERT_EQ_U32(esh_damage_to_argb(esh_suspension_damage_norm(esh_deg_to_rad(5.0), 5.0)), 0xFFFF0000u);
    ASSERT_EQ_U32(esh_damage_to_argb(esh_suspension_damage_norm(esh_deg_to_rad(5.0), 10.0)), 0xFFFF8000u);

    if (test_midrace_setup() != 0)
        return 1;

    printf("All damage color and mid-race setup tests passed.\n");
    return 0;
}
