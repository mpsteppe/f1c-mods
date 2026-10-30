/* thunk_test.c - headless check of the probe's esi-capturing thunks.
 *
 * Calls each thunk the way the game's sound-object update does (stdcall
 * arguments pushed, esi = sound object, call through a pointer) and checks:
 * the stack pointer is identical before and after, esi survives, the original
 * Miles function received the exact arguments, and the probe recorded the
 * call with the object fields read through esi.
 *
 * Build and run (output in %TEMP%):
 *   gcc -O2 -Wall -Wextra -Werror -std=gnu11 -I..\..\..\..\tools\sdk
 *       -o %TEMP%\shift_sound_probe\thunk_test.exe thunk_test.c
 *       ..\..\..\..\tools\sdk\f1c_broker.c
 */
#include <stdint.h>
extern char ssp_test_ret_rate[] __asm__("ssp_test_ret_rate_sym");
extern char ssp_test_ret_volume[] __asm__("ssp_test_ret_volume_sym");
#define SITE_RATE_RET   ((uint32_t)(uintptr_t)ssp_test_ret_rate)
#define SITE_VOLUME_RET ((uint32_t)(uintptr_t)ssp_test_ret_volume)
#include "shift_sound_probe.c"

static HSAMPLE got_h;
static int32_t got_rate;
static float got_l, got_r;
static void __stdcall fake_rate(HSAMPLE h, int32_t rate) { got_h = h; got_rate = rate; }
static void __stdcall fake_volume(HSAMPLE h, float l, float r) { got_h = h; got_l = l; got_r = r; }

static uint32_t g_esp_before, g_esp_after, g_esi_after, g_esi_in;
static void *g_thunk, *g_arg_h;
static uint32_t g_arg_a, g_arg_b;

/* push b, push a (rate) / push b, push a, push h order matches stdcall */
static __attribute__((noinline)) void call_rate(void)
{
    __asm__ volatile(
        "pushl %%esi\n\t"
        "movl %%esp, %0\n\t"
        "pushl %4\n\t"
        "pushl %5\n\t"
        "movl %6, %%esi\n\t"
        "call *%3\n\t"
        "ssp_test_ret_rate_sym:\n\t"
        "movl %%esp, %1\n\t"
        "movl %%esi, %2\n\t"
        "popl %%esi\n\t"
        : "=m"(g_esp_before), "=m"(g_esp_after), "=m"(g_esi_after)
        : "m"(g_thunk), "m"(g_arg_a), "m"(g_arg_h), "m"(g_esi_in)
        : "eax", "ecx", "edx", "memory", "cc");
}

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
        "ssp_test_ret_volume_sym:\n\t"
        "movl %%esp, %1\n\t"
        "movl %%esi, %2\n\t"
        "popl %%esi\n\t"
        : "=m"(g_esp_before), "=m"(g_esp_after), "=m"(g_esi_after)
        : "m"(g_thunk), "m"(g_arg_a), "m"(g_arg_b), "m"(g_arg_h), "m"(g_esi_in)
        : "eax", "ecx", "edx", "memory", "cc");
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main(void)
{
    static uint8_t object[OBJ_SNAP_BYTES];
    float f44 = 44100.0f, f48 = 1.25f, l = 0.25f, r = 0.75f;
    HandleInfo *e;
    int i;
    memcpy(object + 0x44, &f44, 4);
    memcpy(object + 0x48, &f48, 4);
    InitializeCriticalSection(&g_cs);
    g_set_rate = fake_rate;
    g_set_volume = fake_volume;
    g_obj_ok = 1;
    g_esi_in = (uint32_t)(uintptr_t)object;
    g_arg_h = (void *)0x1234;

    for (i = 0; i < 1000; i++) {
        g_thunk = (void *)ssp_rate_thunk;
        g_arg_a = (uint32_t)(44100 + i);
        call_rate();
        CHECK(g_esp_before == g_esp_after);
        CHECK(g_esi_after == g_esi_in);
    }
    CHECK(got_h == (HSAMPLE)0x1234 && got_rate == 44100 + 999);
    e = find_handle((HSAMPLE)0x1234, 0);
    CHECK(e && e->rate_calls == 1000 && e->rate_last == 44100 + 999);
    CHECK(e && e->obj == g_esi_in && e->f44 == 44100.0f && e->f48 == 1.25f);

    g_thunk = (void *)ssp_volume_thunk;
    memcpy(&g_arg_a, &l, 4);
    memcpy(&g_arg_b, &r, 4);
    call_volume();
    CHECK(g_esp_before == g_esp_after);
    CHECK(g_esi_after == g_esi_in);
    CHECK(got_l == 0.25f && got_r == 0.75f);
    CHECK(e && e->vol_calls == 1 && e->vol_l == 0.25f && e->vol_r == 0.75f);

    printf(failures ? "thunk_test: %d FAILED\n" : "thunk_test: PASS (%d)\n", failures ? failures : 1006);
    return failures ? 1 : 0;
}
