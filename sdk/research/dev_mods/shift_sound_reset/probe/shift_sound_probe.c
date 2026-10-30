/*
 * shift_sound_probe.c - read-only research probe for the Shift Sound Reset mod.
 *
 * Question it answers: which Miles sample handles are the PLAYER car's engine
 * layers (cockpit and outside), and what happens to them at a gear change.
 *
 * What it does (observation only; the game hears exactly what it would hear
 * without the probe):
 *   - Replaces the game's import-table entries for a few Miles Sound System
 *     calls (Mss32.dll) with wrappers that call the original first, then
 *     record the call. No game code bytes are patched.
 *   - For each sample handle it records: the file loaded into it, loop count,
 *     playback-rate calls (pitch), position sets, start/stop, volume, and the
 *     correlation between its playback rate and the player's engine RPM.
 *     Player engine layers should correlate near 1.0; AI engines should not.
 *   - At every player gear change it records each recently pitched sample's
 *     current position in its recording (ms) - the continuous playback the
 *     mod will reset.
 *   - A worker thread writes a summary every 2 s to shift_sound_probe_log.txt
 *     beside the DLL (bounded to 8 MB). Hooks never do file I/O.
 *
 * Static evidence this relies on (DERIVED, read from the executable's
 * disassembly 2026-10-26, see ../README.md):
 *   - 0x405B90 is the sound-object update (thiscall, esi = sound object). Its
 *     only AIL_set_sample_playback_rate call returns to 0x405CF4 and its
 *     AIL_set_sample_volume_levels call returns to 0x405CC7. Only at those
 *     two return addresses is esi read as the sound object.
 *   - Player slot: broker vehicle snapshot controller_role == 0 (reviewed
 *     broker field). Gear 0x39FC (FACT) and RPM 0x39A0 (CONFIRMED) are read
 *     from that slot.
 *
 * Entry: Lightloader protected host export rfe_1d3b7a91 (same contract as
 * ESC Pause Menu). DllMain is inert. The hooks stay for the life of the
 * process (the module is pinned); restart the game to remove the probe.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <wchar.h>
#include "protected_plugin_abi.h"
#include "f1c_broker.h"

#define PROBE_ID      "shift_sound_probe"
#define PROBE_VERSION "0.1.1"
#define LOG_NAME      PROBE_ID "_log.txt"
#define LOG_PREV      PROBE_ID "_log_prev.txt"
#define LOG_MAX_BYTES (8L * 1024L * 1024L)
#define SUMMARY_MS    2000u
#define SLOT_POLL_MS  1000u
#define RECENT_MS     250u

/* Layout gate: the SDK target and the patched co-op variant share these. */
#define EXE_TIMESTAMP  0x3E9C24F6u
#define EXE_IMAGE_SIZE 0x005FA000u
#define SITE_VOLUME_CALL 0x00405CC1u
#define SITE_RATE_CALL   0x00405CEEu
#ifndef SITE_VOLUME_RET            /* overridable by thunk_test.c only */
#define SITE_VOLUME_RET  0x00405CC7u
#endif
#ifndef SITE_RATE_RET
#define SITE_RATE_RET    0x00405CF4u
#endif
static const uint8_t k_site_volume[6] = {0xFF, 0x15, 0xB8, 0xE3, 0x59, 0x00};
static const uint8_t k_site_rate[6]   = {0xFF, 0x15, 0xBC, 0xE3, 0x59, 0x00};

#define VEH_BASE   0x005EC6D8u
#define VEH_STRIDE 0x48C0u
#define VEH_SLOTS  104
#define OFF_RPM    0x39A0u   /* EngineRPM, CONFIRMED */
#define OFF_GEAR   0x39FCu   /* CurrentGear, FACT */
#define OBJ_SNAP_BYTES 0x60u

/* ------------------------------------------------------------ Miles types */
typedef void *HSAMPLE;
typedef void *(__stdcall *file_read_fn)(const char *, void *);
typedef int32_t (__stdcall *set_file_fn)(HSAMPLE, const void *, int32_t);
typedef void (__stdcall *h_i32_fn)(HSAMPLE, int32_t);
typedef void (__stdcall *h_u32_fn)(HSAMPLE, uint32_t);
typedef void (__stdcall *h_fn)(HSAMPLE);
typedef void (__stdcall *vol_fn)(HSAMPLE, float, float);
typedef void (__stdcall *ms_query_fn)(HSAMPLE, int32_t *, int32_t *);

