/*
 * expanded_setup_and_hud 1.0.0 (Lightloader plug-in)
 *
 * Phase 1: Car Status LCD damage color tuning (0x0049DE40)
 *   - Undamaged (delta == 0) -> Green (0xFF00FF00)
 *   - Any slight damage (> 0) -> Yellow (0xFFFFFF00) -> Orange -> Red
 * (0xFFFF0000)
 *   - Detached front/rear wings keep vanilla 1 Hz blinking Red.
 *
 * Entry: Lightloader protected host export rfe_1d3b7a91. DllMain is inert.
 */
#include "esh_damage_color.h"
#include "esh_midrace_setup.h"
#include "protected_plugin_abi.h"
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define ESH_MOD_ID "expanded_setup_and_hud"
#define ESH_INI_NAME "expanded_setup_and_hud.ini"
#define ESH_LOG_NAME "expanded_setup_and_hud_log.txt"
#define ESH_LOG_PREV "expanded_setup_and_hud_log_prev.txt"
#define ESH_LOG_MAX_BYTES (256L * 1024L)
#define ESH_PATH_MAX 1024

/* Verified addresses in F1 Challenge Online.exe (ImageBase 0x00400000). */
#define ADDR_APPLY_SETUP 0x0043C250u
#define STEAL_APPLY_SETUP 6u
#define ADDR_RENDER_WHEEL_LCD 0x0049D5D0u
#define STEAL_RENDER_WHEEL_LCD 8u
#define ADDR_RENDER_CAR_STATUS 0x0049DE40u
#define STEAL_RENDER_CAR_STATUS 8u
#define ADDR_SET_LCD_MODE 0x0049F150u
#define ADDR_LCD_DRAW_TEXT 0x0049F6A0u
#define ADDR_POLL_LCD_INPUT 0x0049F790u
#define STEAL_POLL_LCD_INPUT 7u
#define ADDR_BLIT_BMP 0x005432F0u
#define ADDR_FILL_RECT 0x00543440u
#define ADDR_FONT_EXTENTS 0x005531F0u
#define ADDR_PARSE_RANGE_SETTING 0x0042E1F0u
#define ADDR_PARSE_ENGINE_SETTINGS 0x004F3C10u
#define STEAL_PARSE_ENGINE_SETTINGS 5u

/* AI Engine Map & Strategy hooks (verified in F1 Challenge Online.exe). */
#define ADDR_AI_PLAN_STRATEGY  0x00429960u
#define STEAL_AI_PLAN_STRATEGY 7u
#define ADDR_AI_INIT_VEHICLE   0x004E5F60u
#define STEAL_AI_INIT_VEHICLE  6u
#define ADDR_AI_FUEL_WEAR      0x004ED960u
#define STEAL_AI_FUEL_WEAR     10u

#define ADDR_NUM_VEHICLES      0x005C329Cu
#define ADDR_SESSION_ID        0x007C56A4u
#define ADDR_SESSION_PHASE     0x007C56A8u
#define ADDR_TOTAL_RACE_LAPS   0x008ACF8Cu

#define ESH_MAX_AI_SLOTS       32
#define OFF_VEH_CTRL_TYPE      0x32C8u
#define OFF_VEH_POS_X          0x34E0u
#define OFF_VEH_POS_Y          0x34E4u
#define OFF_VEH_POS_Z          0x34E8u
#define OFF_VEH_VEL_X          0x34F8u
#define OFF_VEH_VEL_Y          0x34FCu
#define OFF_VEH_VEL_Z          0x3500u
#define OFF_VEH_DRY_MASS       0x351Cu
#define OFF_VEH_MASS           0x3520u
#define OFF_VEH_INV_MASS       0x3524u
#define OFF_VEH_FUEL_CUR       0x3984u
#define OFF_VEH_FUEL_MAX       0x3988u
#define OFF_VEH_FUEL_RATE      0x3990u
#define OFF_VEH_TORQUE_TBL     0x39B0u
#define OFF_VEH_TORQUE_BINS    0x39BCu
#define OFF_VEH_IN_PIT_LANE    0x39FAu
#define OFF_VEH_ENG_SMOKE      0x3D54u
#define OFF_VEH_ENG_LIFE       0x3D70u
#define OFF_VEH_LAPS_DONE      0x403Cu
#define OFF_VEH_SECTOR         0x4040u
#define OFF_VEH_RACE_POS       0x4060u
#define OFF_VEH_TOTAL_STOPS    0x4588u
#define OFF_VEH_TEAMMATE_PTR   0x45F8u
#define OFF_VEH_YIELD_TARGET   0x45FCu
#define OFF_VEH_DRIVER_ORDER   0x48A8u
#define OFF_VEH_STINT_PIT_LAP0 0x4590u
#define OFF_VEH_STINT_IDX      0x45E0u
#define OFF_VEH_FUEL_PER_LAP   0x45E8u
#define OFF_VEH_INIT_FUEL      0x45F0u

#define ADDR_CAMERA_PTR_A 0x005EA518u
#define ADDR_CAMERA_PTR_B 0x005EA528u
#define OFF_CAMERA_MODE 0x0034u
#define ADDR_GARAGE_SETUP_BUF 0x007C56C0u
#define ADDR_PLAYER_VEH_PTR 0x007C54E8u
#define ADDR_VIEWED_VEH_PTR 0x007C54E0u
#define ADDR_VIEWED_SLOT 0x007C54DCu
#define ADDR_PLAYER_SLOT 0x007C54E4u
#define ADDR_VEH_PTR_ARRAY 0x007C54F0u
#define ADDR_SESSION_TIME 0x008AFEA0u

/* LCD state & input latches. */
#define ADDR_LCD_MODE 0x007CE048u
#define ADDR_LCD_DISPLAY_MASK 0x007CF7ACu
#define ADDR_KEY_LCD_MODE 0x007CC234u
#define ADDR_KEY_LCD_UP 0x007CC28Cu
#define ADDR_KEY_LCD_DOWN 0x007CC2B8u
#define ADDR_KEY_LCD_INC 0x007CC2E4u
#define ADDR_KEY_LCD_DEC 0x007CC310u

/* Live physics targets on 0x007C7428 (from FUN_0043c250). */
#define ADDR_PHYS_TC_GRIP_MULT     0x007C7578u
#define ADDR_PHYS_TC_GRIP_ADD      0x007C7580u
#define ADDR_PHYS_ABS_GRIP_MULT    0x007C75A4u
#define ADDR_PHYS_ABS_GRIP_ADD     0x007C75ACu
#define ADDR_PHYS_FUEL_CONSUMPTION 0x007C75D4u
#define ADDR_PHYS_BRAKE_BIAS_VAL   0x007C898Cu
#define ADDR_PHYS_BRAKE_BIAS_SET   0x007C89A8u
#define ADDR_PHYS_ENG_BRAKE        0x007C9078u
#define ADDR_PHYS_REV_SOFT         0x007C90BCu
#define ADDR_PHYS_REV_LIMIT        0x007C9104u
#define ADDR_PHYS_REV_LOW          0x007C910Cu
#define ADDR_PHYS_REV_HIGH         0x007C9114u
#define ADDR_PHYS_TORQUE_TABLE_PTR 0x007C9124u
#define ADDR_PHYS_TORQUE_BIN_COUNT 0x007C9130u
#define ADDR_PHYS_COMBUSTION_HEAT  0x007C9184u
#define ADDR_PHYS_ENG_SPEED_HEAT   0x007C918Cu
#define ADDR_PHYS_DIFF_PUMP        0x007C9240u
#define ADDR_PHYS_DIFF_POWER       0x007C9248u
#define ADDR_PHYS_DIFF_COAST       0x007C9250u
#define ADDR_PHYS_DIFF_PRELOAD     0x007C9258u
#define OFF_VEH_REV_LIMIT_F32      0x39A4u

/* Player physics object (0x007C7428) aero damage accumulators & baselines. */
#define ADDR_PHYS_BASE 0x007C7428u
#define OFF_PHYS_WHEEL_ARRAY 0x028Cu
#define STRIDE_PHYS_WHEEL 0x0464u
#define OFF_WHEEL_PHYS_BEND_RAD 0x01E0u

#define ADDR_FW_DRAG_CUR 0x007C8BD0u
#define ADDR_FW_LIFT_CUR 0x007C8BE8u
#define ADDR_RW_DRAG_CUR 0x007C8C00u
#define ADDR_RW_LIFT_CUR 0x007C8C18u
#define ADDR_DIFF_DRAG_CUR 0x007C8C30u
#define ADDR_DIFF_LIFT_CUR 0x007C8C40u
#define ADDR_BODY_DRAG_CUR 0x007C8FE8u

#define ADDR_FW_DRAG_BASE 0x007C9030u
#define ADDR_FW_LIFT_BASE 0x007C9038u
#define ADDR_DIFF_DRAG_BASE 0x007C9048u
#define ADDR_DIFF_LIFT_BASE 0x007C9050u
#define ADDR_BODY_DRAG_BASE 0x007C9058u
#define ADDR_RW_DRAG_BASE 0x007C9068u
#define ADDR_RW_LIFT_BASE 0x007C9070u

