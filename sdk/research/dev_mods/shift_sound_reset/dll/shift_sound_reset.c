/*
 * shift_sound_reset.c - Shift Sound Reset for F1 Challenge 99-02.
 *
 * On every gear change (up and/or down, configurable) of the car being heard
 * as the main car, its engine sound layers restart from the beginning of
 * their recordings, with a configurable fade-in. Cockpit and outside layers.
 * The main car is the one the camera is on (switched with +/- in game), else
 * the player's own car; every other car is left untouched.
 *
 * Why: modern sound packs use long engine recordings. The game loops each
 * layer continuously and only changes its pitch, so a recording ends in the
 * middle of a gear and jumps back to its start (live probe 2026-10-26: the
 * layers play straight through 141 shifts).
 *
 * How (evidence: ../README.md, probe runs 2026-10-26):
 *   - The game's sound-object update 0x405B90 (thiscall, esi = sound object)
 *     calls AIL_set_sample_volume_levels (returns to 0x405CC7) and then
 *     AIL_set_sample_playback_rate for every audible sound, every frame.
 *     This mod redirects the game's Mss32 import of the volume call to a
 *     thunk that passes esi through; the original is always called.
 *   - Engine layers: the worker reads every .sfx under the game folder and
 *     collects VS_INSIDE_ENGINE_n / VS_OUTSIDE_ENGINE_n files. A voice plays
 *     an engine layer when the file loaded into it (AIL_file_read +
 *     AIL_set_sample_file) ends with one of those names. The .sfx files are
 *     read again each time a session is loaded, as the game does, so edits
 *     made while the game is open are picked up (names are only added).
 *   - Focus car: broker session snapshot spectated_vehicle_slot (the camera
 *     target, game 0x7C54DC) when valid, else the player's slot (broker
 *     controller_role == 0). Polled every 100 ms by the worker.
 *   - Ownership: all of a car's sound objects share one position
 *     (+0x28/+0x2C/+0x30), 1.0-1.3 m from the car's WorldPosition (0x34E0,
 *     FACT) by a car-dependent amount (0.1.0 in game: the player's sounds sat
 *     1.2-1.3 m away, so OwnerMeters 1.0 never matched them). The focus car's
 *     sound point is the closest engine-layer sound within SearchMeters of
 *     the car, kept relative to the car; a sound belongs to the focus car
 *     when it is within MatchMeters of that point (ssr_anchor_classify).
 *     Other cars may use the very same files.
 *   - Shift: the focus car's gear (0x39FC, FACT) lands on a non-neutral gear
 *     that differs from the last one (each shift is n -> 0 -> m in one
 *     frame). Changing focus car re-arms this, so it never counts as a shift.
 *   - Restart: the first time each of the focus car's engine-layer objects
 *     passes the volume call after a shift, AIL_set_sample_ms_position(h, 0)
 *     on its current voice, then its volume is scaled from 0 to 1 over
 *     FadeInMs. Layers without a voice need nothing: they start at 0 when
 *     the game gives them one again.
 *
 * Safety: fails closed on an unknown executable layout, unexpected bytes at
 * the call site, an import already redirected by another module, or a
 * missing Mss32 export. Enabled=0 installs nothing. No game code bytes are
 * patched; hooks never do file I/O. The module is pinned (hooks live for the
 * process); restart the game to remove it.
 *
 * Entry: Lightloader protected host export rfe_1d3b7a91. DllMain is inert.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <wchar.h>
#include "protected_plugin_abi.h"
#include "f1c_broker.h"
#include "ssr_logic.h"

#define SSR_ID       "shift_sound_reset"
#define SSR_VERSION  "0.1.3"
#define SSR_INI      SSR_ID ".ini"
#define SSR_LOG      SSR_ID "_log.txt"
#define SSR_LOG_PREV SSR_ID "_log_prev.txt"
#define SSR_LOG_MAX  (1024L * 1024L)

/* Layout gate: the SDK target and the patched co-op variant share these. */
#define EXE_TIMESTAMP  0x3E9C24F6u
#define EXE_IMAGE_SIZE 0x005FA000u
#define SITE_VOLUME_CALL 0x00405CC1u
#ifndef SITE_VOLUME_RET            /* overridable by ssr_test.c only */
#define SITE_VOLUME_RET  0x00405CC7u
#endif
static const uint8_t k_site_volume[6] = {0xFF, 0x15, 0xB8, 0xE3, 0x59, 0x00};

