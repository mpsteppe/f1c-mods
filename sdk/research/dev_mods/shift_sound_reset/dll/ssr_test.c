/* ssr_test.c - headless tests for Shift Sound Reset (run by build.bat).
 *
 * 1. Pure logic: shift detection on the live gear pattern (n -> 0 -> m),
 *    fade curve, .sfx line parsing, path matching, ownership distance.
 * 2. End to end through the real volume thunk with fake Miles functions and
 *    a fake vehicle: only the player's engine layers restart, other cars'
 *    identical files and non-engine sounds never do, the fade ramps 0 -> 1,
 *    the stack stays balanced and esi survives.
 * 3. A car's .sfx edited while the game runs: the re-read at session start
 *    picks up its new engine files and keeps the old ones.
 */
#include <stdint.h>
extern char ssr_test_ret_volume[] __asm__("ssr_test_ret_volume_sym");
#define SITE_VOLUME_RET ((uint32_t)(uintptr_t)ssr_test_ret_volume)
static unsigned char test_vehicles[2][0x4000];      /* slot 0 = you, slot 1 = AI teammate */
#define test_vehicle (test_vehicles[0])
#define SSR_VEH_ADDR(slot) ((uintptr_t)test_vehicles[(slot) & 1])
#define SSR_TEST_CLOCK 1
double ssr_test_clock;
#include "shift_sound_reset.c"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

/* ---- fake Miles ---- */
static float got_l, got_r;
static int ms_calls;
static HSAMPLE ms_last;
static void *__stdcall fake_file_read(const char *name, void *dest) { (void)name; return dest; }
static int32_t __stdcall fake_set_file(HSAMPLE h, const void *image, int32_t block) { (void)h; (void)image; (void)block; return 1; }
static void __stdcall fake_volume(HSAMPLE h, float l, float r) { (void)h; got_l = l; got_r = r; }
static void __stdcall fake_set_ms(HSAMPLE h, int32_t ms) { (void)ms; ms_calls++; ms_last = h; }
static void __stdcall fake_release(HSAMPLE h) { (void)h; }

/* ---- call the thunk exactly like 0x405B90 does ---- */
static uint32_t g_esp_before, g_esp_after, g_esi_after, g_esi_in;
static void *g_arg_h;
static uint32_t g_arg_l, g_arg_r;
static void *g_thunk;
static __attribute__((noinline)) void call_volume(void)
{
    __asm__ volatile(
        "pushl %%esi\n\t"
        "movl %%esp, %0\n\t"
        "pushl %5\n\t"
        "pushl %4\n\t"
        "pushl %6\n\t"
        "movl %7, %%esi\n\t"
        "call *%3\n\t"
        "ssr_test_ret_volume_sym:\n\t"
        "movl %%esp, %1\n\t"
        "movl %%esi, %2\n\t"
        "popl %%esi\n\t"
        : "=m"(g_esp_before), "=m"(g_esp_after), "=m"(g_esi_after)
        : "m"(g_thunk), "m"(g_arg_l), "m"(g_arg_r), "m"(g_arg_h), "m"(g_esi_in)
        : "eax", "ecx", "edx", "memory", "cc");
}

static float volume(uint8_t *obj, HSAMPLE h)
{
    float l = 0.8f, r = 0.6f;
    g_thunk = (void *)ssr_volume_thunk;
    g_esi_in = (uint32_t)(uintptr_t)obj;
    g_arg_h = h;
    memcpy(&g_arg_l, &l, 4);
    memcpy(&g_arg_r, &r, 4);
    call_volume();
    CHECK(g_esp_before == g_esp_after);
    CHECK(g_esi_after == g_esi_in);
    return got_l / 0.8f;
}

static void set_gear(int gear) { memcpy(test_vehicle + OFF_GEAR, &gear, 4); }
static void set_pos(uint8_t *base, float x, float y, float z) { float p[3] = {x, y, z}; memcpy(base, p, sizeof p); }