/* Vehicle struct offsets. */
#define OFF_VEH_WHEEL_ARRAY 0x0834u
#define STRIDE_VEH_WHEEL 0x0128u
#define OFF_VEH_WHEEL_INTACT 0x0040u
#define OFF_VEH_FW_REC_PTR 0x0CD4u
#define OFF_VEH_FW_INTACT 0x0D14u
#define OFF_VEH_RW_REC_PTR 0x0D18u
#define OFF_VEH_RW_INTACT 0x0D58u
#define OFF_VEH_AERO_CUR 0x3A78u
#define OFF_VEH_AERO_BASE 0x3A84u
#define OFF_VEH_SCENE_SLOT_ID 0x4894u

/* Max damage caps from 0x0043E890..0x0043EAE0 and 0x004ED0F0. */
#define MAX_WING_DRAG_DELTA 0.25
#define MAX_WING_LIFT_DELTA 0.20
#define MAX_DIFF_DRAG_DELTA 0.25
#define MAX_DIFF_LIFT_DELTA 0.20
#define MAX_BODY_DRAG_DELTA 0.40
#define MAX_VEH_AERO_DELTA 0.45

/* Car Status LCD rectangle coordinates (matching 0x0049DE40 and LCD01.BMP). */
#define FW_RECT_X 0xC3
#define FW_RECT_Y 0x1A
#define FW_RECT_W 0x12
#define FW_RECT_H 0x35

#define RW_RECT_X 0x2A
#define RW_RECT_Y 0x23
#define RW_RECT_W 0x0E
#define RW_RECT_H 0x23

#define DIFF_RECT_X 0x57
#define DIFF_RECT_Y 0x2D
#define DIFF_RECT_W 0x10
#define DIFF_RECT_H 0x0D

#define SIDEPOD_L_X 80
#define SIDEPOD_L_Y 28
#define SIDEPOD_R_X 80
#define SIDEPOD_R_Y 64
#define SIDEPOD_W 44
#define SIDEPOD_H 12

typedef struct {
  int x;
  int y;
  int w;
  int h;
} esh_rect_t;

static const esh_rect_t kSuspStrutRects[4] = {
    {173, 31, 7, 16}, /* 0: FL */
    {173, 57, 7, 17}, /* 1: FR */
    {59, 31, 7, 15},  /* 2: RL */
    {59, 58, 7, 16}   /* 3: RR */
};

typedef void(__attribute__((thiscall)) *
             esh_render_car_status_fn)(void *this_lcd, int display_off);
typedef void(__attribute__((thiscall)) * esh_fill_rect_fn)(void *surface, int x,
                                                           int y, int w, int h,
                                                           uint32_t argb);
typedef void(__attribute__((thiscall)) * esh_apply_setup_fn)(void *this_phys,
                                                             void *setup_struct,
                                                             void *hdv_struct);
typedef void(__attribute__((thiscall)) *
             esh_poll_lcd_input_fn)(void *this_pit_lcd);
typedef void(__attribute__((thiscall)) *
             esh_render_wheel_lcd_fn)(void *this_lcd, int display_off);
typedef void(__attribute__((thiscall)) *
             esh_set_lcd_mode_fn)(void *this_pit_lcd, int mode);
typedef void(__attribute__((thiscall)) *
             esh_lcd_draw_text_fn)(void *this_pit_lcd, const char *str, int x,
                                   int y, void *font, void *surface, int align);
typedef void(__attribute__((thiscall)) *
             esh_blit_bmp_fn)(void *bmp, int x, int y, void *dst_surface,
                              int sx, int sy, float scale_x, float scale_y,
                              int a8, int a9);
typedef void(__attribute__((thiscall)) *
             esh_font_extents_fn)(void *font, const char *str, int *out_w,
                                  int *out_h, int use_scale);
typedef void(__attribute__((thiscall)) *
             esh_ai_init_vehicle_fn)(void *this_veh_body, void *setup_struct,
                                     void *hdv_struct);
typedef void(__attribute__((thiscall)) *
             esh_ai_plan_strategy_fn)(void *this_veh);
typedef void(__attribute__((thiscall)) *
             esh_ai_fuel_wear_fn)(void *this_veh_body, float dt);
typedef void(__attribute__((thiscall)) *
             esh_parse_range_setting_fn)(void *this_param, void *parser,
                                         const char *range_key,
                                         const char *setting_key,
                                         uint32_t scale_lo, uint32_t scale_hi);
typedef void(__attribute__((stdcall)) *
             esh_parse_engine_settings_fn)(void *parser, uint8_t *setup_struct);

static const uint8_t kSigRenderCarStatus[STEAL_RENDER_CAR_STATUS] = {
    0x83, 0xEC, 0x10, 0x53, 0x55, 0x56, 0x8B, 0xF1};
static const uint8_t kSigFillRect[7] = {0x56, 0x8B, 0xF1, 0x8D,
                                        0x44, 0x24, 0x14};
static const uint8_t kSigApplySetup[STEAL_APPLY_SETUP] = {0x55, 0x8B, 0xEC,
                                                          0x83, 0xE4, 0xF8};
static const uint8_t kSigPollLcdInput[STEAL_POLL_LCD_INPUT] = {
    0xA1, 0x34, 0xC2, 0x7C, 0x00, 0x85, 0xC0};
static const uint8_t kSigRenderWheelLcd[STEAL_RENDER_WHEEL_LCD] = {
    0xA1, 0x48, 0xE0, 0x7C, 0x00, 0x83, 0xF8, 0x04};
static const uint8_t kSigAiInitVehicle[STEAL_AI_INIT_VEHICLE] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
static const uint8_t kSigAiPlanStrategy[STEAL_AI_PLAN_STRATEGY] = {
    0x83, 0xEC, 0x28, 0x56, 0x8B, 0xF1, 0x57};
static const uint8_t kSigAiFuelWear[STEAL_AI_FUEL_WEAR] = {
    0x55, 0x56, 0x8B, 0xF1, 0xD9, 0x86, 0x78, 0x39, 0x00, 0x00};
static const uint8_t kSigParseEngineSettings[STEAL_PARSE_ENGINE_SETTINGS] = {
    0x56, 0x8B, 0x74, 0x24, 0x08};

static char g_inipath[ESH_PATH_MAX];
static char g_logpath[ESH_PATH_MAX];
static CRITICAL_SECTION g_logcs;
static long g_logbytes;
static int g_logfull;

static int g_enabled = 1;
static int g_car_status_colors = 1;
static int g_susp_max_deg = 5;
static int g_midrace_setup_lcd = 1;
static int g_ai_dynamic_engine_map = 1;
static double g_engmap_fuel_step = 0.020;
static double g_engmap_heat_step = 0.025;
static double g_engmap_boost_torque = 0.0075;
static double g_engmap_boost_power = 0.000250;

static esh_render_car_status_fn g_orig_render_car_status = NULL;
static esh_fill_rect_fn g_fill_rect = NULL;
static esh_apply_setup_fn g_orig_apply_setup = NULL;
static esh_poll_lcd_input_fn g_orig_poll_lcd_input = NULL;
static esh_render_wheel_lcd_fn g_orig_render_wheel_lcd = NULL;
static esh_set_lcd_mode_fn g_set_lcd_mode = NULL;
static esh_lcd_draw_text_fn g_lcd_draw_text = NULL;
static esh_blit_bmp_fn g_blit_bmp = NULL;
static esh_font_extents_fn g_font_extents = NULL;
static esh_ai_init_vehicle_fn g_orig_ai_init_vehicle = NULL;
static esh_ai_plan_strategy_fn g_orig_ai_plan_strategy = NULL;
static esh_ai_fuel_wear_fn g_orig_ai_fuel_wear = NULL;
static esh_parse_range_setting_fn g_parse_range_setting = NULL;
static esh_parse_engine_settings_fn g_orig_parse_engine_settings = NULL;

typedef struct {
  const void *veh_ptr;
  float base_fuel_rate;
  float base_torque_table[ESH_MAX_TORQUE_BINS * 2];
  int32_t torque_bin_count;
  EshParamSlot map_slot;
  double next_eval_time;
} EshAiVehicleState;

static EshAiVehicleState g_ai_states[ESH_MAX_AI_SLOTS];

static EshSetupState g_esh_setup;
static EshMenuState g_esh_menu;
static volatile LONG g_loader_init_state = 0;

static int esh_path_cat(char *out, size_t cap, const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  if (!cap)
    return 0;
  if (la + lb + 1 > cap) {
    out[0] = '\0';
    return 0;
  }
  memcpy(out, a, la);
  memcpy(out + la, b, lb + 1);
  return 1;
}