#define VEH_BASE   0x005EC6D8u
#define VEH_STRIDE 0x48C0u
#define VEH_SLOTS  104
#ifndef SSR_VEH_ADDR               /* overridable by ssr_test.c only */
#define SSR_VEH_ADDR(slot) (VEH_BASE + (uintptr_t)(slot) * VEH_STRIDE)
#endif
#define OFF_WORLD_POS 0x34E0u      /* WorldPositionX/Y/Z, FACT */
#define OFF_GEAR      0x39FCu      /* CurrentGear, FACT */
#define OBJ_POS       0x28u        /* sound object world position (live probe) */

#define RESET_WINDOW_MS 200.0      /* objects passing the update this soon after a shift */
#define STATS_WINDOW_MS 300.0      /* distance statistics collected per shift */
#define ANCHOR_WINDOW_MS 50.0      /* focus car sound point refreshed this often */

/* ------------------------------------------------------------ settings */
static int g_enabled = 1, g_up = 1, g_down = 1, g_log = 0;
static double g_fade_ms = 30.0;
static float g_search_m = 2.0f, g_match_m = 0.25f;

/* ------------------------------------------------------------ Miles */
typedef void *HSAMPLE;
typedef void *(__stdcall *file_read_fn)(const char *, void *);
typedef int32_t (__stdcall *set_file_fn)(HSAMPLE, const void *, int32_t);
typedef void (__stdcall *h_fn)(HSAMPLE);
typedef void (__stdcall *vol_fn)(HSAMPLE, float, float);
typedef void (__stdcall *set_ms_fn)(HSAMPLE, int32_t);

static file_read_fn g_file_read;
static set_file_fn g_set_file;
static h_fn g_release;
static vol_fn g_set_volume;
static set_ms_fn g_set_ms;

/* ------------------------------------------------------------ state */
typedef struct HandleEntry {
    HSAMPLE h;
    const void *image;
    int kind;                    /* -1 unknown, 0 not engine, 1 inside, 2 outside */
    char file[80];
} HandleEntry;
#define MAX_HANDLES 64
static HandleEntry g_handles[MAX_HANDLES];

typedef struct FileMap { const void *image; char name[80]; } FileMap;
#define MAX_FILES 512
static FileMap g_files[MAX_FILES];
static unsigned g_file_next;

typedef struct EngineName { char rel[96]; int kind; } EngineName;
#define MAX_NAMES 2048
static EngineName g_names[MAX_NAMES];
static unsigned g_name_count;
static volatile LONG g_names_ready;

typedef struct ObjEntry {
    uint32_t obj;
    uint32_t gen;
    int fading;
    double fade_start;
    double last_seen;
} ObjEntry;
#define MAX_OBJS 64
static ObjEntry g_objs[MAX_OBJS];

typedef struct ShiftStat {
    volatile LONG pending;       /* 1 while collecting, 2 ready to log */
    uint32_t gen;
    int slot, from, to;
    double at;
    unsigned reset_inside, reset_outside, owned_seen, foreign_seen;
    float max_owned_m, min_foreign_m;
} ShiftStat;
#define MAX_STATS 32
static ShiftStat g_stats[MAX_STATS];

static CRITICAL_SECTION g_cs;
static volatile LONG g_focus_slot = -1;
static volatile LONG g_rearm = 1;
static SsrShift g_shift;
static SsrAnchor g_anchor;
static uint32_t g_gen;
static double g_shift_at = -1e9;
static int g_active;
static double g_qpc_per_ms = 1.0;
static HMODULE g_module;
static char g_dir[MAX_PATH], g_log_path[MAX_PATH], g_status[256];
static long g_log_bytes;
static int g_log_full;

#ifdef SSR_TEST_CLOCK              /* ssr_test.c drives time itself */
extern double ssr_test_clock;
static double now_ms(void) { return ssr_test_clock; }
#else
static double now_ms(void)
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return (double)q.QuadPart / g_qpc_per_ms;
}
#endif

static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t a = (uintptr_t)p;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return a + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

/* ------------------------------------------------------------ tables (g_cs held) */
static HandleEntry *handle_entry(HSAMPLE h, int add)
{
    unsigned i, free_slot = MAX_HANDLES;
    for (i = 0; i < MAX_HANDLES; i++) {
        if (g_handles[i].h == h) return &g_handles[i];
        if (!g_handles[i].h && free_slot == MAX_HANDLES) free_slot = i;
    }
    if (!add || free_slot == MAX_HANDLES) return NULL;
    memset(&g_handles[free_slot], 0, sizeof g_handles[0]);
    g_handles[free_slot].h = h;
    g_handles[free_slot].kind = -1;
    return &g_handles[free_slot];
}