static void bind_voice(HSAMPLE h, const char *file, void *image)
{
    hk_file_read(file, image);   /* fake read returns dest = image */
    hk_set_file(h, image, -1);
}

static void test_logic(void)
{
    SsrShift s = {0, 0};
    char out[96];
    /* live pattern: 1 -> 0 -> 2 -> 0 -> 3, then 3 -> 0 -> 2 */
    CHECK(ssr_shift_step(&s, 1) == 0);           /* arms only */
    CHECK(ssr_shift_step(&s, 0) == 0);
    CHECK(ssr_shift_step(&s, 2) == 1);
    CHECK(ssr_shift_step(&s, 0) == 0);
    CHECK(ssr_shift_step(&s, 3) == 1);
    CHECK(ssr_shift_step(&s, 0) == 0);
    CHECK(ssr_shift_step(&s, 2) == -1);
    CHECK(ssr_shift_step(&s, 0) == 0);           /* neutral held ... */
    CHECK(ssr_shift_step(&s, 0) == 0);
    CHECK(ssr_shift_step(&s, 2) == 0);           /* ... same gear again: no shift */
    CHECK(ssr_shift_step(&s, -1) == -1);         /* reverse */
    CHECK(ssr_shift_step(&s, 1) == 1);
    CHECK(ssr_shift_step(&s, 77) == 0 && !s.armed);  /* garbage disarms */
    CHECK(ssr_shift_step(&s, 4) == 0);           /* re-arms */
    CHECK(ssr_shift_step(&s, 5) == 1);

    CHECK(ssr_fade_gain(0.0, 30.0) == 0.0f);
    CHECK(ssr_fade_gain(15.0, 30.0) == 0.5f);
    CHECK(ssr_fade_gain(30.0, 30.0) == 1.0f);
    CHECK(ssr_fade_gain(5.0, 0.0) == 1.0f);

    CHECK(ssr_parse_engine_line("VS_INSIDE_ENGINE_4=int\\Ferrari\\h6.wav\r\n", out, sizeof out) == SSR_KIND_INSIDE &&
          !strcmp(out, "int\\ferrari\\h6.wav"));
    CHECK(ssr_parse_engine_line("  vs_outside_engine_5 = ext/Ferrari/MAX1.wav   // loud\n", out, sizeof out) ==
          SSR_KIND_OUTSIDE && !strcmp(out, "ext\\ferrari\\max1.wav"));
    CHECK(ssr_parse_engine_line("VS_INSIDE_ENGINE_1=\"int\\Ford\\idle1.wav\"", out, sizeof out) == SSR_KIND_INSIDE &&
          !strcmp(out, "int\\ford\\idle1.wav"));
    CHECK(ssr_parse_engine_line("//VS_INSIDE_ENGINE_1=int\\Ford\\idle1.wav", out, sizeof out) == 0);
    CHECK(ssr_parse_engine_line("VS_INSIDE_ENGINE_STARTER=starter_1.wav", out, sizeof out) == 0);
    CHECK(ssr_parse_engine_line("VS_INSIDE_SHIFT_UP_1=internal_shift_up_1.wav", out, sizeof out) == 0);
    CHECK(ssr_parse_engine_line("VS_INSIDE_ENGINE_2=", out, sizeof out) == 0);
    CHECK(ssr_parse_engine_line("VS_INSIDE_ENGINE_2=abcdefghij", out, 6) == 0);   /* too long */

    CHECK(ssr_path_matches("AUDIO\\VEHICLE\\INT\\FERRARI\\MAX4.WAV", "int\\ferrari\\max4.wav"));
    CHECK(ssr_path_matches("audio/vehicle/int/ferrari/max4.wav", "int\\ferrari\\max4.wav"));
    CHECK(!ssr_path_matches("AUDIO\\VEHICLE\\INT\\FERRARI\\MAX44.WAV", "int\\ferrari\\max4.wav"));
    CHECK(!ssr_path_matches("AUDIO\\VEHICLE\\PRINT\\FERRARI\\MAX4.WAV", "int\\ferrari\\max4.wav"));
    CHECK(!ssr_path_matches("AUDIO\\VEHICLE\\EXT\\FERRARI\\MAX4.WAV", "int\\ferrari\\max4.wav"));

    /* ownership, with the distances measured in game by 0.1.0: the focus
     * car's sounds 1.25 m from it, a car alongside with its sounds 1.88 m
     * away (inside the 2 m search), one far away */
    {
        SsrAnchor an;
        float d2;
        float own[3] = {0.0f, 0.0f, -1.25f}, drift[3] = {0.1f, 0.0f, -1.25f};
        float beside[3] = {1.6f, 0.0f, -0.99f}, distant[3] = {8.0f, 0.0f, 0.0f};
        memset(&an, 0, sizeof an);
        /* first window: nothing is trusted yet, not even the closest sound,
         * and the car alongside being processed first changes nothing */
        CHECK(!ssr_anchor_classify(&an, beside, 0.0, 2.0f, 0.25f, 50.0, &d2));
        CHECK(!ssr_anchor_classify(&an, own, 1.0, 2.0f, 0.25f, 50.0, &d2) && fabsf(sqrtf(d2) - 1.25f) < 1e-4f);
        CHECK(!ssr_anchor_classify(&an, distant, 2.0, 2.0f, 0.25f, 50.0, &d2));
        /* next window: the closest sound of the last one is the car's point */
        CHECK(!ssr_anchor_classify(&an, beside, 60.0, 2.0f, 0.25f, 50.0, &d2));
        CHECK(ssr_anchor_classify(&an, own, 61.0, 2.0f, 0.25f, 50.0, &d2));
        CHECK(ssr_anchor_classify(&an, drift, 62.0, 2.0f, 0.25f, 50.0, &d2));   /* 10 cm: same car */
        CHECK(!ssr_anchor_classify(&an, distant, 63.0, 2.0f, 0.25f, 50.0, &d2));
        /* the car's sounds stop: the point is dropped after 500 ms */
        CHECK(!ssr_anchor_classify(&an, distant, 700.0, 2.0f, 0.25f, 50.0, &d2) && an.valid);
        CHECK(!ssr_anchor_classify(&an, distant, 1300.0, 2.0f, 0.25f, 50.0, &d2) && !an.valid);
        CHECK(!ssr_anchor_classify(&an, own, 1301.0, 2.0f, 0.25f, 50.0, &d2));   /* must re-learn */
    }
}