static void logf_(const char *fmt, ...) {
  char line[512];
  int n;
  va_list ap;
  if (!g_logpath[0])
    return;
  EnterCriticalSection(&g_logcs);
  if (!g_logfull) {
    FILE *f;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
      n = 0;
    if (n > (int)sizeof line - 2)
      n = (int)sizeof line - 2;
    line[n] = '\0';
    if (g_logbytes + n + 1 > ESH_LOG_MAX_BYTES) {
      g_logfull = 1;
      strcpy(line, "log size limit reached; further lines suppressed");
      n = (int)strlen(line);
    }
    f = fopen(g_logpath, "a");
    if (f) {
      fputs(line, f);
      fputc('\n', f);
      fclose(f);
      g_logbytes += n + 1;
    }
  }
  LeaveCriticalSection(&g_logcs);
}

static int ini_flag(const char *key, int def) {
  if (!g_inipath[0])
    return def;
  return GetPrivateProfileIntA("ExpandedSetupAndHud", key, def, g_inipath) != 0
             ? 1
             : 0;
}

static int ini_int(const char *key, int def, int min_v, int max_v) {
  int v;
  if (!g_inipath[0])
    return def;
  v = (int)GetPrivateProfileIntA("ExpandedSetupAndHud", key, def, g_inipath);
  if (v < min_v)
    return min_v;
  if (v > max_v)
    return max_v;
  return v;
}

static double ini_double(const char *key, double def) {
  char buf[64];
  char *endp = NULL;
  double v;
  if (!g_inipath[0])
    return def;
  GetPrivateProfileStringA("ExpandedSetupAndHud", key, "", buf, sizeof buf,
                           g_inipath);
  if (!buf[0])
    return def;
  v = strtod(buf, &endp);
  if (endp == buf)
    return def;
  return v;
}

static void draw_bordered_rect(void *surface, int x, int y, int w, int h,
                               uint32_t fill_argb) {
  g_fill_rect(surface, x, y, w, h, ESH_COLOR_BLACK);
  if (w > 2 && h > 2)
    g_fill_rect(surface, x + 1, y + 1, w - 2, h - 2, fill_argb);
}

static int viewed_vehicle_is_player(void) {
  int viewed_slot = *(const volatile int *)ADDR_VIEWED_SLOT;
  int player_slot = *(const volatile int *)ADDR_PLAYER_SLOT;
  const uint8_t *const *veh_arr = (const uint8_t *const *)ADDR_VEH_PTR_ARRAY;
  const uint8_t *viewed_veh;
  const uint8_t *player_veh;
  if (viewed_slot < 0 || player_slot < 0)
    return 0;
  viewed_veh = veh_arr[viewed_slot];
  player_veh = veh_arr[player_slot];
  if (!viewed_veh || !player_veh)
    return 0;
  return *(const int *)(viewed_veh + OFF_VEH_SCENE_SLOT_ID) ==
         *(const int *)(player_veh + OFF_VEH_SCENE_SLOT_ID);
}

static double rd_f64(uintptr_t addr) { return *(const volatile double *)addr; }

static void wr_f64(uintptr_t addr, double v) { *(volatile double *)addr = v; }

static void esh_snapshot_engine_baselines(void) {
  const float *tbl =
      *(const float *const *)(uintptr_t)ADDR_PHYS_TORQUE_TABLE_PTR;
  int32_t bins = *(const volatile int32_t *)(uintptr_t)ADDR_PHYS_TORQUE_BIN_COUNT;
  g_esh_setup.base_fuel_consumption = rd_f64(ADDR_PHYS_FUEL_CONSUMPTION);
  g_esh_setup.base_combustion_heat = rd_f64(ADDR_PHYS_COMBUSTION_HEAT);
  g_esh_setup.base_engine_speed_heat = rd_f64(ADDR_PHYS_ENG_SPEED_HEAT);
  if (tbl && bins > 0) {
    if (bins > ESH_MAX_TORQUE_BINS)
      bins = ESH_MAX_TORQUE_BINS;
    memcpy(g_esh_setup.base_torque_table, tbl, (size_t)bins * 2u * sizeof(float));
    g_esh_setup.torque_bin_count = bins;
  } else {
    g_esh_setup.torque_bin_count = 0;
  }
}

static void esh_sync_live_brake_bias(void) {
  EshParamSlot *bb = &g_esh_setup.params[ESH_PARAM_BRAKE_BIAS];
  int32_t live_set =
      *(const volatile int32_t *)(uintptr_t)ADDR_PHYS_BRAKE_BIAS_SET;
  if (bb->count > 0 && live_set >= 0 && live_set < bb->count)
    bb->setting = live_set;
}

static void esh_setup_ensure_initialized(void) {
  const uint8_t *garage_buf;
  int32_t rev_count;
  int i;
  if (g_esh_menu.initialized)
    return;

  garage_buf = (const uint8_t *)(uintptr_t)ADDR_GARAGE_SETUP_BUF;
  rev_count =
      *(const int32_t *)(garage_buf +
                         esh_setup_block_offset(ESH_IDX_REV_LIMIT) + 0x10);
  if (rev_count > 0 && rev_count <= 500) {
    for (i = 0; i < ESH_PARAM_COUNT; i++) {
      uint32_t idx = esh_param_meta((EshParamId)i)->setup_index;
      if (idx != ESH_IDX_SYNTHETIC) {
        const uint8_t *blk = garage_buf + esh_setup_block_offset(idx);
        g_esh_setup.params[i].base = *(const double *)(blk + 0x00);
        g_esh_setup.params[i].step = *(const double *)(blk + 0x08);
        g_esh_setup.params[i].count = *(const int32_t *)(blk + 0x10);
        g_esh_setup.params[i].setting = *(const int32_t *)(blk + 0x14);
        esh_clamp_slot(&g_esh_setup.params[i]);
      }
    }
    g_esh_setup.rev_limit_logic =
        rd_f64(ADDR_PHYS_REV_HIGH) - rd_f64(ADDR_PHYS_REV_LOW);
    {
      double pump_frac =
          esh_param_raw_value(&g_esh_setup.params[ESH_PARAM_DIFF_PUMP]);
      g_esh_setup.diff_pump_torque =
          (pump_frac > 1e-9) ? (rd_f64(ADDR_PHYS_DIFF_PUMP) / pump_frac) : 0.0;
    }
  } else {
    g_esh_setup.params[ESH_PARAM_REV_LIMIT].base = rd_f64(ADDR_PHYS_REV_LIMIT);
    g_esh_setup.params[ESH_PARAM_ENGINE_MAP].base = 0.0;
    g_esh_setup.params[ESH_PARAM_ENG_BRAKE].base =
        rd_f64(ADDR_PHYS_ENG_BRAKE) / ESH_ENGBRAKE_MULT;
    g_esh_setup.params[ESH_PARAM_BRAKE_BIAS].base =
        rd_f64(ADDR_PHYS_BRAKE_BIAS_VAL);
    g_esh_setup.params[ESH_PARAM_DIFF_PRELOAD].base =
        rd_f64(ADDR_PHYS_DIFF_PRELOAD);
    g_esh_setup.params[ESH_PARAM_DIFF_POWER].base =
        rd_f64(ADDR_PHYS_DIFF_POWER);
    g_esh_setup.params[ESH_PARAM_DIFF_COAST].base =
        rd_f64(ADDR_PHYS_DIFF_COAST);
    g_esh_setup.params[ESH_PARAM_DIFF_PUMP].base = 0.0;
    for (i = 0; i <= (int)ESH_PARAM_DIFF_PUMP; i++) {
      g_esh_setup.params[i].step = 0.0;
      g_esh_setup.params[i].count = 1;
      g_esh_setup.params[i].setting = 0;
    }
    g_esh_setup.rev_limit_logic =
        rd_f64(ADDR_PHYS_REV_HIGH) - rd_f64(ADDR_PHYS_REV_LOW);
    g_esh_setup.diff_pump_torque = rd_f64(ADDR_PHYS_DIFF_PUMP);
  }
  esh_init_synthetic_slot(&g_esh_setup.params[ESH_PARAM_TC_GRIP_MULT],
                          rd_f64(ADDR_PHYS_TC_GRIP_MULT), 0.02, 20);
  esh_init_synthetic_slot(&g_esh_setup.params[ESH_PARAM_TC_GRIP_ADD],
                          rd_f64(ADDR_PHYS_TC_GRIP_ADD), 0.01, 20);
  esh_init_synthetic_slot(&g_esh_setup.params[ESH_PARAM_ABS_GRIP_MULT],
                          rd_f64(ADDR_PHYS_ABS_GRIP_MULT), 0.02, 20);
  esh_init_synthetic_slot(&g_esh_setup.params[ESH_PARAM_ABS_GRIP_ADD],
                          rd_f64(ADDR_PHYS_ABS_GRIP_ADD), 0.01, 20);
  esh_snapshot_engine_baselines();
  esh_sync_live_brake_bias();
  esh_menu_reset(&g_esh_menu, 1);
}