static const char *file_for(const void *image)
{
    unsigned i;
    for (i = 0; i < MAX_FILES; i++)
        if (g_files[i].image == image && g_files[i].name[0]) return g_files[i].name;
    return NULL;
}

static int handle_kind(HandleEntry *he)
{
    unsigned i;
    if (he->kind >= 0) return he->kind;
    if (!g_names_ready) return 0;          /* not known yet; retried next frame */
    he->kind = 0;
    if (he->file[0])
        for (i = 0; i < g_name_count; i++)
            if (ssr_path_matches(he->file, g_names[i].rel)) { he->kind = g_names[i].kind; break; }
    return he->kind;
}

static ObjEntry *obj_entry(uint32_t obj, double now)
{
    unsigned i, victim = 0;
    for (i = 0; i < MAX_OBJS; i++) {
        if (g_objs[i].obj == obj) return &g_objs[i];
        if (g_objs[i].last_seen < g_objs[victim].last_seen) victim = i;
    }
    for (i = 0; i < MAX_OBJS; i++)
        if (!g_objs[i].obj) { victim = i; break; }
    memset(&g_objs[victim], 0, sizeof g_objs[0]);
    g_objs[victim].obj = obj;
    g_objs[victim].gen = g_gen;        /* first sight is not a shift */
    g_objs[victim].last_seen = now;
    return &g_objs[victim];
}

/* ------------------------------------------------------------ hooks */
static void *__stdcall hk_file_read(const char *name, void *dest)
{
    void *r = g_file_read(name, dest);
    if (r && name && readable(name, 1)) {
        size_t n = strlen(name);
        const char *tail = n >= sizeof g_files[0].name ? name + n - (sizeof g_files[0].name - 1) : name;
        EnterCriticalSection(&g_cs);
        g_files[g_file_next].image = r;
        lstrcpynA(g_files[g_file_next].name, tail, sizeof g_files[0].name);
        g_file_next = (g_file_next + 1u) % MAX_FILES;
        LeaveCriticalSection(&g_cs);
    }
    return r;
}