static void test_end_to_end(void)
{
    static uint8_t own_int[0x60], own_ext[0x60], other_ext[0x60], own_skid[0x60];
    static char img_int[4], img_ext[4], img_other[4], img_skid[4];
    HSAMPLE h_int = (HSAMPLE)0x101, h_ext = (HSAMPLE)0x102, h_other = (HSAMPLE)0x103, h_skid = (HSAMPLE)0x104;
    float g;

    InitializeCriticalSection(&g_cs);
    g_file_read = fake_file_read;
    g_set_file = fake_set_file;
    g_set_volume = fake_volume;
    g_set_ms = fake_set_ms;
    g_release = fake_release;
    g_active = 1;
    g_fade_ms = 30.0;
    stage_engine_name("int\\ferrari\\max4.wav", SSR_KIND_INSIDE);
    stage_engine_name("ext\\ferrari\\max1.wav", SSR_KIND_OUTSIDE);
    CHECK(merge_engine_names() == 2 && g_names_ready);
    g_focus_slot = 0;

    /* distances as measured in game by 0.1.0: your sounds 1.25 m from your
     * car; the teammate alongside (1.6 m to the side) with its sounds 0.99 m
     * from its car, i.e. 1.88 m from yours: inside the 2 m search */
    set_pos(test_vehicle + OFF_WORLD_POS, 13.62f, -7.50f, 3.83f);
    set_pos(own_int + OBJ_POS, 13.62f, -7.50f, 3.83f - 1.25f);
    set_pos(own_ext + OBJ_POS, 13.62f, -7.50f, 3.83f - 1.25f);
    set_pos(own_skid + OBJ_POS, 13.62f, -7.50f, 3.83f - 1.25f);
    set_pos(other_ext + OBJ_POS, 15.22f, -7.50f, 3.83f - 0.99f);   /* teammate, same file */
    bind_voice(h_int, "AUDIO\\VEHICLE\\INT\\FERRARI\\MAX4.WAV", img_int);
    bind_voice(h_ext, "AUDIO\\VEHICLE\\EXT\\FERRARI\\MAX1.WAV", img_ext);
    bind_voice(h_other, "AUDIO\\VEHICLE\\EXT\\FERRARI\\MAX1.WAV", img_other);
    bind_voice(h_skid, "AUDIO\\VEHICLE\\INTERNAL_SKID_ROAD_DRY.WAV", img_skid);

#define FRAME() do { volume(own_int, h_int); volume(own_ext, h_ext); volume(other_ext, h_other); volume(own_skid, h_skid); } while (0)
    ssr_test_clock = 1000.0;
    set_gear(2);
    FRAME();                                   /* arms; first sight is not a shift */
    CHECK(ms_calls == 0);
    ssr_test_clock += 60.0;
    FRAME();                                   /* one window later the sound point is known */
    CHECK(ms_calls == 0);
    ssr_test_clock += 16.0;
    set_gear(0);
    FRAME();
    CHECK(ms_calls == 0);
    ssr_test_clock += 16.0;
    set_gear(3);                               /* upshift lands */
    g = volume(own_int, h_int);
    CHECK(ms_calls == 1 && ms_last == h_int && g == 0.0f);
    g = volume(own_ext, h_ext);
    CHECK(ms_calls == 2 && ms_last == h_ext && g == 0.0f);
    g = volume(other_ext, h_other);
    CHECK(ms_calls == 2 && g == 1.0f);         /* other car: untouched */
    g = volume(own_skid, h_skid);
    CHECK(ms_calls == 2 && g == 1.0f);         /* not an engine layer */
    ssr_test_clock += 15.0;
    g = volume(own_int, h_int);
    CHECK(ms_calls == 2 && g == 0.5f);         /* halfway through the fade, no second restart */
    ssr_test_clock += 15.0;
    g = volume(own_int, h_int);
    CHECK(g == 1.0f);
    {
        ShiftStat *st = &g_stats[g_gen % MAX_STATS];
        CHECK(st->from == 2 && st->to == 3 && st->reset_inside == 1 && st->reset_outside == 1);
        CHECK(fabsf(st->max_owned_m - 1.25f) < 0.01f && fabsf(st->min_foreign_m - 1.88f) < 0.01f);
    }

    /* downshift disabled: no restart */
    g_down = 0;
    ssr_test_clock += 500.0;
    set_gear(0);
    FRAME();
    ssr_test_clock += 16.0;
    set_gear(2);
    FRAME();
    CHECK(ms_calls == 2);
    g_down = 1;

    /* the next upshift restarts again */
    ssr_test_clock += 500.0;
    set_gear(0);
    FRAME();
    ssr_test_clock += 16.0;
    set_gear(3);
    FRAME();
    CHECK(ms_calls == 4);

    /* a layer that gets a voice long after the shift is not restarted or faded */
    {
        static uint8_t own_late[0x60];
        static char img_late[4];
        HSAMPLE h_late = (HSAMPLE)0x105;
        set_pos(own_late + OBJ_POS, 13.62f, -7.50f, 3.83f - 1.25f);
        bind_voice(h_late, "AUDIO\\VEHICLE\\EXT\\FERRARI\\MAX1.WAV", img_late);
        ssr_test_clock += 400.0;
        g = volume(own_late, h_late);
        CHECK(ms_calls == 4 && g == 1.0f);
    }

    /* switching the camera to the AI teammate (+/- in game): its sounds are
     * now the main car's. The switch itself is not a shift, its own shift
     * restarts its layer, and the player's layers are left alone. */
    {
        int ai_gear = 5;
        set_pos(test_vehicles[1] + OFF_WORLD_POS, 15.22f, -7.50f, 3.83f);   /* alongside you */
        memcpy(test_vehicles[1] + OFF_GEAR, &ai_gear, 4);
        g_focus_slot = 1;
        g_rearm = 1;                           /* what refresh_focus_slot does */
        ssr_test_clock += 500.0;
        FRAME();
        CHECK(ms_calls == 4);                  /* gear 3 (you) -> 5 (AI) is not a shift */
        ssr_test_clock += 60.0;
        FRAME();                               /* the teammate's sound point is learned */
        CHECK(ms_calls == 4);
        ai_gear = 0;
        memcpy(test_vehicles[1] + OFF_GEAR, &ai_gear, 4);
        ssr_test_clock += 16.0;
        FRAME();
        ai_gear = 6;
        memcpy(test_vehicles[1] + OFF_GEAR, &ai_gear, 4);
        ssr_test_clock += 16.0;
        g = volume(own_int, h_int);
        CHECK(ms_calls == 4 && g == 1.0f);     /* your car: untouched */
        g = volume(other_ext, h_other);
        CHECK(ms_calls == 5 && ms_last == h_other && g == 0.0f);
        CHECK(g_stats[g_gen % MAX_STATS].slot == 1);
        set_gear(2);                           /* your gear changing meanwhile: ignored */
        ssr_test_clock += 16.0;
        FRAME();
        CHECK(ms_calls == 5);
        g_focus_slot = 0;
        g_rearm = 1;
    }

    /* inactive (Enabled=0 or failed install): pure pass-through */
    g_active = 0;
    ssr_test_clock += 500.0;
    set_gear(0);
    FRAME();
    set_gear(4);
    FRAME();
    CHECK(ms_calls == 5 && got_l == 0.8f && got_r == 0.6f);
#undef FRAME
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int name_known(const char *rel)
{
    unsigned i;
    for (i = 0; i < g_name_count; i++)
        if (!strcmp(g_names[i].rel, rel)) return 1;
    return 0;
}

/* The owner's case (README rules 21-22): a car's .sfx is edited while the game
 * is open. A layer named only in the edited file is unknown until the re-read
 * at the next session start; then it is recognised, even on a voice bound
 * before the re-read, and the names of the old file stay known. */
static void test_rescan(void)
{
    char root[MAX_PATH], veh[MAX_PATH + 16], dir[MAX_PATH + 32], sfx[MAX_PATH + 48];
    static uint8_t lambo[0x60];
    static char img_lambo[4];
    HSAMPLE h = (HSAMPLE)0x106;
    HandleEntry *he;
    unsigned before = g_name_count;
    int calls;
    DWORD n = GetTempPathA(sizeof root, root);
    CHECK(n > 0 && n < sizeof root - 64);
    snprintf(root + n, sizeof root - n, "ssr_test_scan_%lu\\", (unsigned long)GetCurrentProcessId());
    snprintf(veh, sizeof veh, "%sVehicles\\", root);
    snprintf(dir, sizeof dir, "%sLotus\\", veh);
    snprintf(sfx, sizeof sfx, "%sLambo.sfx", dir);
    CreateDirectoryA(root, NULL);
    CreateDirectoryA(veh, NULL);
    CreateDirectoryA(dir, NULL);

    write_text(sfx, "VS_OUTSIDE_ENGINE_1=ext\\Lambo\\v1g.wav\nVS_OUTSIDE_ENGINE_5=ext\\Lambo\\max2k.wav\n");
    CHECK(scan_engine_names_in(root, 0) == 2 && g_name_count == before + 2);

    /* the .sfx is edited and the car loaded with it; the mod has not re-read yet */
    write_text(sfx, "VS_OUTSIDE_ENGINE_1=ext\\Lambo\\idle1.wav\nVS_OUTSIDE_ENGINE_5=ext\\Lambo\\max1k.wav\n");
    g_active = 1;
    g_focus_slot = 0;
    g_rearm = 1;
    set_pos(test_vehicle + OFF_WORLD_POS, 100.0f, 0.0f, 0.0f);
    set_pos(lambo + OBJ_POS, 100.0f, 0.0f, -1.3f);
    bind_voice(h, "AUDIO\\VEHICLE\\EXT\\LAMBO\\IDLE1.WAV", img_lambo);
    calls = ms_calls;
    ssr_test_clock += 500.0;
    set_gear(2);
    volume(lambo, h);
    ssr_test_clock += 60.0;
    volume(lambo, h);
    ssr_test_clock += 16.0;
    set_gear(0);
    volume(lambo, h);
    ssr_test_clock += 16.0;
    set_gear(3);
    volume(lambo, h);
    he = handle_entry(h, 0);
    CHECK(ms_calls == calls && he && he->kind == 0);          /* unknown file: not restarted */

    /* next session start: re-read */
    CHECK(scan_engine_names_in(root, 1) == 2 && g_name_count == before + 4);
    CHECK(name_known("ext\\lambo\\idle1.wav") && name_known("ext\\lambo\\max1k.wav"));
    CHECK(name_known("ext\\lambo\\v1g.wav") && name_known("ext\\lambo\\max2k.wav"));   /* never dropped */
    CHECK(he->kind == -1);                                    /* the bound voice re-checks its file */
    CHECK(scan_engine_names_in(root, 1) == 0 && g_name_count == before + 4);           /* nothing new */

    /* the same voice, still playing, is restarted on the next shift */
    ssr_test_clock += 500.0;
    volume(lambo, h);
    CHECK(he->kind == SSR_KIND_OUTSIDE);
    ssr_test_clock += 60.0;
    volume(lambo, h);
    ssr_test_clock += 16.0;
    set_gear(0);
    volume(lambo, h);
    ssr_test_clock += 16.0;
    set_gear(4);
    CHECK(volume(lambo, h) == 0.0f && ms_calls == calls + 1 && ms_last == h);

    DeleteFileA(sfx);
    RemoveDirectoryA(dir);
    RemoveDirectoryA(veh);
    RemoveDirectoryA(root);
}

/* Logging is off unless Log=1: no key, an old ini with only LogShifts=1, or an
 * invalid value all mean no log file. */
static void test_log_setting(void)
{
    char ini[MAX_PATH + 32];
    DWORD n = GetTempPathA(sizeof g_dir, g_dir);
    CHECK(n > 0 && n < sizeof g_dir - 40);
    snprintf(g_dir + n, sizeof g_dir - n, "ssr_test_ini_%lu\\", (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_dir, NULL);
    snprintf(ini, sizeof ini, "%s%s", g_dir, SSR_INI);

    write_text(ini, "[Mod]\nEnabled=1\n");
    g_log = 1;
    load_settings();
    CHECK(g_log == 0);
    write_text(ini, "[Advanced]\nLogShifts=1\n");
    load_settings();
    CHECK(g_log == 0);
    write_text(ini, "[Advanced]\nLog=1\n");
    load_settings();
    CHECK(g_log == 1);
    write_text(ini, "[Advanced]\nLog=yes\n");
    load_settings();
    CHECK(g_log == 0);
    DeleteFileA(ini);                  /* no ini at all */
    g_log = 1;
    load_settings();
    CHECK(g_log == 0);

    RemoveDirectoryA(g_dir);
    g_dir[0] = '\0';
}

int main(void)
{
    test_logic();
    test_end_to_end();
    test_rescan();
    test_log_setting();
    if (failures) printf("ssr_test: %d of %d checks FAILED\n", failures, checks);
    else printf("ssr_test: PASS (%d checks)\n", checks);
    return failures ? 1 : 0;
}