static void esh_apply_param_to_live_physics(EshParamId id) {
  double raw;
  const EshParamSlot *slot;
  if ((int)id < 0 || (int)id >= ESH_PARAM_COUNT)
    return;
  slot = &g_esh_setup.params[(int)id];
  raw = esh_param_raw_value(slot);

  switch (id) {
  case ESH_PARAM_REV_LIMIT: {
    double logic = g_esh_setup.rev_limit_logic;
    uint8_t *player_veh = *(uint8_t *const *)(uintptr_t)ADDR_PLAYER_VEH_PTR;
    wr_f64(ADDR_PHYS_REV_LIMIT, raw);
    wr_f64(ADDR_PHYS_REV_LOW, raw - 0.5 * logic);
    wr_f64(ADDR_PHYS_REV_HIGH, raw + 0.5 * logic);
    wr_f64(ADDR_PHYS_REV_SOFT, raw * 0.975);
    if (player_veh)
      *(volatile float *)(player_veh + OFF_VEH_REV_LIMIT_F32) = (float)raw;
    break;
  }
  case ESH_PARAM_ENGINE_MAP: {
    float *live_tbl = *(float *const *)(uintptr_t)ADDR_PHYS_TORQUE_TABLE_PTR;
    int32_t live_bins =
        *(const volatile int32_t *)(uintptr_t)ADDR_PHYS_TORQUE_BIN_COUNT;
    int32_t bins = g_esh_setup.torque_bin_count;
    if (g_esh_setup.base_fuel_consumption > 0.0) {
      double scale = esh_engine_map_fuel_scale(slot, g_engmap_fuel_step);
      wr_f64(ADDR_PHYS_FUEL_CONSUMPTION,
             g_esh_setup.base_fuel_consumption * scale);
    }
    {
      double heat_scale = esh_engine_map_heat_scale(slot, g_engmap_heat_step);
      if (g_esh_setup.base_combustion_heat > 0.0)
        wr_f64(ADDR_PHYS_COMBUSTION_HEAT,
               g_esh_setup.base_combustion_heat * heat_scale);
      if (g_esh_setup.base_engine_speed_heat > 0.0)
        wr_f64(ADDR_PHYS_ENG_SPEED_HEAT,
               g_esh_setup.base_engine_speed_heat * heat_scale);
    }
    if (live_tbl && bins > 0 && live_bins >= bins) {
      esh_engine_map_scale_torque_table(
          g_esh_setup.base_torque_table, live_tbl, bins, slot,
          g_engmap_boost_torque, g_engmap_boost_power);
    }
    break;
  }
  case ESH_PARAM_ENG_BRAKE:
    wr_f64(ADDR_PHYS_ENG_BRAKE, raw * ESH_ENGBRAKE_MULT);
    break;
  case ESH_PARAM_BRAKE_BIAS:
    *(volatile int32_t *)(uintptr_t)ADDR_PHYS_BRAKE_BIAS_SET = slot->setting;
    wr_f64(ADDR_PHYS_BRAKE_BIAS_VAL, raw);
    break;
  case ESH_PARAM_DIFF_PRELOAD:
    wr_f64(ADDR_PHYS_DIFF_PRELOAD, raw);
    break;
  case ESH_PARAM_DIFF_POWER:
    wr_f64(ADDR_PHYS_DIFF_POWER, raw);
    break;
  case ESH_PARAM_DIFF_COAST:
    wr_f64(ADDR_PHYS_DIFF_COAST, raw);
    break;
  case ESH_PARAM_DIFF_PUMP:
    wr_f64(ADDR_PHYS_DIFF_PUMP, raw * g_esh_setup.diff_pump_torque);
    break;
  case ESH_PARAM_TC_GRIP_MULT:
    wr_f64(ADDR_PHYS_TC_GRIP_MULT, esh_param_nonneg_value(slot));
    break;
  case ESH_PARAM_TC_GRIP_ADD:
    wr_f64(ADDR_PHYS_TC_GRIP_ADD, esh_param_nonneg_value(slot));
    break;
  case ESH_PARAM_ABS_GRIP_MULT:
    wr_f64(ADDR_PHYS_ABS_GRIP_MULT, esh_param_nonneg_value(slot));
    break;
  case ESH_PARAM_ABS_GRIP_ADD:
    wr_f64(ADDR_PHYS_ABS_GRIP_ADD, esh_param_nonneg_value(slot));
    break;
  default:
    break;
  }
}

static void __fastcall esh_apply_setup_hook(void *this_phys, void *edx_unused,
                                            void *setup_struct,
                                            void *hdv_struct) {
  (void)edx_unused;
  g_orig_apply_setup(this_phys, setup_struct, hdv_struct);
  if (this_phys == (void *)(uintptr_t)ADDR_PHYS_BASE && setup_struct &&
      hdv_struct) {
    esh_setup_snapshot(&g_esh_setup, setup_struct, hdv_struct);
    esh_snapshot_engine_baselines();
    esh_sync_live_brake_bias();
    esh_menu_reset(&g_esh_menu, 1);
    esh_apply_param_to_live_physics(ESH_PARAM_ENGINE_MAP);
  }
}

static int is_cockpit_camera_active(void) {
  const uint8_t *cam_a = *(const uint8_t *const *)(uintptr_t)ADDR_CAMERA_PTR_A;
  const uint8_t *cam_b = *(const uint8_t *const *)(uintptr_t)ADDR_CAMERA_PTR_B;
  int cam_mode;
  if (!cam_b || cam_a != cam_b)
    return 0;
  cam_mode = *(const volatile int *)(cam_b + OFF_CAMERA_MODE);
  return (cam_mode == 1 || cam_mode == 2);
}

static void __fastcall esh_poll_lcd_input_hook(void *this_pit_lcd,
                                               void *edx_unused) {
  int in_cockpit = is_cockpit_camera_active();
  int cur_mode = *(volatile int *)(uintptr_t)ADDR_LCD_MODE;
  (void)edx_unused;

  if (!in_cockpit && cur_mode == 5) {
    g_set_lcd_mode(this_pit_lcd, 0);
    cur_mode = 0;
  }

  if (*(volatile int *)(uintptr_t)ADDR_KEY_LCD_MODE != 0 && in_cockpit &&
      g_esh_menu.active_count > 0) {
    int last_vanilla = esh_compute_last_vanilla_cockpit_mode(
        *(const volatile uint8_t *)(uintptr_t)ADDR_LCD_DISPLAY_MASK);
    if (cur_mode == last_vanilla) {
      g_set_lcd_mode(this_pit_lcd, 5);
      *(volatile int *)(uintptr_t)ADDR_KEY_LCD_MODE = 0;
      return;
    }
    if (cur_mode == 5) {
      g_set_lcd_mode(this_pit_lcd, 0);
      *(volatile int *)(uintptr_t)ADDR_KEY_LCD_MODE = 0;
      return;
    }
  }

  if (cur_mode == 5 && g_esh_menu.active_count > 0) {
    int dirty = 0;
    EshParamId sel_id;
    esh_setup_ensure_initialized();
    esh_sync_live_brake_bias();
    if (*(volatile int *)(uintptr_t)ADDR_KEY_LCD_UP != 0) {
      *(volatile int *)(uintptr_t)ADDR_KEY_LCD_UP = 0;
      esh_menu_move_selection(&g_esh_menu, -1);
      dirty = 1;
    }
    if (*(volatile int *)(uintptr_t)ADDR_KEY_LCD_DOWN != 0) {
      *(volatile int *)(uintptr_t)ADDR_KEY_LCD_DOWN = 0;
      esh_menu_move_selection(&g_esh_menu, +1);
      dirty = 1;
    }
    if (*(volatile int *)(uintptr_t)ADDR_KEY_LCD_INC != 0) {
      *(volatile int *)(uintptr_t)ADDR_KEY_LCD_INC = 0;
      if (esh_menu_selected_param_id(&g_esh_menu, &sel_id) &&
          esh_param_adjust(&g_esh_setup, sel_id, +1))
        esh_apply_param_to_live_physics(sel_id);
      dirty = 1;
    }
    if (*(volatile int *)(uintptr_t)ADDR_KEY_LCD_DEC != 0) {
      *(volatile int *)(uintptr_t)ADDR_KEY_LCD_DEC = 0;
      if (esh_menu_selected_param_id(&g_esh_menu, &sel_id) &&
          esh_param_adjust(&g_esh_setup, sel_id, -1))
        esh_apply_param_to_live_physics(sel_id);
      dirty = 1;
    }
    if (dirty)
      *(volatile uint8_t *)((uint8_t *)this_pit_lcd + 0x0C) = 1;
    return;
  }

  g_orig_poll_lcd_input(this_pit_lcd);
}

static void draw_selection_box(void *surface, int x, int y, int w, int h,
                               uint32_t argb) {
  if (!g_fill_rect || w < 3 || h < 3)
    return;
  g_fill_rect(surface, x, y, w, 1, argb);
  g_fill_rect(surface, x, y + h - 1, w, 1, argb);
  g_fill_rect(surface, x, y, 1, h, argb);
  g_fill_rect(surface, x + w - 1, y, 1, h, argb);
}