static int32_t __stdcall hk_set_file(HSAMPLE h, const void *image, int32_t block)
{
    int32_t r = g_set_file(h, image, block);
    HandleEntry *he;
    EnterCriticalSection(&g_cs);
    he = handle_entry(h, 1);
    if (he) {
        const char *name = file_for(image);
        he->image = image;
        he->kind = -1;
        lstrcpynA(he->file, name ? name : "", sizeof he->file);
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

static void __stdcall hk_release(HSAMPLE h)
{
    HandleEntry *he;
    g_release(h);
    EnterCriticalSection(&g_cs);
    he = handle_entry(h, 0);
    if (he) memset(he, 0, sizeof *he);
    LeaveCriticalSection(&g_cs);
}

/* Decides, under g_cs, whether this voice is a player engine layer that must
 * restart now, and the gain to apply. Never calls Miles. */
static float volume_decision(uint32_t obj, HSAMPLE h, int *restart)
{
    double now = now_ms();
    float gain = 1.0f, car[3], pos[3], d2;
    int slot, gear, from, dir, kind, owned;
    uintptr_t veh;
    HandleEntry *he;
    ShiftStat *st;
    *restart = 0;
    slot = (int)g_focus_slot;
    if (slot < 0 || slot >= VEH_SLOTS) return 1.0f;
    veh = SSR_VEH_ADDR(slot);
    gear = *(volatile int32_t *)(veh + OFF_GEAR);
    memcpy(car, (const void *)(veh + OFF_WORLD_POS), sizeof car);

    if (InterlockedExchange(&g_rearm, 0)) {
        memset(&g_shift, 0, sizeof g_shift);
        memset(&g_anchor, 0, sizeof g_anchor);
    }
    from = g_shift.last_gear;
    dir = ssr_shift_step(&g_shift, gear);
    if ((dir > 0 && g_up) || (dir < 0 && g_down)) {
        g_gen++;
        g_shift_at = now;
        st = &g_stats[g_gen % MAX_STATS];
        if (st->pending != 1) {            /* never overwrite one being collected */
            memset(st, 0, sizeof *st);
            st->gen = g_gen;
            st->slot = slot;
            st->from = from;
            st->to = gear;
            st->at = now;
            st->min_foreign_m = INFINITY;
            st->pending = 1;
        }
    }

    he = handle_entry(h, 0);
    if (!he || !(kind = handle_kind(he))) return 1.0f;
    if (!readable((const void *)(uintptr_t)obj, OBJ_POS + sizeof pos)) return 1.0f;
    memcpy(pos, (const void *)(uintptr_t)(obj + OBJ_POS), sizeof pos);
    {
        float rel[3] = {pos[0] - car[0], pos[1] - car[1], pos[2] - car[2]};
        owned = ssr_anchor_classify(&g_anchor, rel, now, g_search_m, g_match_m, ANCHOR_WINDOW_MS, &d2);
    }

    st = &g_stats[g_gen % MAX_STATS];
    if (st->pending == 1 && st->gen == g_gen && now - st->at <= STATS_WINDOW_MS) {
        float d = sqrtf(d2);
        if (owned) { st->owned_seen++; if (d > st->max_owned_m) st->max_owned_m = d; }
        else { st->foreign_seen++; if (d < st->min_foreign_m) st->min_foreign_m = d; }
    }
    if (!owned) return 1.0f;

    {
        ObjEntry *oe = obj_entry(obj, now);
        oe->last_seen = now;
        if (oe->gen != g_gen) {
            oe->gen = g_gen;
            if (now - g_shift_at <= RESET_WINDOW_MS) {
                *restart = 1;
                oe->fading = g_fade_ms > 0.0;
                oe->fade_start = now;
                if (st->pending == 1 && st->gen == g_gen) {
                    if (kind == SSR_KIND_INSIDE) st->reset_inside++;
                    else st->reset_outside++;
                }
            }
        }
        if (oe->fading) {
            gain = ssr_fade_gain(now - oe->fade_start, g_fade_ms);
            if (gain >= 1.0f) oe->fading = 0;
        }
    }
    return gain;
}

void ssr_volume_impl(uint32_t esi, uint32_t ret, HSAMPLE h, float left, float right)
    __asm__("ssr_volume_impl_sym") __attribute__((used));
void ssr_volume_impl(uint32_t esi, uint32_t ret, HSAMPLE h, float left, float right)
{
    float gain = 1.0f;
    int restart = 0;
    if (g_active && ret == SITE_VOLUME_RET && esi) {
        EnterCriticalSection(&g_cs);
        gain = volume_decision(esi, h, &restart);
        LeaveCriticalSection(&g_cs);
        if (restart) g_set_ms(h, 0);
    }
    g_set_volume(h, left * gain, right * gain);
}

/* stdcall(h, left, right): pass esi and the return address to the cdecl impl. */
extern void ssr_volume_thunk(void) __asm__("ssr_volume_thunk_sym");
__asm__(
    ".text\n"
    "ssr_volume_thunk_sym:\n"
    "    pushl 12(%esp)\n"         /* right */
    "    pushl 12(%esp)\n"         /* left */
    "    pushl 12(%esp)\n"         /* h */
    "    pushl 12(%esp)\n"         /* return address */
    "    pushl %esi\n"
    "    call ssr_volume_impl_sym\n"
    "    addl $20, %esp\n"
    "    ret $12\n");

/* ------------------------------------------------------------ install */
typedef struct Hook { const char *name; void *hook; void **orig; DWORD *slot; } Hook;
static Hook g_hooks[] = {
    { "_AIL_file_read@8",                 (void *)hk_file_read,     (void **)&g_file_read,  NULL },
    { "_AIL_set_sample_file@12",          (void *)hk_set_file,      (void **)&g_set_file,   NULL },
    { "_AIL_release_sample_handle@4",     (void *)hk_release,       (void **)&g_release,    NULL },
    { "_AIL_set_sample_volume_levels@12", (void *)ssr_volume_thunk, (void **)&g_set_volume, NULL },
};
#define HOOK_COUNT (sizeof g_hooks / sizeof g_hooks[0])

static DWORD *iat_slot(HMODULE exe, const char *dll, const char *fn)
{
    uint8_t *b = (uint8_t *)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!rva) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + rva); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *slots;
        if (_stricmp((const char *)(b + imp->Name), dll) || !imp->OriginalFirstThunk) continue;
        names = (IMAGE_THUNK_DATA *)(b + imp->OriginalFirstThunk);
        slots = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (!strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(b + names->u1.AddressOfData))->Name, fn))
                return (DWORD *)&slots->u1.Function;
        }
    }
    return NULL;
}

/* All-or-nothing: every slot must exist and still point at the real Mss32
 * export (no foreign hook) before any slot is written. */