static file_read_fn g_file_read;
static set_file_fn g_set_file;
static h_i32_fn g_set_loop, g_set_rate;
static h_u32_fn g_set_pos;
static h_fn g_start, g_resume, g_end, g_stop, g_release;
static vol_fn g_set_volume;
static ms_query_fn g_ms_query;     /* optional, from Mss32 exports */

/* ------------------------------------------------------------ records */
typedef struct HandleInfo {
    HSAMPLE h;
    const void *image;
    char file[72];
    uint32_t wav_rate, wav_channels, wav_bits, wav_data;
    int32_t loop_count;
    int has_loop, file_logged, released;
    uint32_t set_file_caller;
    uint32_t obj;                       /* sound object (esi) at 0x405B90 */
    uint32_t obj_logged;                /* object whose bytes were logged */
    uint8_t obj_bytes[OBJ_SNAP_BYTES];
    int obj_bytes_valid;
    uint32_t f14, f18;
    float f44, f48, f4c;
    DWORD last_rate_tick;
    /* window counters, reset after each summary */
    uint32_t rate_calls, pos_calls, starts, resumes, ends, stops, vol_calls;
    int32_t rate_min, rate_max, rate_last;
    uint32_t pos_last, pos_caller;
    float vol_l, vol_r;
    double n, sx, sy, sxx, syy, sxy;   /* rate vs player rpm */
} HandleInfo;

#define MAX_HANDLES 256
static HandleInfo g_handles[MAX_HANDLES];
static HandleInfo g_copy[MAX_HANDLES];

typedef struct FileMap { const void *image; char name[72]; } FileMap;
#define MAX_FILES 512
static FileMap g_files[MAX_FILES];
static unsigned g_file_next;

#define EVENT_HANDLES 24
typedef struct GearEvent {
    volatile LONG ready;
    DWORD tick;
    int from, to;
    float rpm;
    unsigned count;
    HSAMPLE h[EVENT_HANDLES];
    int32_t pos_ms[EVENT_HANDLES], total_ms[EVENT_HANDLES], rate[EVENT_HANDLES];
} GearEvent;
#define MAX_EVENTS 64
static GearEvent g_events[MAX_EVENTS];
static LONG g_event_write, g_event_read;
static volatile LONG g_events_dropped;

static CRITICAL_SECTION g_cs;
static volatile LONG g_player_slot = -1;
static int g_last_gear = INT_MIN;
static int g_obj_ok;
static HMODULE g_module;
static char g_log_path[MAX_PATH], g_status[512];
static long g_log_bytes;
static int g_log_full;

/* ------------------------------------------------------------ helpers */
static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t a = (uintptr_t)p;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return a + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static void tail_copy(char *out, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) s += n - (cap - 1);
    lstrcpynA(out, s, (int)cap);
}