static void __fastcall
esh_render_wheel_lcd_hook(void *this_lcd, void *edx_unused, int display_off) {
  void *mat;
  void *surface;
  void *bmp_bg;
  void *font;
  void *pit_lcd;
  int row_stride, start_y, vis;
  (void)edx_unused;

  if (*(const volatile int *)(uintptr_t)ADDR_LCD_MODE != 5) {
    g_orig_render_wheel_lcd(this_lcd, display_off);
    return;
  }

  if (!this_lcd || !g_blit_bmp || !g_lcd_draw_text || !g_font_extents)
    return;

  mat = *(void *const *)((const uint8_t *)this_lcd + 0x14);
  bmp_bg = *(void *const *)((const uint8_t *)this_lcd + 0x18);
  font = *(void *const *)((const uint8_t *)this_lcd + 0x08);
  if (!mat || !bmp_bg || !font)
    return;
  surface = *(void *const *)((const uint8_t *)mat + 0x54);
  if (!surface)
    return;

  g_blit_bmp(bmp_bg, 0, 0, surface, 0, 0, 1.0f, 1.0f, 0, 0);

  if ((display_off & 0xFF) != 0)
    return;

  esh_setup_ensure_initialized();
  esh_sync_live_brake_bias();
  {
    int wijs_w = 48, char_h = 20, max_lbl_w = 0, dummy_h = 0;
    int q, x_label, x_value, i;
    int vis_rows = esh_menu_visible_rows(&g_esh_menu);

    g_font_extents(font, "WIJS", &wijs_w, &char_h, 0);
    if (wijs_w < 16 || wijs_w > 200)
      wijs_w = 48;
    if (char_h < 10 || char_h > 32)
      char_h = 20;
    row_stride = char_h + 1;
    if (ESH_VISIBLE_ROWS * row_stride > 122)
      row_stride = 122 / ESH_VISIBLE_ROWS;
    start_y = 2;

    q = (wijs_w + 3) / 4;
    x_label = 1 + 11 * q - (q / 2);
    x_value = 1 + 11 * q + (q / 2);
    for (i = 0; i < g_esh_menu.active_count; i++) {
      int lw = 0;
      g_font_extents(font,
                     esh_param_meta((EshParamId)g_esh_menu.active_ids[i])->label,
                     &lw, &dummy_h, 0);
      if (lw > max_lbl_w)
        max_lbl_w = lw;
    }
    if (max_lbl_w > 0 && x_label < max_lbl_w + 6) {
      x_label = max_lbl_w + 6;
      x_value = x_label + q;
    }

    pit_lcd = (void *)(uintptr_t)ADDR_LCD_MODE;
    for (vis = 0; vis < vis_rows; vis++) {
      int row_idx = g_esh_menu.scroll_top + vis;
      int y;
      char val_buf[32];
      EshParamId param_id;
      if (row_idx < 0 || row_idx >= g_esh_menu.active_count)
        continue;
      param_id = (EshParamId)g_esh_menu.active_ids[row_idx];
      y = start_y + vis * row_stride;
      esh_param_format_value(&g_esh_setup, param_id, val_buf, sizeof val_buf);

      g_lcd_draw_text(pit_lcd, esh_param_meta(param_id)->label, x_label, y,
                      font, surface, 1);
      g_lcd_draw_text(pit_lcd, val_buf, x_value, y, font, surface, 0);

      if (row_idx == g_esh_menu.selected_row)
        draw_selection_box(surface, 1, y - 1, 251, row_stride + 1,
                           ESH_COLOR_BLACK);
    }

    if (g_fill_rect) {
      if (g_esh_menu.scroll_top > 0)
        g_fill_rect(surface, 249, 4, 4, 4, ESH_COLOR_BLACK);
      if (g_esh_menu.scroll_top + vis_rows < g_esh_menu.active_count)
        g_fill_rect(surface, 249, 120, 4, 4, ESH_COLOR_BLACK);
    }
  }
}