static const char *install_hooks(void)
{
    HMODULE exe = GetModuleHandleA(NULL), mss = GetModuleHandleA("mss32.dll");
    unsigned i;
    if (!mss) return "mss32.dll not loaded";
    g_set_ms = (set_ms_fn)(void *)GetProcAddress(mss, "_AIL_set_sample_ms_position@8");
    if (!g_set_ms) return "Mss32 has no AIL_set_sample_ms_position";
    for (i = 0; i < HOOK_COUNT; i++) {
        FARPROC real = GetProcAddress(mss, g_hooks[i].name);
        g_hooks[i].slot = iat_slot(exe, "mss32.dll", g_hooks[i].name);
        if (!g_hooks[i].slot || !real) return g_hooks[i].name;
        if (*g_hooks[i].slot != (DWORD)(uintptr_t)real) return "import already redirected by another module";
        *g_hooks[i].orig = (void *)real;
    }
    for (i = 0; i < HOOK_COUNT; i++) {
        DWORD old;
        if (!VirtualProtect(g_hooks[i].slot, 4, PAGE_READWRITE, &old)) return "VirtualProtect failed";
        InterlockedExchange((volatile LONG *)g_hooks[i].slot, (LONG)(uintptr_t)g_hooks[i].hook);
        VirtualProtect(g_hooks[i].slot, 4, old, &old);
    }
    return NULL;
}

static int layout_ok(void)
{
    uint8_t *b = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    return (uintptr_t)b == 0x400000u &&
        nt->FileHeader.TimeDateStamp == EXE_TIMESTAMP &&
        nt->OptionalHeader.SizeOfImage == EXE_IMAGE_SIZE &&
        !memcmp((const void *)SITE_VOLUME_CALL, k_site_volume, sizeof k_site_volume);
}

/* ------------------------------------------------------------ settings and log */
static void logf_(const char *fmt, ...)
{
    char line[600];
    FILE *f;
    int n;
    va_list ap;
    if (g_log_full || !g_log_path[0]) return;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof line) n = (int)sizeof line - 1;
    if (g_log_bytes + n + 1 > SSR_LOG_MAX) {
        g_log_full = 1;
        lstrcpyA(line, "log size limit reached; further lines suppressed");
        n = (int)strlen(line);
    }
    f = fopen(g_log_path, "a");
    if (!f) return;
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
    g_log_bytes += n + 1;
}

static void ini_text(const char *ini, const char *section, const char *key, char *buf, size_t cap)
{
    char *s = buf, *e;
    GetPrivateProfileStringA(section, key, "", buf, (DWORD)cap, ini);
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
    memmove(buf, s, strlen(s) + 1);
}

static int ini_flag(const char *ini, const char *section, const char *key, int def)
{
    char b[32];
    ini_text(ini, section, key, b, sizeof b);
    if (!strcmp(b, "0")) return 0;
    if (!strcmp(b, "1")) return 1;
    return def;
}

static double ini_number(const char *ini, const char *section, const char *key, double def,
                         double min, double max)
{
    char b[32], *e = NULL;
    double v;
    ini_text(ini, section, key, b, sizeof b);
    if (!b[0]) return def;
    v = strtod(b, &e);
    if (e == b || *e != '\0' || !(v >= min && v <= max)) return def;
    return v;
}

static void load_settings(void)
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof ini, "%s%s", g_dir, SSR_INI);
    g_enabled = ini_flag(ini, "Mod", "Enabled", 1);
    g_fade_ms = ini_number(ini, "Mod", "FadeInMs", 30.0, 0.0, 1000.0);
    g_up = ini_flag(ini, "Mod", "Upshifts", 1);
    g_down = ini_flag(ini, "Mod", "Downshifts", 1);
    g_search_m = (float)ini_number(ini, "Advanced", "SearchMeters", 2.0, 0.5, 5.0);
    g_match_m = (float)ini_number(ini, "Advanced", "MatchMeters", 0.25, 0.05, 1.0);
    g_log = ini_flag(ini, "Advanced", "Log", 0);
}

/* ------------------------------------------------------------ .sfx scan */
/* The game parses a car's .sfx each time the player goes from the menus into
 * a session (README rule 22), so the files can change while the game runs.
 * Each scan fills g_scan (worker only); merge_engine_names() then adds its new
 * names to g_names under g_cs. */
static EngineName g_scan[MAX_NAMES];
static unsigned g_scan_count;

static void stage_engine_name(const char *rel, int kind)
{
    unsigned i;
    for (i = 0; i < g_scan_count; i++)
        if (!strcmp(g_scan[i].rel, rel)) return;
    if (g_scan_count >= MAX_NAMES) return;
    lstrcpynA(g_scan[g_scan_count].rel, rel, sizeof g_scan[0].rel);
    g_scan[g_scan_count].kind = kind;
    g_scan_count++;
}