/* WAV header: format and data size, bounded to the first 4 KB. */
static void parse_wav(HandleInfo *e, const void *image)
{
    const uint8_t *p = (const uint8_t *)image;
    uint32_t off = 12;
    e->wav_rate = e->wav_channels = e->wav_bits = e->wav_data = 0;
    if (!readable(p, 4096) || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return;
    while (off + 8 <= 4096) {
        uint32_t size;
        memcpy(&size, p + off + 4, 4);
        if (!memcmp(p + off, "fmt ", 4) && off + 24 <= 4096) {
            uint16_t ch, bits;
            memcpy(&ch, p + off + 10, 2);
            memcpy(&e->wav_rate, p + off + 12, 4);
            memcpy(&bits, p + off + 22, 2);
            e->wav_channels = ch;
            e->wav_bits = bits;
        } else if (!memcmp(p + off, "data", 4)) {
            e->wav_data = size;
            return;
        }
        off += 8 + size + (size & 1u);
    }
}

static HandleInfo *find_handle(HSAMPLE h, int add)
{
    unsigned i, free_slot = MAX_HANDLES;
    for (i = 0; i < MAX_HANDLES; i++) {
        if (g_handles[i].h == h) return &g_handles[i];
        if (!g_handles[i].h && free_slot == MAX_HANDLES) free_slot = i;
    }
    if (!add || free_slot == MAX_HANDLES) return NULL;
    memset(&g_handles[free_slot], 0, sizeof g_handles[free_slot]);
    g_handles[free_slot].h = h;
    g_handles[free_slot].rate_min = INT_MAX;
    g_handles[free_slot].rate_max = INT_MIN;
    return &g_handles[free_slot];
}

static const char *file_for(const void *image)
{
    unsigned i;
    for (i = 0; i < MAX_FILES; i++)
        if (g_files[i].image == image && g_files[i].name[0]) return g_files[i].name;
    return NULL;
}

static int player_slot(void)
{
    LONG s = g_player_slot;
    return (s >= 0 && s < VEH_SLOTS) ? (int)s : -1;
}

/* ------------------------------------------------------------ hooks */
static void *__stdcall hk_file_read(const char *name, void *dest)
{
    void *r = g_file_read(name, dest);
    if (r && name && readable(name, 1)) {
        EnterCriticalSection(&g_cs);
        g_files[g_file_next].image = r;
        tail_copy(g_files[g_file_next].name, sizeof g_files[0].name, name);
        g_file_next = (g_file_next + 1u) % MAX_FILES;
        LeaveCriticalSection(&g_cs);
    }
    return r;
}

static int32_t __stdcall hk_set_file(HSAMPLE h, const void *image, int32_t block)
{
    int32_t r = g_set_file(h, image, block);
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    HandleInfo *e;
    EnterCriticalSection(&g_cs);
    e = find_handle(h, 1);
    if (e) {
        const char *name = file_for(image);
        e->image = image;
        e->released = 0;
        e->file_logged = 0;
        e->set_file_caller = caller;
        lstrcpynA(e->file, name ? name : "?", sizeof e->file);
        parse_wav(e, image);
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

static void __stdcall hk_set_loop(HSAMPLE h, int32_t count)
{
    HandleInfo *e;
    g_set_loop(h, count);
    EnterCriticalSection(&g_cs);
    e = find_handle(h, 1);
    if (e) { e->loop_count = count; e->has_loop = 1; }
    LeaveCriticalSection(&g_cs);
}

static void __stdcall hk_set_pos(HSAMPLE h, uint32_t pos)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    HandleInfo *e;
    g_set_pos(h, pos);
    EnterCriticalSection(&g_cs);
    e = find_handle(h, 1);
    if (e) { e->pos_calls++; e->pos_last = pos; e->pos_caller = caller; }
    LeaveCriticalSection(&g_cs);
}

#define COUNT_HOOK(fn_name, orig, field)                      \
    static void __stdcall fn_name(HSAMPLE h)                  \
    {                                                         \
        HandleInfo *e;                                        \
        orig(h);                                              \
        EnterCriticalSection(&g_cs);                          \
        e = find_handle(h, 1);                                \
        if (e) e->field++;                                    \
        LeaveCriticalSection(&g_cs);                          \
    }
COUNT_HOOK(hk_start, g_start, starts)
COUNT_HOOK(hk_resume, g_resume, resumes)
COUNT_HOOK(hk_end, g_end, ends)
COUNT_HOOK(hk_stop, g_stop, stops)

static void __stdcall hk_release(HSAMPLE h)
{
    HandleInfo *e;
    g_release(h);
    EnterCriticalSection(&g_cs);
    e = find_handle(h, 0);
    if (e) e->released = 1;
    LeaveCriticalSection(&g_cs);
}

static void read_object(HandleInfo *e, uint32_t esi)
{
    const uint8_t *o = (const uint8_t *)(uintptr_t)esi;
    if (!readable(o, OBJ_SNAP_BYTES)) return;
    e->obj = esi;
    memcpy(&e->f14, o + 0x14, 4);
    memcpy(&e->f18, o + 0x18, 4);
    memcpy(&e->f44, o + 0x44, 4);
    memcpy(&e->f48, o + 0x48, 4);
    memcpy(&e->f4c, o + 0x4C, 4);
    if (e->obj_logged != esi) {
        memcpy(e->obj_bytes, o, OBJ_SNAP_BYTES);
        e->obj_bytes_valid = 1;
    }
}

/* Gear change: reserve an event, snapshot the recently pitched handles, then
 * query their positions outside our lock (never hold g_cs inside Miles). */
static void gear_event(DWORD now, int from, int to, float rpm)
{
    GearEvent *ev;
    HSAMPLE hs[EVENT_HANDLES];
    int32_t rates[EVENT_HANDLES];
    unsigned i, n = 0;
    LONG w = g_event_write;
    if (w - g_event_read >= MAX_EVENTS) { InterlockedIncrement(&g_events_dropped); return; }
    for (i = 0; i < MAX_HANDLES && n < EVENT_HANDLES; i++) {
        if (g_handles[i].h && !g_handles[i].released &&
            now - g_handles[i].last_rate_tick <= RECENT_MS) {
            hs[n] = g_handles[i].h;
            rates[n] = g_handles[i].rate_last;
            n++;
        }
    }
    ev = &g_events[w % MAX_EVENTS];
    ev->ready = 0;
    ev->tick = now; ev->from = from; ev->to = to; ev->rpm = rpm; ev->count = n;
    for (i = 0; i < n; i++) {
        ev->h[i] = hs[i];
        ev->rate[i] = rates[i];
        ev->pos_ms[i] = ev->total_ms[i] = -1;
    }
    InterlockedIncrement(&g_event_write);
    LeaveCriticalSection(&g_cs);
    if (g_ms_query)
        for (i = 0; i < n; i++) g_ms_query(ev->h[i], &ev->total_ms[i], &ev->pos_ms[i]);
    InterlockedExchange(&ev->ready, 1);
    EnterCriticalSection(&g_cs);
}

/* Called by the naked thunks below with the caller's esi and return address. */
void ssp_rate_impl(uint32_t esi, uint32_t ret, HSAMPLE h, int32_t rate)
    __asm__("ssp_rate_impl_sym") __attribute__((used));
void ssp_rate_impl(uint32_t esi, uint32_t ret, HSAMPLE h, int32_t rate)
{
    DWORD now;
    int slot, gear = INT_MIN;
    float rpm = 0.0f;
    HandleInfo *e;
    g_set_rate(h, rate);
    now = GetTickCount();
    slot = player_slot();
    if (slot >= 0) {
        uintptr_t v = VEH_BASE + (uintptr_t)slot * VEH_STRIDE;
        rpm = *(volatile float *)(v + OFF_RPM);
        gear = *(volatile int32_t *)(v + OFF_GEAR);
    }
    EnterCriticalSection(&g_cs);
    e = find_handle(h, 1);
    if (e) {
        e->rate_calls++;
        e->rate_last = rate;
        if (rate < e->rate_min) e->rate_min = rate;
        if (rate > e->rate_max) e->rate_max = rate;
        e->last_rate_tick = now;
        if (g_obj_ok && ret == SITE_RATE_RET && esi) read_object(e, esi);
        if (rpm > 100.0f && rpm < 30000.0f && isfinite(rpm)) {
            double x = rpm, y = rate;
            e->n += 1.0; e->sx += x; e->sy += y;
            e->sxx += x * x; e->syy += y * y; e->sxy += x * y;
        }
    }
    if (gear != INT_MIN && gear >= -1 && gear <= 9 && gear != g_last_gear) {
        if (g_last_gear != INT_MIN) gear_event(now, g_last_gear, gear, rpm);
        g_last_gear = gear;
    }
    LeaveCriticalSection(&g_cs);
}

void ssp_volume_impl(uint32_t esi, uint32_t ret, HSAMPLE h, float left, float right)
    __asm__("ssp_volume_impl_sym") __attribute__((used));
void ssp_volume_impl(uint32_t esi, uint32_t ret, HSAMPLE h, float left, float right)
{
    HandleInfo *e;
    g_set_volume(h, left, right);
    EnterCriticalSection(&g_cs);
    e = find_handle(h, 1);
    if (e) {
        e->vol_calls++;
        e->vol_l = left;
        e->vol_r = right;
        if (g_obj_ok && ret == SITE_VOLUME_RET && esi) read_object(e, esi);
    }
    LeaveCriticalSection(&g_cs);
}

/* stdcall(h, rate): pass esi and the return address on to the cdecl impl. */
extern void ssp_rate_thunk(void) __asm__("ssp_rate_thunk_sym");
extern void ssp_volume_thunk(void) __asm__("ssp_volume_thunk_sym");
__asm__(
    ".text\n"
    "ssp_rate_thunk_sym:\n"
    "    pushl 8(%esp)\n"          /* rate */
    "    pushl 8(%esp)\n"          /* h */
    "    pushl 8(%esp)\n"          /* return address */
    "    pushl %esi\n"
    "    call ssp_rate_impl_sym\n"
    "    addl $16, %esp\n"
    "    ret $8\n"
    "ssp_volume_thunk_sym:\n"
    "    pushl 12(%esp)\n"         /* right */
    "    pushl 12(%esp)\n"         /* left */
    "    pushl 12(%esp)\n"         /* h */
    "    pushl 12(%esp)\n"         /* return address */
    "    pushl %esi\n"
    "    call ssp_volume_impl_sym\n"
    "    addl $20, %esp\n"
    "    ret $12\n");

/* ------------------------------------------------------------ IAT hooks */
typedef struct Hook { const char *name; void *hook; void **orig; DWORD *slot; } Hook;
static Hook g_hooks[] = {
    { "_AIL_file_read@8",                 (void *)hk_file_read,     (void **)&g_file_read,  NULL },
    { "_AIL_set_sample_file@12",          (void *)hk_set_file,      (void **)&g_set_file,   NULL },
    { "_AIL_set_sample_loop_count@8",     (void *)hk_set_loop,      (void **)&g_set_loop,   NULL },
    { "_AIL_set_sample_playback_rate@8",  (void *)ssp_rate_thunk,   (void **)&g_set_rate,   NULL },
    { "_AIL_set_sample_position@8",       (void *)hk_set_pos,       (void **)&g_set_pos,    NULL },
    { "_AIL_set_sample_volume_levels@12", (void *)ssp_volume_thunk, (void **)&g_set_volume, NULL },
    { "_AIL_start_sample@4",              (void *)hk_start,         (void **)&g_start,      NULL },
    { "_AIL_resume_sample@4",             (void *)hk_resume,        (void **)&g_resume,     NULL },
    { "_AIL_end_sample@4",                (void *)hk_end,           (void **)&g_end,        NULL },
    { "_AIL_stop_sample@4",               (void *)hk_stop,          (void **)&g_stop,       NULL },
    { "_AIL_release_sample_handle@4",     (void *)hk_release,       (void **)&g_release,    NULL },
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
    g_ms_query = (ms_query_fn)(void *)GetProcAddress(mss, "_AIL_sample_ms_position@12");
    return NULL;
}

static int layout_ok(void)
{
    uint8_t *b = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    return (uintptr_t)b == 0x400000u &&
        nt->FileHeader.TimeDateStamp == EXE_TIMESTAMP &&
        nt->OptionalHeader.SizeOfImage == EXE_IMAGE_SIZE;
}

/* ------------------------------------------------------------ logging */
static void logf_(const char *fmt, ...)
{
    char line[1400];
    FILE *f;
    int n;
    va_list ap;
    if (g_log_full || !g_log_path[0]) return;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof line) n = (int)sizeof line - 1;
    if (g_log_bytes + n + 1 > LOG_MAX_BYTES) {
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

static double corr(const HandleInfo *e)
{
    double vx = e->n * e->sxx - e->sx * e->sx, vy = e->n * e->syy - e->sy * e->sy;
    if (e->n < 8.0 || vx <= 0.0 || vy <= 0.0) return NAN;
    return (e->n * e->sxy - e->sx * e->sy) / sqrt(vx * vy);
}

static void write_summary(void)
{
    unsigned i, active = 0;
    int slot = player_slot(), gear = INT_MIN;
    float rpm = 0.0f;
    if (slot >= 0) {
        uintptr_t v = VEH_BASE + (uintptr_t)slot * VEH_STRIDE;
        rpm = *(volatile float *)(v + OFF_RPM);
        gear = *(volatile int32_t *)(v + OFF_GEAR);
    }
    EnterCriticalSection(&g_cs);
    memcpy(g_copy, g_handles, sizeof g_copy);
    for (i = 0; i < MAX_HANDLES; i++) {
        HandleInfo *e = &g_handles[i];
        if (!e->h) continue;
        e->file_logged = 1;
        if (e->obj_bytes_valid) { e->obj_logged = e->obj; e->obj_bytes_valid = 0; }
        e->rate_calls = e->pos_calls = e->starts = e->resumes = e->ends = e->stops = e->vol_calls = 0;
        e->rate_min = INT_MAX; e->rate_max = INT_MIN;
        e->n = e->sx = e->sy = e->sxx = e->syy = e->sxy = 0.0;
    }
    LeaveCriticalSection(&g_cs);

    for (i = 0; i < MAX_HANDLES; i++) {
        const HandleInfo *e = &g_copy[i];
        if (!e->h) continue;
        if (!e->file_logged && e->image)
            logf_("F h=%p image=%p file=%s fmt=%luHz/%luch/%lubit data=%lu (%.2fs) caller=0x%08lX",
                  e->h, e->image, e->file, (unsigned long)e->wav_rate, (unsigned long)e->wav_channels,
                  (unsigned long)e->wav_bits, (unsigned long)e->wav_data,
                  (e->wav_rate && e->wav_channels && e->wav_bits)
                      ? (double)e->wav_data / ((double)e->wav_rate * e->wav_channels * (e->wav_bits / 8.0)) : 0.0,
                  (unsigned long)e->set_file_caller);
        if (e->obj_bytes_valid) {
            char hex[OBJ_SNAP_BYTES * 3 + 1];
            unsigned k;
            for (k = 0; k < OBJ_SNAP_BYTES; k++) sprintf(hex + k * 3, "%02X ", e->obj_bytes[k]);
            logf_("O h=%p obj=0x%08lX bytes: %s", e->h, (unsigned long)e->obj, hex);
        }
        if (e->rate_calls || e->pos_calls || e->starts || e->ends || e->stops) active++;
    }
    if (!active) return;
    logf_("W t=%lu player_slot=%d gear=%d rpm=%.0f active=%u", (unsigned long)GetTickCount(), slot,
          gear == INT_MIN ? -99 : gear, rpm, active);
    for (i = 0; i < MAX_HANDLES; i++) {
        const HandleInfo *e = &g_copy[i];
        if (!e->h || !(e->rate_calls || e->pos_calls || e->starts || e->ends || e->stops)) continue;
        logf_("H h=%p obj=0x%08lX file=%s loop=%ld rate[n=%lu min=%ld max=%ld last=%ld] r_rpm=%.3f "
              "pos[n=%lu last=%lu from=0x%08lX] start=%lu resume=%lu end=%lu stop=%lu vol=%.3f/%.3f "
              "f14=%08lX f18=%08lX f44=%.2f f48=%.5f f4c=%.3f%s",
              e->h, (unsigned long)e->obj, e->file, e->has_loop ? (long)e->loop_count : -99L,
              (unsigned long)e->rate_calls, e->rate_calls ? (long)e->rate_min : 0L,
              e->rate_calls ? (long)e->rate_max : 0L, (long)e->rate_last, corr(e),
              (unsigned long)e->pos_calls, (unsigned long)e->pos_last, (unsigned long)e->pos_caller,
              (unsigned long)e->starts, (unsigned long)e->resumes, (unsigned long)e->ends,
              (unsigned long)e->stops, e->vol_l, e->vol_r, (unsigned long)e->f14, (unsigned long)e->f18,
              e->f44, e->f48, e->f4c, e->released ? " released" : "");
    }
}

static void write_events(void)
{
    while (g_event_read != g_event_write) {
        GearEvent *ev = &g_events[g_event_read % MAX_EVENTS];
        char buf[1200];
        int used = 0;
        unsigned i;
        if (!ev->ready) break;
        for (i = 0; i < ev->count && used < (int)sizeof buf - 64; i++)
            used += snprintf(buf + used, sizeof buf - (size_t)used, " [%p %ld/%ldms rate=%ld]",
                             ev->h[i], (long)ev->pos_ms[i], (long)ev->total_ms[i], (long)ev->rate[i]);
        buf[used] = '\0';
        logf_("G t=%lu gear %d -> %d rpm=%.0f pitched=%u%s", (unsigned long)ev->tick, ev->from, ev->to,
              ev->rpm, ev->count, buf);
        InterlockedIncrement(&g_event_read);
    }
    if (g_events_dropped) logf_("G dropped=%ld", (long)InterlockedExchange(&g_events_dropped, 0));
}

/* ------------------------------------------------------------ worker */
static void refresh_player_slot(F1CBrokerApi *api, F1CBrokerToken token)
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
    if (InterlockedExchange(&g_player_slot, found) != found)
        logf_("player slot -> %ld", (long)found);
}

static DWORD WINAPI worker(void *unused)
{
    F1CBrokerApi api;
    F1CBrokerToken token = 0;
    F1CBrokerDiscoveryResult d;
    DWORD started = GetTickCount(), next_summary, next_slot;
    int have_broker = 0;
    (void)unused;
    logf_("--- %s %s pid %lu: %s", PROBE_ID, PROBE_VERSION, (unsigned long)GetCurrentProcessId(), g_status);
    do {
        memset(&api, 0, sizeof api);
        api.structure_size = sizeof api;
        d = f1c_broker_discover(11u, F1C_BROKER_ABI_VERSION, F1C_BROKER_CAP_VEHICLE_SNAPSHOTS, &api);
        if (d != F1C_BROKER_DISCOVERY_NOT_FOUND && d != F1C_BROKER_DISCOVERY_QUERY_FAILED) break;
        Sleep(50);
    } while (GetTickCount() - started < 10000u);
    if (d == F1C_BROKER_DISCOVERY_OK && api.register_client(g_module, PROBE_ID, &token) == F1C_BROKER_OK) {
        have_broker = 1;
        logf_("broker ABI %lu: player slot from controller_role", (unsigned long)api.negotiated_abi);
    } else {
        if (d == F1C_BROKER_DISCOVERY_OK) f1c_broker_release(&api);
        InterlockedExchange(&g_player_slot, 0);
        logf_("broker unavailable (discovery %d): assuming player slot 0", (int)d);
    }
    next_summary = next_slot = GetTickCount();
    for (;;) {
        DWORD now = GetTickCount();
        if (have_broker && (LONG)(now - next_slot) >= 0) {
            refresh_player_slot(&api, token);
            next_slot = now + SLOT_POLL_MS;
        }
        write_events();
        if ((LONG)(now - next_summary) >= 0) {
            write_summary();
            next_summary = now + SUMMARY_MS;
        }
        Sleep(50);
    }
    return 0;
}

/* ------------------------------------------------------------ entry */
static volatile LONG g_loader_init_state;

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
        /* Exclusive-host contract (generation 1+), as Pit Stop Failures 1.0.5,
         * Safety Car and Mechanical Failures accept. Hub 2 is NOT required:
         * the owner's Lightloader (Sep 2026, separate broker DLL) does not
         * offer it and refused 0.1.0, which demanded it. */
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

static void sibling_path(char *out, size_t cap, const char *name)
{
    char *slash;
    DWORD n = GetModuleFileNameA(g_module, out, (DWORD)cap);
    if (!n || n >= cap || !(slash = strrchr(out, '\\'))) { lstrcpynA(out, name, (int)cap); return; }
    lstrcpynA(slash + 1, name, (int)(cap - (size_t)(slash + 1 - out)));
}

__declspec(dllexport) const char *F1CModId(void) { return PROBE_ID; }

__declspec(dllexport) BOOL __cdecl rfe_1d3b7a91(const LightLoaderProtectedHostV2 *host)
{
    HMODULE pinned;
    HANDLE thread;
    char prev[MAX_PATH];
    const char *failure;
    if (InterlockedCompareExchange(&g_loader_init_state, 1, 0) != 0) return FALSE;
    if (!validate_loader_host(host)) { InterlockedExchange(&g_loader_init_state, 0); return FALSE; }
    sibling_path(g_log_path, sizeof g_log_path, LOG_NAME);
    sibling_path(prev, sizeof prev, LOG_PREV);
    if (GetFileAttributesA(g_log_path) != INVALID_FILE_ATTRIBUTES)
        MoveFileExA(g_log_path, prev, MOVEFILE_REPLACE_EXISTING);
    InitializeCriticalSection(&g_cs);
    if (!layout_ok()) {
        lstrcpyA(g_status, "executable layout not recognised; no hooks installed");
    } else {
        g_obj_ok = !memcmp((const void *)SITE_RATE_CALL, k_site_rate, sizeof k_site_rate) &&
                   !memcmp((const void *)SITE_VOLUME_CALL, k_site_volume, sizeof k_site_volume);
        /* The hooks point into this module: pin it before redirecting. */
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                (LPCSTR)(void *)worker, &pinned))
            lstrcpyA(g_status, "could not pin module; no hooks installed");
        else if ((failure = install_hooks()) != NULL)
            snprintf(g_status, sizeof g_status, "hooks NOT installed (%s)", failure);
        else
            snprintf(g_status, sizeof g_status, "%u Miles imports observed; sound-object sites %s; ms query %s",
                     (unsigned)HOOK_COUNT, g_obj_ok ? "verified" : "NOT verified (object fields off)",
                     g_ms_query ? "available" : "missing");
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