static int install_detour(uintptr_t addr, const uint8_t *sig, size_t steal_len,
                          void *hook_fn, void **orig_out, const char *name) {
  uint8_t *target = (uint8_t *)addr;
  uint8_t *trampoline;
  DWORD old_prot = 0;
  int32_t rel;

  if (memcmp(target, sig, steal_len) != 0) {
    logf_("%s hook skipped: signature mismatch at 0x%08X", name,
          (unsigned)addr);
    return 0;
  }

  trampoline = (uint8_t *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
  if (!trampoline) {
    logf_("%s hook skipped: VirtualAlloc failed (%lu)", name, GetLastError());
    return 0;
  }

  memcpy(trampoline, target, steal_len);
  trampoline[steal_len] = 0xE9;
  rel = (int32_t)((target + steal_len) - (trampoline + steal_len + 5));
  memcpy(trampoline + steal_len + 1, &rel, 4);
  FlushInstructionCache(GetCurrentProcess(), trampoline, 32);

  if (!VirtualProtect(target, steal_len, PAGE_EXECUTE_READWRITE, &old_prot)) {
    logf_("%s hook skipped: VirtualProtect failed (%lu)", name, GetLastError());
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return 0;
  }

  *orig_out = (void *)trampoline;
  target[0] = 0xE9;
  rel = (int32_t)((uint8_t *)hook_fn - (target + 5));
  memcpy(target + 1, &rel, 4);
  if (steal_len > 5)
    memset(target + 5, 0x90, steal_len - 5);
  VirtualProtect(target, steal_len, old_prot, &old_prot);
  FlushInstructionCache(GetCurrentProcess(), target, steal_len);

  logf_("%s hook armed at 0x%08X", name, (unsigned)addr);
  return 1;
}

static void resolve_shared_helpers(void) {
  const uint8_t *fill_rect = (const uint8_t *)(uintptr_t)ADDR_FILL_RECT;
  if (!g_fill_rect && memcmp(fill_rect, kSigFillRect, sizeof kSigFillRect) == 0)
    g_fill_rect = (esh_fill_rect_fn)(void *)fill_rect;
  g_set_lcd_mode = (esh_set_lcd_mode_fn)(uintptr_t)ADDR_SET_LCD_MODE;
  g_lcd_draw_text = (esh_lcd_draw_text_fn)(uintptr_t)ADDR_LCD_DRAW_TEXT;
  g_blit_bmp = (esh_blit_bmp_fn)(uintptr_t)ADDR_BLIT_BMP;
  g_font_extents = (esh_font_extents_fn)(uintptr_t)ADDR_FONT_EXTENTS;
  g_parse_range_setting =
      (esh_parse_range_setting_fn)(uintptr_t)ADDR_PARSE_RANGE_SETTING;
}

static void __fastcall
esh_render_car_status_hook(void *this_lcd, void *edx_unused, int display_off) {
  void *mat;
  void *surface;
  const uint8_t *veh;
  int is_player;
  double ai_norm;
  (void)edx_unused;

  g_orig_render_car_status(this_lcd, display_off);

  /* FUN_0049de40 reads display_off as a 1-byte bool (mov al, [esp+0x24]);
   * callers leave garbage in the upper 24 bits (e.g. 0x007CE000). */
  if ((display_off & 0xFF) != 0 || !this_lcd || !g_fill_rect)
    return;

  mat = *(void *const *)((const uint8_t *)this_lcd + 0x14);
  if (!mat)
    return;
  surface = *(void *const *)((const uint8_t *)mat + 0x54);
  if (!surface)
    return;

  veh = *(const uint8_t *const *)ADDR_VIEWED_VEH_PTR;
  if (!veh)
    return;

  is_player = viewed_vehicle_is_player();
  ai_norm = esh_normalize_damage(
      (double)*(const volatile float *)(veh + OFF_VEH_AERO_CUR),
      (double)*(const volatile float *)(veh + OFF_VEH_AERO_BASE),
      MAX_VEH_AERO_DELTA);

  /* Front Wing: overdraw only when attached; detached wings keep 1 Hz blinking
   * Red. */
  if (*(const uint32_t *)(veh + OFF_VEH_FW_REC_PTR) != 0 &&
      *(const uint8_t *)(veh + OFF_VEH_FW_INTACT) != 0) {
    double fw_norm =
        is_player ? esh_pair_damage_norm(
                        rd_f64(ADDR_FW_DRAG_CUR), rd_f64(ADDR_FW_DRAG_BASE),
                        MAX_WING_DRAG_DELTA, rd_f64(ADDR_FW_LIFT_CUR),
                        rd_f64(ADDR_FW_LIFT_BASE), MAX_WING_LIFT_DELTA)
                  : ai_norm;
    g_fill_rect(surface, FW_RECT_X, FW_RECT_Y, FW_RECT_W, FW_RECT_H,
                esh_damage_to_argb(fw_norm));
  }

  /* Rear Wing: overdraw only when attached; detached wings keep 1 Hz blinking
   * Red. */
  if (*(const uint32_t *)(veh + OFF_VEH_RW_REC_PTR) != 0 &&
      *(const uint8_t *)(veh + OFF_VEH_RW_INTACT) != 0) {
    double rw_norm =
        is_player ? esh_pair_damage_norm(
                        rd_f64(ADDR_RW_DRAG_CUR), rd_f64(ADDR_RW_DRAG_BASE),
                        MAX_WING_DRAG_DELTA, rd_f64(ADDR_RW_LIFT_CUR),
                        rd_f64(ADDR_RW_LIFT_BASE), MAX_WING_LIFT_DELTA)
                  : ai_norm;
    g_fill_rect(surface, RW_RECT_X, RW_RECT_Y, RW_RECT_W, RW_RECT_H,
                esh_damage_to_argb(rw_norm));
  }

  /* Center box: dedicated exclusively to Diffuser damage (replaces hardcoded
   * 0x0000FF00). */
  {
    double diff_norm =
        is_player ? esh_pair_damage_norm(
                        rd_f64(ADDR_DIFF_DRAG_CUR), rd_f64(ADDR_DIFF_DRAG_BASE),
                        MAX_DIFF_DRAG_DELTA, rd_f64(ADDR_DIFF_LIFT_CUR),
                        rd_f64(ADDR_DIFF_LIFT_BASE), MAX_DIFF_LIFT_DELTA)
                  : ai_norm;
    g_fill_rect(surface, DIFF_RECT_X, DIFF_RECT_Y, DIFF_RECT_W, DIFF_RECT_H,
                esh_damage_to_argb(diff_norm));
  }

  /* Left & Right Sidepods: dedicated exclusively to Body aero damage. */
  {
    double body_norm = is_player
                           ? esh_normalize_damage(rd_f64(ADDR_BODY_DRAG_CUR),
                                                  rd_f64(ADDR_BODY_DRAG_BASE),
                                                  MAX_BODY_DRAG_DELTA)
                           : ai_norm;
    uint32_t body_col = esh_damage_to_argb(body_norm);
    draw_bordered_rect(surface, SIDEPOD_L_X, SIDEPOD_L_Y, SIDEPOD_W, SIDEPOD_H,
                       body_col);
    draw_bordered_rect(surface, SIDEPOD_R_X, SIDEPOD_R_Y, SIDEPOD_W, SIDEPOD_H,
                       body_col);
  }

  /* 4 Vertical Suspension / Wheel Bend struts (FL, FR, RL, RR). */
  {
    int blink_on = (((int)rd_f64(ADDR_SESSION_TIME)) & 1) == 0;
    int corner;
    for (corner = 0; corner < 4; corner++) {
      const uint8_t *wrec =
          veh + OFF_VEH_WHEEL_ARRAY + (size_t)corner * STRIDE_VEH_WHEEL;
      int attached = (*(const uint32_t *)wrec != 0 &&
                      *(const uint8_t *)(wrec + OFF_VEH_WHEEL_INTACT) != 0);
      const esh_rect_t *r = &kSuspStrutRects[corner];
      if (!attached) {
        if (blink_on)
          draw_bordered_rect(surface, r->x, r->y, r->w, r->h, ESH_COLOR_RED);
      } else {
        double bend_norm = 0.0;
        if (is_player) {
          uintptr_t bend_addr = ADDR_PHYS_BASE + OFF_PHYS_WHEEL_ARRAY +
                                (uintptr_t)corner * STRIDE_PHYS_WHEEL +
                                OFF_WHEEL_PHYS_BEND_RAD;
          bend_norm = esh_suspension_damage_norm(rd_f64(bend_addr),
                                                 (double)g_susp_max_deg);
        }
        draw_bordered_rect(surface, r->x, r->y, r->w, r->h,
                           esh_damage_to_argb(bend_norm));
      }
    }
  }
}

static void install_midrace_setup_hooks(void) {
  install_detour(ADDR_APPLY_SETUP, kSigApplySetup, STEAL_APPLY_SETUP,
                 (void *)esh_apply_setup_hook, (void **)&g_orig_apply_setup,
                 "apply setup");
  install_detour(ADDR_POLL_LCD_INPUT, kSigPollLcdInput, STEAL_POLL_LCD_INPUT,
                 (void *)esh_poll_lcd_input_hook,
                 (void **)&g_orig_poll_lcd_input, "poll lcd input");
  install_detour(ADDR_RENDER_WHEEL_LCD, kSigRenderWheelLcd,
                 STEAL_RENDER_WHEEL_LCD, (void *)esh_render_wheel_lcd_hook,
                 (void **)&g_orig_render_wheel_lcd, "render wheel lcd");
}

static EshAiVehicleState *esh_ai_find_state(const uint8_t *veh, int create) {
  int i, free_idx = -1;
  int slot_id;
  if (!veh)
    return NULL;
  slot_id = *(const volatile int *)(veh + OFF_VEH_SCENE_SLOT_ID);
  if (slot_id >= 0 && slot_id < ESH_MAX_AI_SLOTS &&
      g_ai_states[slot_id].veh_ptr == veh)
    return &g_ai_states[slot_id];
  for (i = 0; i < ESH_MAX_AI_SLOTS; i++) {
    if (g_ai_states[i].veh_ptr == veh)
      return &g_ai_states[i];
    if (g_ai_states[i].veh_ptr == NULL && free_idx < 0)
      free_idx = i;
  }
  if (!create)
    return NULL;
  if (slot_id >= 0 && slot_id < ESH_MAX_AI_SLOTS)
    return &g_ai_states[slot_id];
  return (free_idx >= 0) ? &g_ai_states[free_idx] : NULL;
}

static void esh_ai_apply_map_to_veh(uint8_t *veh, EshAiVehicleState *st,
                                    int32_t new_setting) {
  float *live_tbl;
  int32_t live_bins, bins;
  if (!veh || !st)
    return;
  if (st->map_slot.count < 1)
    st->map_slot.count = 5;
  if (new_setting < 0)
    new_setting = 0;
  if (new_setting >= st->map_slot.count)
    new_setting = st->map_slot.count - 1;
  st->map_slot.setting = new_setting;

  if (st->base_fuel_rate > 0.0f) {
    double fscale =
        esh_engine_map_fuel_scale(&st->map_slot, g_engmap_fuel_step);
    *(volatile float *)(veh + OFF_VEH_FUEL_RATE) =
        (float)((double)st->base_fuel_rate * fscale);
  }
  live_tbl = *(float *const *)(veh + OFF_VEH_TORQUE_TBL);
  live_bins = *(const volatile int32_t *)(veh + OFF_VEH_TORQUE_BINS);
  bins = st->torque_bin_count;
  if (live_tbl && bins > 0 && live_bins >= bins) {
    esh_engine_map_scale_torque_table(
        st->base_torque_table, live_tbl, bins, &st->map_slot,
        g_engmap_boost_torque, g_engmap_boost_power);
  }
}

static void esh_ai_compute_gaps(const uint8_t *veh, float *out_ahead,
                                  float *out_behind, int32_t *out_yielding) {
  int num_veh = *(const volatile int *)(uintptr_t)ADDR_NUM_VEHICLES;
  const uint8_t *const *veh_arr =
      (const uint8_t *const *)(uintptr_t)ADDR_VEH_PTR_ARRAY;
  const uint8_t *teammate =
      *(const uint8_t *const *)(veh + OFF_VEH_TEAMMATE_PTR);
  const void *yield_target =
      *(const void *const *)(veh + OFF_VEH_YIELD_TARGET);
  int my_pos = *(const volatile int *)(veh + OFF_VEH_RACE_POS);
  int my_laps = *(const volatile int *)(veh + OFF_VEH_LAPS_DONE);
  int my_order = *(const volatile int *)(veh + OFF_VEH_DRIVER_ORDER);
  float vx = *(const volatile float *)(veh + OFF_VEH_VEL_X);
  float vy = *(const volatile float *)(veh + OFF_VEH_VEL_Y);
  float vz = *(const volatile float *)(veh + OFF_VEH_VEL_Z);
  float speed = sqrtf(vx * vx + vy * vy + vz * vz);
  float px = *(const volatile float *)(veh + OFF_VEH_POS_X);
  float py = *(const volatile float *)(veh + OFF_VEH_POS_Y);
  float pz = *(const volatile float *)(veh + OFF_VEH_POS_Z);
  float gap_a = 999.0f, gap_b = 999.0f;
  int32_t yielding = (yield_target != NULL) ? 1 : 0;
  int i;

  if (speed < 25.0f)
    speed = 25.0f;
  if (num_veh > 24)
    num_veh = 24;

  for (i = 0; i < num_veh; i++) {
    const uint8_t *other = veh_arr[i];
    int o_pos, o_laps;
    float dx, dy, dz, dist, sec;
    if (!other || other == veh)
      continue;
    o_pos = *(const volatile int *)(other + OFF_VEH_RACE_POS);
    o_laps = *(const volatile int *)(other + OFF_VEH_LAPS_DONE);
    dx = *(const volatile float *)(other + OFF_VEH_POS_X) - px;
    dy = *(const volatile float *)(other + OFF_VEH_POS_Y) - py;
    dz = *(const volatile float *)(other + OFF_VEH_POS_Z) - pz;
    dist = sqrtf(dx * dx + dy * dy + dz * dz);
    sec = dist / speed;

    if (*(const volatile uint8_t *)(other + OFF_VEH_IN_PIT_LANE) == 0) {
      if (o_laps > my_laps && (dist < 30.0f || sec < 1.0f)) {
        yielding = 1;
      } else if (other == teammate && my_laps > 3 && o_laps == my_laps &&
                 o_pos == my_pos + 1 &&
                 *(const volatile int *)(other + OFF_VEH_DRIVER_ORDER) < my_order &&
                 (dist < 30.0f || sec < 1.0f)) {
        yielding = 1;
      }
    }

    if (abs(o_laps - my_laps) > 1)
      continue;
    if (o_pos != my_pos - 1 && o_pos != my_pos + 1)
      continue;
    if (o_pos == my_pos - 1 && sec < gap_a)
      gap_a = sec;
    if (o_pos == my_pos + 1 && sec < gap_b)
      gap_b = sec;
  }
  *out_ahead = gap_a;
  *out_behind = gap_b;
  *out_yielding = yielding;
}

static void esh_ai_evaluate_and_update(uint8_t *veh, EshAiVehicleState *st,
                                       int force_update) {
  EshAiContext ctx;
  int32_t session_id, session_phase, total_laps, stint_idx, next_pit_lap;
  int32_t sector, laps_done, total_stops;
  int32_t target_map;
  double now_t;

  if (!veh || !st)
    return;
  now_t = rd_f64(ADDR_SESSION_TIME);
  if (!force_update && now_t < st->next_eval_time)
    return;
  st->next_eval_time = now_t + 2.0;

  session_id = *(const volatile int32_t *)(uintptr_t)ADDR_SESSION_ID;
  session_phase = *(const volatile int32_t *)(uintptr_t)ADDR_SESSION_PHASE;
  total_laps = *(const volatile int32_t *)(uintptr_t)ADDR_TOTAL_RACE_LAPS;
  laps_done = *(const volatile int32_t *)(veh + OFF_VEH_LAPS_DONE);
  sector = *(const volatile int32_t *)(veh + OFF_VEH_SECTOR);
  total_stops = *(const volatile int32_t *)(veh + OFF_VEH_TOTAL_STOPS);
  stint_idx = *(const volatile int32_t *)(veh + OFF_VEH_STINT_IDX);
  if (stint_idx < 0)
    stint_idx = 0;
  if (stint_idx > 4)
    stint_idx = 4;

  if (stint_idx < total_stops) {
    next_pit_lap = *(const volatile int32_t *)(veh + OFF_VEH_STINT_PIT_LAP0 +
                                               (size_t)stint_idx * 0x10u);
  } else {
    next_pit_lap = total_laps;
  }

  memset(&ctx, 0, sizeof ctx);
  ctx.session_id = session_id;
  ctx.map_count = st->map_slot.count;
  ctx.slot_seed = *(const volatile int32_t *)(veh + OFF_VEH_SCENE_SLOT_ID) +
                  laps_done * 7 + (int32_t)(now_t * 0.05);
  ctx.in_pit_or_caution =
      (*(const volatile uint8_t *)(veh + OFF_VEH_IN_PIT_LANE) != 0) ||
      (session_id == 7 && session_phase != 5);
  ctx.total_stops = total_stops;
  ctx.completed_laps = laps_done;
  ctx.total_race_laps = total_laps;
  ctx.next_pit_lap = next_pit_lap;
  ctx.stint_laps_remaining =
      (float)next_pit_lap - ((float)laps_done + (float)sector / 3.0f);
  if (ctx.stint_laps_remaining < 0.0f)
    ctx.stint_laps_remaining = 0.0f;
  ctx.current_fuel_liters = *(const volatile float *)(veh + OFF_VEH_FUEL_CUR);
  ctx.fuel_per_lap_liters =
      *(const volatile float *)(veh + OFF_VEH_FUEL_PER_LAP);
  ctx.engine_life_seconds = *(const volatile float *)(veh + OFF_VEH_ENG_LIFE);
  if (session_id == 7 && total_laps > laps_done) {
    float rem_laps = (float)total_laps - ((float)laps_done + (float)sector / 3.0f);
    if (rem_laps < 0.0f)
      rem_laps = 0.0f;
    ctx.est_race_seconds_left = rem_laps * 85.0f;
  } else {
    ctx.est_race_seconds_left = 0.0f;
  }
  ctx.engine_wounded =
      (*(const volatile float *)(veh + OFF_VEH_ENG_SMOKE) == -789.0f) ? 1 : 0;

  if (session_id == 7)
    esh_ai_compute_gaps(veh, &ctx.gap_ahead_seconds, &ctx.gap_behind_seconds,
                        &ctx.is_yielding);
  else {
    ctx.gap_ahead_seconds = 999.0f;
    ctx.gap_behind_seconds = 999.0f;
    ctx.is_yielding = 0;
  }

  target_map = esh_ai_decide_engine_map(&ctx);
  if (force_update || target_map != st->map_slot.setting)
    esh_ai_apply_map_to_veh(veh, st, target_map);
}

static void __fastcall esh_ai_init_vehicle_hook(void *this_veh_body,
                                                void *edx_unused,
                                                void *setup_struct,
                                                void *hdv_struct) {
  uint8_t *veh = (uint8_t *)this_veh_body - 0x0Cu;
  (void)edx_unused;
  g_orig_ai_init_vehicle(this_veh_body, setup_struct, hdv_struct);
  if (!this_veh_body || !setup_struct)
    return;
  if (*(const volatile int32_t *)(veh + OFF_VEH_CTRL_TYPE) == 1) {
    EshAiVehicleState *st = esh_ai_find_state(veh, 1);
    if (st) {
      const float *tbl = *(const float *const *)(veh + OFF_VEH_TORQUE_TBL);
      int32_t bins = *(const volatile int32_t *)(veh + OFF_VEH_TORQUE_BINS);
      const uint8_t *map_blk =
          (const uint8_t *)setup_struct +
          esh_setup_block_offset(ESH_IDX_ENGINE_MAP);
      int32_t map_cnt = *(const int32_t *)(map_blk + 0x10);
      st->veh_ptr = veh;
      st->base_fuel_rate = *(const volatile float *)(veh + OFF_VEH_FUEL_RATE);
      if (tbl && bins > 0) {
        if (bins > ESH_MAX_TORQUE_BINS)
          bins = ESH_MAX_TORQUE_BINS;
        memcpy(st->base_torque_table, tbl, (size_t)bins * 2u * sizeof(float));
        st->torque_bin_count = bins;
      } else {
        st->torque_bin_count = 0;
      }
      st->map_slot.base = 0.0;
      st->map_slot.step = 1.0;
      st->map_slot.count = (map_cnt >= 3) ? map_cnt : 5;
      st->map_slot.setting = (st->map_slot.count - 1) / 2;
      st->next_eval_time = 0.0;
      esh_ai_evaluate_and_update(veh, st, 1);
    }
  }
}

static void __fastcall esh_ai_plan_strategy_hook(void *this_veh,
                                                 void *edx_unused) {
  uint8_t *veh = (uint8_t *)this_veh;
  (void)edx_unused;
  g_orig_ai_plan_strategy(this_veh);
  if (!veh)
    return;
  if (*(const volatile int32_t *)(veh + OFF_VEH_CTRL_TYPE) == 1 &&
      *(const volatile int32_t *)(uintptr_t)ADDR_SESSION_ID == 7) {
    int32_t total_stops = *(const volatile int32_t *)(veh + OFF_VEH_TOTAL_STOPS);
    float mult =
        (float)esh_ai_race_fuel_buffer_mult(total_stops, g_engmap_fuel_step);
    float max_tank = *(const volatile float *)(veh + OFF_VEH_FUEL_MAX);
    float init_fuel, dry_mass, new_mass;
    int i;

    *(volatile float *)(veh + OFF_VEH_FUEL_PER_LAP) *= mult;
    init_fuel = *(const volatile float *)(veh + OFF_VEH_INIT_FUEL) * mult;
    if (max_tank > 0.0f && init_fuel > max_tank)
      init_fuel = max_tank;
    *(volatile float *)(veh + OFF_VEH_INIT_FUEL) = init_fuel;
    *(volatile float *)(veh + OFF_VEH_FUEL_CUR) = init_fuel;
    dry_mass = *(const volatile float *)(veh + OFF_VEH_DRY_MASS);
    new_mass = init_fuel * 0.75f + dry_mass;
    if (new_mass > 1.0f) {
      *(volatile float *)(veh + OFF_VEH_MASS) = new_mass;
      *(volatile float *)(veh + OFF_VEH_INV_MASS) = 1.0f / new_mass;
    }
    for (i = 0; i < 4; i++) {
      volatile float *pf = (volatile float *)(veh + OFF_VEH_STINT_PIT_LAP0 +
                                              4u + (size_t)i * 0x10u);
      float f = (*pf) * mult;
      if (max_tank > 0.0f && f > max_tank)
        f = max_tank;
      *pf = f;
    }
  }
}

static void __fastcall esh_ai_fuel_and_wear_hook(void *this_veh_body,
                                                 void *edx_unused, float dt) {
  uint8_t *veh = (uint8_t *)this_veh_body - 0x0Cu;
  EshAiVehicleState *st;
  (void)edx_unused;
  g_orig_ai_fuel_wear(this_veh_body, dt);
  if (!this_veh_body ||
      *(const volatile int32_t *)(veh + OFF_VEH_CTRL_TYPE) != 1)
    return;
  st = esh_ai_find_state(veh, 0);
  if (!st)
    return;

  {
    float life_rem = *(const volatile float *)(veh + OFF_VEH_ENG_LIFE);
    if (life_rem > 0.0f && life_rem < 1.0e30f) {
      float heat_scale =
          (float)esh_engine_map_heat_scale(&st->map_slot, g_engmap_heat_step);
      life_rem -= (heat_scale - 1.0f) * dt;
      if (life_rem < 0.01f)
        life_rem = 0.01f;
      *(volatile float *)(veh + OFF_VEH_ENG_LIFE) = life_rem;
    }
  }

  esh_ai_evaluate_and_update(veh, st, 0);
}

static void install_ai_engine_map_hooks(void) {
  install_detour(ADDR_AI_INIT_VEHICLE, kSigAiInitVehicle, STEAL_AI_INIT_VEHICLE,
                 (void *)esh_ai_init_vehicle_hook,
                 (void **)&g_orig_ai_init_vehicle, "ai init vehicle");
  install_detour(ADDR_AI_PLAN_STRATEGY, kSigAiPlanStrategy,
                 STEAL_AI_PLAN_STRATEGY, (void *)esh_ai_plan_strategy_hook,
                 (void **)&g_orig_ai_plan_strategy, "ai plan strategy");
  install_detour(ADDR_AI_FUEL_WEAR, kSigAiFuelWear, STEAL_AI_FUEL_WEAR,
                 (void *)esh_ai_fuel_and_wear_hook,
                 (void **)&g_orig_ai_fuel_wear, "ai fuel wear");
}

/*
 * Hook for FUN_004f3c10 (0x004F3C10):
 * The game saves Engine Brake Map to .svm files as "EngineBrakeMapSetting=%d"
 * (0x005C3A50), but FUN_004f3c10 only queries "EngineBrakingMapSetting"
 * (0x005D54CC). After running original FUN_004f3c10, also query
 * "EngineBrakeMapSetting" on setup_struct + 0x15F0 so .svm setups load their
 * saved Engine Brake Map setting and clamp it to [0, count - 1].
 */
static void __stdcall esh_parse_engine_settings_hook(void *parser,
                                                     uint8_t *setup_struct) {
  g_orig_parse_engine_settings(parser, setup_struct);
  if (parser && setup_struct && g_parse_range_setting) {
    uint8_t *eb_slot =
        setup_struct + esh_setup_block_offset(ESH_IDX_ENG_BRAKE);
    g_parse_range_setting(eb_slot, parser, NULL, "EngineBrakeMapSetting", 0u,
                          0x3FF00000u);
  }
}

static void esh_init(HMODULE self) {
  char dir[ESH_PATH_MAX], prev[ESH_PATH_MAX], *s;
  InitializeCriticalSection(&g_logcs);
  GetModuleFileNameA(self, dir, sizeof dir);
  s = strrchr(dir, '\\');
  if (s)
    s[1] = '\0';
  esh_path_cat(g_inipath, sizeof g_inipath, dir, ESH_INI_NAME);
  esh_path_cat(g_logpath, sizeof g_logpath, dir, ESH_LOG_NAME);
  esh_path_cat(prev, sizeof prev, dir, ESH_LOG_PREV);
  if (GetFileAttributesA(g_logpath) != INVALID_FILE_ATTRIBUTES)
    MoveFileExA(g_logpath, prev, MOVEFILE_REPLACE_EXISTING);

  g_enabled = ini_flag("Enabled", 1);
  g_car_status_colors = ini_flag("CarStatusDamageColors", 1);
  g_susp_max_deg = ini_int("SuspensionMaxBendDegrees", 5, 1, 20);
  g_midrace_setup_lcd = ini_flag("MidRaceSetupLCD", 1);
  g_engmap_fuel_step = ini_double("EngineMapFuelStep", 0.020);
  g_engmap_heat_step = ini_double("EngineMapHeatStep", 0.025);
  g_engmap_boost_torque = ini_double("EngineMapBoostTorque", 0.0075);
  g_engmap_boost_power = ini_double("EngineMapBoostPower", 0.000250);
  g_ai_dynamic_engine_map = ini_flag("AiDynamicEngineMap", 1);
  {
    int enabled_flags[ESH_PARAM_COUNT];
    int i;
    for (i = 0; i < ESH_PARAM_COUNT; i++)
      enabled_flags[i] = ini_flag(esh_param_meta((EshParamId)i)->ini_key, 1);
    esh_menu_configure(&g_esh_menu, enabled_flags);
  }
  if (g_midrace_setup_lcd && g_esh_menu.active_count == 0) {
    logf_("no MidRaceSetupLCD parameters enabled in ini: Mode 5 disabled");
    g_midrace_setup_lcd = 0;
  }
  logf_("--- expanded_setup_and_hud 1.0.0 loaded, pid %lu, enabled %d, "
        "car_status_colors %d, susp_max_deg %d, midrace_setup_lcd %d, "
        "active_params %d, ai_dynamic_engine_map %d ---",
        GetCurrentProcessId(), g_enabled, g_car_status_colors, g_susp_max_deg,
        g_midrace_setup_lcd, (int)g_esh_menu.active_count,
        g_ai_dynamic_engine_map);
  if (!g_enabled) {
    logf_("disabled in ini (Enabled=0): game unchanged");
    return;
  }
  resolve_shared_helpers();
  install_detour(ADDR_PARSE_ENGINE_SETTINGS, kSigParseEngineSettings,
                 STEAL_PARSE_ENGINE_SETTINGS,
                 (void *)esh_parse_engine_settings_hook,
                 (void **)&g_orig_parse_engine_settings,
                 "parse engine settings");
  if (g_car_status_colors) {
    install_detour(ADDR_RENDER_CAR_STATUS, kSigRenderCarStatus,
                   STEAL_RENDER_CAR_STATUS, (void *)esh_render_car_status_hook,
                   (void **)&g_orig_render_car_status, "car status");
  }
  if (g_midrace_setup_lcd)
    install_midrace_setup_hooks();
  if (g_ai_dynamic_engine_map)
    install_ai_engine_map_hooks();
}

static __attribute__((noinline)) BOOL
loader_policy_matches(const LightLoaderProtectedHostV2 *host) {
  return host->abi_version >= LLP_ABI_VERSION &&
         host->product_id == LLP_PRODUCT_ID &&
         (host->capabilities & (LLP_CAP_EXCLUSIVE_HOST | LLP_CAP_HUB_2)) ==
             (LLP_CAP_EXCLUSIVE_HOST | LLP_CAP_HUB_2) &&
         host->compatibility_generation >= LLP_HUB_2_GENERATION;
}

static BOOL validate_loader_host(const LightLoaderProtectedHostV2 *host) {
  wchar_t path[MAX_PATH];
  wchar_t *base;
  union {
    FARPROC raw;
    LightLoaderProofProc typed;
  } proof = {0};
  if (!host || host->size < sizeof(*host) || host->magic != LLP_ABI_MAGIC ||
      host->process_id != GetCurrentProcessId() || !host->loader_module ||
      host->loader_module != GetModuleHandleW(L"dinput8.dll") ||
      !loader_policy_matches(host) || host->seal != LightLoaderHostSeal(host))
    return FALSE;
  if (!GetModuleFileNameW(host->loader_module, path, MAX_PATH))
    return FALSE;
  path[MAX_PATH - 1] = L'\0';
  base = wcsrchr(path, L'\\');
  if (_wcsicmp(base ? base + 1 : path, L"dinput8.dll") != 0)
    return FALSE;
  proof.raw = GetProcAddress(host->loader_module, LLP_PROOF_NAME);
  return proof.typed &&
         proof.typed(LightLoaderProofChallenge(host)) == host->proof;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls(instance);
  return TRUE;
}

__declspec(dllexport) const char *F1CModId(void) { return ESH_MOD_ID; }

__declspec(dllexport) BOOL __cdecl
rfe_1d3b7a91(const LightLoaderProtectedHostV2 *host) {
  HMODULE self = NULL;
  if (InterlockedCompareExchange(&g_loader_init_state, 1, 0) != 0)
    return FALSE;
  if (!validate_loader_host(host)) {
    InterlockedExchange(&g_loader_init_state, 0);
    return FALSE;
  }
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     (LPCSTR)(void *)esh_init, &self);
  esh_init(self);
  return TRUE;
}