/* Names are only ever added: until the next load a car can still be playing
 * files that its edited .sfx no longer lists. When something was added, every
 * voice re-checks its file, so a layer bound before the scan is recognised.
 * Returns the number of names added. */
static unsigned merge_engine_names(void)
{
    unsigned i, j, added = 0;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_scan_count && g_name_count < MAX_NAMES; i++) {
        for (j = 0; j < g_name_count && strcmp(g_names[j].rel, g_scan[i].rel); j++)
            ;
        if (j < g_name_count) continue;
        g_names[g_name_count++] = g_scan[i];
        added++;
    }
    if (added)
        for (i = 0; i < MAX_HANDLES; i++) g_handles[i].kind = -1;
    InterlockedExchange(&g_names_ready, 1);
    LeaveCriticalSection(&g_cs);
    return added;
}

static void scan_sfx_file(const char *path, unsigned *engine_lines)
{
    char line[512], rel[96];
    FILE *f = fopen(path, "r");
    int kind;
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if ((kind = ssr_parse_engine_line(line, rel, sizeof rel)) != 0) {
            stage_engine_name(rel, kind);
            (*engine_lines)++;
        }
    fclose(f);
}

static void scan_dir(const char *dir, int depth, unsigned *files, unsigned *engine_lines, unsigned *visited)
{
    char pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (depth > 8 || *visited > 200000u) return;
    if (snprintf(pattern, sizeof pattern, "%s*", dir) >= (int)sizeof pattern) return;
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        size_t n = strlen(fd.cFileName);
        (*visited)++;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (snprintf(path, sizeof path, "%s%s\\", dir, fd.cFileName) < (int)sizeof path)
                scan_dir(path, depth + 1, files, engine_lines, visited);
        } else if (n > 4 && !_stricmp(fd.cFileName + n - 4, ".sfx")) {
            if (snprintf(path, sizeof path, "%s%s", dir, fd.cFileName) < (int)sizeof path) {
                scan_sfx_file(path, engine_lines);
                (*files)++;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* root ends with '\'. Returns the number of engine files new to the list. */
static unsigned scan_engine_names_in(const char *root, int rescan)
{
    unsigned files = 0, lines = 0, visited = 0, added;
    double t0 = now_ms();
    g_scan_count = 0;
    scan_dir(root, 0, &files, &lines, &visited);
    added = merge_engine_names();
    if (rescan)
        logf_("engine sounds re-read at session start: %u .sfx files, %u new engine files (%u in total, %.0f ms)",
              files, added, g_name_count, now_ms() - t0);
    else
        logf_("engine sounds: %u .sfx files, %u engine-layer lines, %u distinct files (%.0f ms)",
              files, lines, g_name_count, now_ms() - t0);
    return added;
}

static void scan_engine_names(int rescan)
{
    char root[MAX_PATH], *slash;
    DWORD n = GetModuleFileNameA(NULL, root, sizeof root);
    if (!n || n >= sizeof root || !(slash = strrchr(root, '\\'))) {
        if (!rescan) logf_("engine sounds: cannot resolve the game folder; mod inactive");
        return;
    }
    slash[1] = '\0';
    scan_engine_names_in(root, rescan);
}

/* ------------------------------------------------------------ worker */
/* The player's own car: broker vehicle snapshot controller_role == 0. */
static LONG g_human_slot = -1;     /* worker only */

static void refresh_human_slot(F1CBrokerApi *api, F1CBrokerToken token)
{
    F1CBrokerStats stats;
    F1CBrokerVehicleHandle handles[VEH_SLOTS];
    uint32_t count = 0, i;
    LONG found = -1;
    memset(&stats, 0, sizeof stats);
    stats.structure_size = sizeof stats;
    if (api->get_stats(&stats) == F1C_BROKER_OK && stats.session_active && stats.session_generation &&
        api->get_vehicle_handles(token, stats.session_generation, handles, VEH_SLOTS, &count) == F1C_BROKER_OK) {
        if (count > VEH_SLOTS) count = VEH_SLOTS;
        for (i = 0; i < count && found < 0; i++) {
            F1CBrokerVehicleSnapshot snap;
            if (handles[i].slot >= VEH_SLOTS) continue;
            memset(&snap, 0, sizeof snap);
            snap.structure_size = sizeof snap;
            if (api->get_vehicle_snapshot(token, handles[i], &snap) == F1C_BROKER_OK &&
                snap.controller_role == 0)
                found = (LONG)handles[i].slot;
        }
    }
    if (found != g_human_slot) logf_("your car: slot %ld", (long)found);
    if (found >= 0 && g_human_slot < 0) scan_engine_names(1);   /* a session was loaded */
    g_human_slot = found;
}

/* The car being heard as the main car: the one the camera is on (broker
 * session snapshot spectated_vehicle_slot, the game's 0x7C54DC; switched with
 * +/- in game), else the player's own car. A change re-arms shift detection
 * so a different car's gear never counts as a shift. */
static void refresh_focus_slot(F1CBrokerApi *api, F1CBrokerToken token)
{
    LONG focus = g_human_slot;
    F1CBrokerStats stats;
    F1CBrokerSessionSnapshot session;
    memset(&stats, 0, sizeof stats);
    stats.structure_size = sizeof stats;
    memset(&session, 0, sizeof session);
    session.structure_size = sizeof session;
    if (api->get_session_snapshot && api->get_stats(&stats) == F1C_BROKER_OK && stats.session_active &&
        stats.session_generation &&
        api->get_session_snapshot(token, stats.session_generation, &session) == F1C_BROKER_OK &&
        session.session_active && session.spectated_vehicle_slot >= 0 &&
        session.spectated_vehicle_slot < VEH_SLOTS)
        focus = session.spectated_vehicle_slot;
    if (InterlockedExchange(&g_focus_slot, focus) != focus) {
        InterlockedExchange(&g_rearm, 1);
        logf_("following car slot %ld%s", (long)focus,
              focus < 0 ? " (no session)" : focus == g_human_slot ? " (your car)" : " (viewed car)");
    }
}

static void log_shift_stats(void)
{
    double now = now_ms();
    unsigned i;
    for (i = 0; i < MAX_STATS; i++) {
        ShiftStat s;
        EnterCriticalSection(&g_cs);
        s = g_stats[i];
        if (s.pending == 1 && now - s.at > STATS_WINDOW_MS) g_stats[i].pending = 0;
        LeaveCriticalSection(&g_cs);
        if (s.pending != 1 || now - s.at <= STATS_WINDOW_MS || !g_log) continue;
        if (s.foreign_seen)
            logf_("car %d shift %d -> %d: restarted %u cockpit + %u outside layers; its sounds max %.2f m "
                  "from it, nearest other engine sound %.1f m", s.slot, s.from, s.to, s.reset_inside,
                  s.reset_outside, s.max_owned_m, s.min_foreign_m);
        else
            logf_("car %d shift %d -> %d: restarted %u cockpit + %u outside layers; its sounds max %.2f m "
                  "from it, no other engine sound playing", s.slot, s.from, s.to, s.reset_inside,
                  s.reset_outside, s.max_owned_m);
    }
}

static DWORD WINAPI worker(void *unused)
{
    F1CBrokerApi api;
    F1CBrokerToken token = 0;
    F1CBrokerDiscoveryResult d;
    DWORD started = GetTickCount(), next_human, next_focus;
    int have_broker = 0;
    (void)unused;
    logf_("--- %s %s pid %lu: %s", SSR_ID, SSR_VERSION, (unsigned long)GetCurrentProcessId(), g_status);
    logf_("settings: FadeInMs=%.0f Upshifts=%d Downshifts=%d SearchMeters=%.2f MatchMeters=%.2f", g_fade_ms, g_up, g_down,
          (double)g_search_m, (double)g_match_m);
    if (!g_active) return 0;
    scan_engine_names(0);
    do {
        memset(&api, 0, sizeof api);
        api.structure_size = sizeof api;
        d = f1c_broker_discover(11u, F1C_BROKER_ABI_VERSION, F1C_BROKER_CAP_VEHICLE_SNAPSHOTS, &api);
        if (d != F1C_BROKER_DISCOVERY_NOT_FOUND && d != F1C_BROKER_DISCOVERY_QUERY_FAILED) break;
        Sleep(50);
    } while (GetTickCount() - started < 10000u);
    if (d == F1C_BROKER_DISCOVERY_OK && api.register_client(g_module, SSR_ID, &token) == F1C_BROKER_OK) {
        have_broker = 1;
        logf_("broker ABI %lu: your car from controller_role, viewed car from the session snapshot%s",
              (unsigned long)api.negotiated_abi,
              api.get_session_snapshot ? "" : " (unavailable: following your car only)");
    } else {
        if (d == F1C_BROKER_DISCOVERY_OK) f1c_broker_release(&api);
        InterlockedExchange(&g_focus_slot, 0);
        logf_("broker unavailable (discovery %d): following slot 0 (your car in single player)", (int)d);
    }
    next_human = next_focus = GetTickCount();
    for (;;) {
        if (have_broker && (LONG)(GetTickCount() - next_human) >= 0) {
            refresh_human_slot(&api, token);
            next_human = GetTickCount() + 1000u;
        }
        if (have_broker && (LONG)(GetTickCount() - next_focus) >= 0) {
            refresh_focus_slot(&api, token);
            next_focus = GetTickCount() + 100u;
        }
        log_shift_stats();
        Sleep(50);
    }
    return 0;
}

/* ------------------------------------------------------------ entry */
static volatile LONG g_loader_init_state;

/* Exclusive-host contract (generation 1+): accepted by Lightloader 2.0 and
 * newer, as Pit Stop Failures 1.0.5, Safety Car and Mechanical Failures do. */
static BOOL validate_loader_host(const LightLoaderProtectedHostV2 *host)
{
    wchar_t path[MAX_PATH];
    wchar_t *base;
    union { FARPROC raw; LightLoaderProofProc typed; } proof = {0};
    if (!host || host->size < sizeof(*host) ||
        host->magic != LLP_ABI_MAGIC ||
        host->process_id != GetCurrentProcessId() ||
        !host->loader_module ||
        host->loader_module != GetModuleHandleW(L"dinput8.dll") ||
        host->abi_version < LLP_MIN_EXCLUSIVE_ABI ||
        host->product_id != LLP_PRODUCT_ID ||
        !(host->capabilities & LLP_CAP_EXCLUSIVE_HOST) ||
        host->compatibility_generation < LLP_EXCLUSIVE_GENERATION ||
        host->seal != LightLoaderHostSeal(host))
        return FALSE;
    if (!GetModuleFileNameW(host->loader_module, path, MAX_PATH)) return FALSE;
    path[MAX_PATH - 1] = L'\0';
    base = wcsrchr(path, L'\\');
    if (_wcsicmp(base ? base + 1 : path, L"dinput8.dll") != 0) return FALSE;
    proof.raw = GetProcAddress(host->loader_module, LLP_PROOF_NAME);
    return proof.typed && proof.typed(LightLoaderProofChallenge(host)) == host->proof;
}

__declspec(dllexport) const char *F1CModId(void) { return SSR_ID; }

__declspec(dllexport) BOOL __cdecl rfe_1d3b7a91(const LightLoaderProtectedHostV2 *host)
{
    HMODULE pinned;
    HANDLE thread;
    LARGE_INTEGER freq;
    char prev[MAX_PATH], *slash;
    const char *failure;
    DWORD n;
    if (InterlockedCompareExchange(&g_loader_init_state, 1, 0) != 0) return FALSE;
    if (!validate_loader_host(host)) { InterlockedExchange(&g_loader_init_state, 0); return FALSE; }
    QueryPerformanceFrequency(&freq);
    g_qpc_per_ms = (double)freq.QuadPart / 1000.0;
    n = GetModuleFileNameA(g_module, g_dir, sizeof g_dir);
    if (!n || n >= sizeof g_dir || !(slash = strrchr(g_dir, '\\'))) g_dir[0] = '\0';
    else slash[1] = '\0';
    InitializeCriticalSection(&g_cs);
    load_settings();
    if (g_log) {                       /* Log=0 (default): no log file is written or touched */
        snprintf(g_log_path, sizeof g_log_path, "%s%s", g_dir, SSR_LOG);
        snprintf(prev, sizeof prev, "%s%s", g_dir, SSR_LOG_PREV);
        if (GetFileAttributesA(g_log_path) != INVALID_FILE_ATTRIBUTES)
            MoveFileExA(g_log_path, prev, MOVEFILE_REPLACE_EXISTING);
    }
    if (!g_enabled) {
        lstrcpyA(g_status, "disabled in " SSR_INI " (Enabled=0); nothing installed");
    } else if (!layout_ok()) {
        lstrcpyA(g_status, "executable layout or sound call site not recognised; nothing installed");
    } else if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   (LPCSTR)(void *)worker, &pinned)) {
        lstrcpyA(g_status, "could not pin module; nothing installed");
    } else if ((failure = install_hooks()) != NULL) {
        snprintf(g_status, sizeof g_status, "NOT active (%s); nothing installed", failure);
    } else {
        g_active = 1;
        lstrcpyA(g_status, "active");
    }
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = (HMODULE)instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
